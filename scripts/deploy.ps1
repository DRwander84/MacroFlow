param([string]$QtDir = $env:QT_DIR, [string]$Config = "Release")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Config\MacroFlow.exe"
$deployTool = Join-Path $QtDir "bin\windeployqt.exe"

if (-not $QtDir) {
    throw "Pass -QtDir (Qt MSVC x64 kit directory) or set QT_DIR."
}
if (-not (Test-Path -LiteralPath $deployTool -PathType Leaf)) {
    throw "windeployqt.exe not found: $deployTool"
}
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "MacroFlow.exe not found. Build the project first: $exe"
}

& $deployTool --release --no-translations --no-opengl-sw $exe
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE."
}
Write-Host "Qt runtime deployed beside: $exe"
