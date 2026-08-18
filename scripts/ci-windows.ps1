# Build and test the project on Windows (MSVC, Debug).
# Usage:  .\scripts\ci-windows.ps1 [-BuildDir build] [-Config Debug]

param(
    [string]$BuildDir = "build",
    [string]$Config = "Debug"
)

$ErrorActionPreference = "Stop"

Write-Host "== configure (MSVC $Config) =="
cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== build =="
cmake --build $BuildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "== test =="
ctest --test-dir $BuildDir -C $Config --output-on-failure
exit $LASTEXITCODE
