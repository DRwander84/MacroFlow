# Usage (x64 Native Tools / any PowerShell with Qt on PATH):
#   .\scripts\build.ps1 -QtDir "C:\Qt\6.7.2\msvc2019_64"
param([string]$QtDir = $env:QT_DIR, [string]$Config = "Release")
if (-not $QtDir) { Write-Error "Pass -QtDir (e.g. C:\Qt\6.7.2\msvc2019_64) or set QT_DIR"; exit 1 }
$root = Split-Path $PSScriptRoot -Parent
cmake -S $root -B "$root\build" -G "Visual Studio 18 2026" -A x64 "-DCMAKE_PREFIX_PATH=$QtDir"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build "$root\build" --config $Config --parallel
if ($LASTEXITCODE) { exit $LASTEXITCODE }
ctest --test-dir "$root\build" -C $Config --output-on-failure
