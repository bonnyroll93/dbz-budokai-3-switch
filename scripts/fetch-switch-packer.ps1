[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$checkout = Join-Path $project 'build\tools\nton-src'
$binary = Join-Path $checkout 'nton\bin\hacbrewpack.exe'
$expectedCommit = 'c5d543a91b7cd1df880f98c9ab831be9a40d024f'
$expectedHash = 'A4A78E20EFEB00DEA69A4B02F5D16F7C6161B7AA9AF802517756BA019CB7F8C4'

if (-not (Test-Path -LiteralPath $checkout)) {
    New-Item -ItemType Directory -Force (Split-Path -Parent $checkout) | Out-Null
    & git clone --depth 1 --branch v3.0.1 https://github.com/rlaphoenix/nton.git $checkout
    if ($LASTEXITCODE -ne 0) { throw 'Could not download NTON v3.0.1' }
}
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing hacBrewPack: $binary" }
$commit = (& git -C $checkout rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -ne $expectedCommit) {
    throw "Unexpected NTON revision: $commit"
}
$hash = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash
if ($hash -ne $expectedHash) { throw "Unexpected hacBrewPack SHA-256: $hash" }
Write-Host "Verified hacBrewPack: $binary"
