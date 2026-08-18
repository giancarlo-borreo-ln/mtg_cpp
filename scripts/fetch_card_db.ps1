# Fetch the Scryfall "Default Cards" bulk export into .\data as JSONL.
#
# Usage:   .\scripts\fetch_card_db.ps1 [-Refresh]
# Env:     $env:MTG_CPP_DATA_DIR overrides the destination directory (default: .\data)
#
# The bulk export is a gzipped JSONL stream (~77 MB compressed, ~500-600 MB
# decompressed). This script queries the Scryfall /bulk-data API for the current
# artifact, downloads it with curl.exe resume support, verifies gzip integrity
# via .NET, decompresses to data\default-cards.jsonl, sanity-checks the records,
# and records provenance in data\manifest.json. The compressed copy is deleted
# after a successful decompression.
#
# Idempotent: exits early when the manifest's Scryfall `updated_at` still matches
# the API. Use -Refresh to force a re-download.
#
# Requires: PowerShell 5.1+, curl.exe (Windows 10 1803+).

[CmdletBinding()]
param(
    [switch]$Refresh
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$dataDir = if ($env:MTG_CPP_DATA_DIR) { $env:MTG_CPP_DATA_DIR } else { Join-Path $repoRoot "data" }

$bulkApi = "https://api.scryfall.com/bulk-data"
$jsonlPath = Join-Path $dataDir "default-cards.jsonl"
$gzPath = Join-Path $dataDir "default-cards.jsonl.gz"
$manifestPath = Join-Path $dataDir "manifest.json"
$jsonlTmp = Join-Path $dataDir ".default-cards.jsonl.tmp"
$manifestTmp = Join-Path $dataDir ".manifest.json.tmp"

Write-Host "== querying $bulkApi"
$bulk = Invoke-RestMethod -Uri $bulkApi -TimeoutSec 60
$entry = $bulk.data | Where-Object { $_.type -eq "default_cards" } | Select-Object -First 1
if (-not $entry -or -not $entry.jsonl_download_uri) {
    throw "Could not find the default_cards bulk entry in the Scryfall API response."
}
$downloadUri = [string]$entry.jsonl_download_uri
$updatedAt = [string]$entry.updated_at
Write-Host "   entry: $($entry.name) (updated $updatedAt)"

if (-not $Refresh -and (Test-Path $jsonlPath) -and (Test-Path $manifestPath)) {
    $stored = (Get-Content $manifestPath -Raw | ConvertFrom-Json).scryfall_updated_at
    if ($stored -eq $updatedAt) {
        Write-Host "Card DB is up to date ($updatedAt). Use -Refresh to re-download."
        exit 0
    }
}

New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

try {
    Write-Host "== downloading $downloadUri"
    & curl.exe -fSL --retry 3 -C - -o $gzPath $downloadUri
    if ($LASTEXITCODE -ne 0) { throw "curl download failed (exit $LASTEXITCODE)" }
    if (-not (Test-Path $gzPath) -or (Get-Item $gzPath).Length -eq 0) {
        throw "download produced an empty file"
    }

    $gzBytes = (Get-Item $gzPath).Length
    $gzSha = (Get-FileHash -Algorithm SHA256 -Path $gzPath).Hash.ToLowerInvariant()

    Write-Host "== verifying gzip integrity"
    $in = [System.IO.File]::OpenRead($gzPath)
    try {
        $gzip = [System.IO.Compression.GZipStream]::new($in, [System.IO.Compression.CompressionMode]::Decompress)
        try {
            # Draining the whole stream validates the gzip CRC32 trailer.
            $buffer = New-Object byte[] 65536
            while ($gzip.Read($buffer, 0, $buffer.Length) -gt 0) { }
        }
        finally { $gzip.Dispose() }
    }
    finally { $in.Dispose() }

    Write-Host "== decompressing to $jsonlPath"
    $in = [System.IO.File]::OpenRead($gzPath)
    try {
        $out = [System.IO.File]::Create($jsonlTmp)
        try {
            $gzip = [System.IO.Compression.GZipStream]::new($in, [System.IO.Compression.CompressionMode]::Decompress)
            try { $gzip.CopyTo($out) }
            finally { $gzip.Dispose() }
        }
        finally { $out.Dispose() }
    }
    finally { $in.Dispose() }
    Move-Item -Force $jsonlTmp $jsonlPath

    $jsonlBytes = (Get-Item $jsonlPath).Length
    $jsonlSha = (Get-FileHash -Algorithm SHA256 -Path $jsonlPath).Hash.ToLowerInvariant()

    Write-Host "== sanity check"
    $lineCount = 0
    $firstName = ""
    $reader = [System.IO.StreamReader]::new($jsonlPath)
    try {
        while ($null -ne ($line = $reader.ReadLine())) {
            if ($line) { $lineCount++ }
            if (-not $firstName -and $line) {
                $obj = $line | ConvertFrom-Json
                $firstName = $obj.name
            }
        }
    }
    finally { $reader.Dispose() }
    if (-not $firstName) { throw "first record has no name; file looks corrupt" }
    Write-Host "   records: $lineCount | first: $firstName"

    Write-Host "== writing $manifestPath"
    $manifest = [ordered]@{
        source              = "default_cards"
        name                = $entry.name
        downloaded_at       = (Get-Date).ToUniversalTime().ToString("yyyy-MM-ddTHH:mm:ssZ")
        scryfall_updated_at = $updatedAt
        download_uri        = $downloadUri
        gz_bytes            = $gzBytes
        gz_sha256           = $gzSha
        jsonl_bytes         = $jsonlBytes
        jsonl_sha256        = $jsonlSha
    }
    ($manifest | ConvertTo-Json) | Set-Content -Encoding UTF8 -Path $manifestTmp
    Move-Item -Force $manifestTmp $manifestPath

    Remove-Item $gzPath -Force
    Write-Host "== done: $jsonlPath ($jsonlBytes bytes, $lineCount records)"
}
finally {
    Remove-Item $gzPath, $jsonlTmp, $manifestTmp -Force -ErrorAction SilentlyContinue
}
