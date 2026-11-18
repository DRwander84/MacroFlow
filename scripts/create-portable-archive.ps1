param(
    [Parameter(Mandatory = $true)][string]$SourceDir,
    [Parameter(Mandatory = $true)][string]$OutputFile
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
    throw "Portable staging folder not found: $SourceDir"
}
$parent = Split-Path -Parent $OutputFile
if ($parent -and -not (Test-Path -LiteralPath $parent)) {
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
}
if (Test-Path -LiteralPath $OutputFile) {
    Remove-Item -LiteralPath $OutputFile -Force
}
Compress-Archive -Path (Join-Path $SourceDir '*') -DestinationPath $OutputFile -CompressionLevel Optimal
Write-Host "Portable archive created: $OutputFile"
