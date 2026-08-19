# M11.2 - package the Windows (MSVC) build into a self-contained zip.
#
# Stages a relocatable layout (works from any directory):
#
#   mtg_cpp-<version>-windows-x86_64\
#   ├── mtg_cpp.exe
#   ├── mtg_cpp_*.dll          # our shared libs
#   ├── sfml-*.dll, cpr/curl/ssl DLLs   # runtime deps copied next to the exe
#   └── assets\                # fonts + icon.svg (found next to the exe)
#
# Usage:  powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1
# Env:    MTG_CPP_SFML_DIR   overrides the prebuilt SFML dir (default .\..\sfml)
#         MTG_CPP_DIST_DIR   where the zip lands (default .\dist)

$ErrorActionPreference = "Stop"
$RepoDir = Split-Path -Parent $PSScriptRoot
$SfmlDir = if ($env:MTG_CPP_SFML_DIR) { $env:MTG_CPP_SFML_DIR } else { Join-Path $RepoDir "sfml" }
$DistDir = if ($env:MTG_CPP_DIST_DIR) { $env:MTG_CPP_DIST_DIR } else { Join-Path $RepoDir "dist" }

Set-Location $RepoDir

# Version from CMakeLists.txt (project(mtg_cpp VERSION <x.y.z> ...)).
$Version = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(mtg_cpp\s+VERSION\s+([0-9.]+)').Groups[1].Value
$BundleName = "mtg_cpp-$Version-windows-x86_64"
$Stage = Join-Path $RepoDir "build-release\bundle\$BundleName"

Write-Host "== mtg_cpp packaging (Windows) =="
Write-Host "version: $Version"

# 1. Release build (MSVC). The exe links the shared libs; the DLLs ship with it.
cmake -S . -B build-release -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DMTG_CPP_BUILD_TESTS=OFF
cmake --build build-release --config Release --target mtg_cpp

# 2. Stage the bundle.
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force -Path "$Stage\assets" | Out-Null
Copy-Item "$RepoDir\build-release\mtg_cpp.exe" $Stage
Copy-Item "$RepoDir\assets\*" "$Stage\assets\" -Recurse

# 3. Copy every runtime DLL next to the exe (Windows resolves DLLs from the
#    exe's directory). Sources: our shared libs, the SFML bin dir, and any DLL
#    the build produced (cpr/curl/OpenSSL/zlib land under build-release or
#    build-release\_deps).
$DllSources = @()
if (Test-Path $SfmlDir) {
  $DllSources += Get-ChildItem -Path "$SfmlDir\bin\*.dll" -ErrorAction SilentlyContinue
}
$DllSources += Get-ChildItem -Path "$RepoDir\build-release\*.dll" -ErrorAction SilentlyContinue
$DllSources += Get-ChildItem -Path "$RepoDir\build-release\bin\*.dll" -Recurse -ErrorAction SilentlyContinue
$DllSources += Get-ChildItem -Path "$RepoDir\build-release\_deps\*\*\*.dll" -Recurse -ErrorAction SilentlyContinue
foreach ($dll in $DllSources | Sort-Object FullName -Unique) {
  Copy-Item $dll.FullName $Stage
}

# 4. Zip the bundle.
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
$ZipPath = Join-Path $DistDir "$BundleName.zip"
if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path $Stage -DestinationPath $ZipPath

Write-Host "== packaged: $ZipPath =="
Get-ChildItem $Stage | Select-Object Name | Format-Table -HideTableHeaders
