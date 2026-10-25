[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$destination = Join-Path $root 'userdata\tools\rpcs3'
if (Test-Path -LiteralPath (Join-Path $destination 'rpcs3.exe')) {
    & (Join-Path $PSScriptRoot 'prepare-rpcs3.ps1') -Rpcs3Root $destination
    Write-Host "RPCS3 already installed: $destination"
    return
}
$tag = 'build-ffc50905a640146ddd66ee15d29d90f5841e84f0'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/RPCS3/rpcs3-binaries-win/releases/tags/$tag"
$asset = @($release.assets | Where-Object { $_.name -match '\.7z$' })
if ($asset.Count -ne 1) { throw "Expected one Windows archive in RPCS3 release $tag." }
$hashMatch = [regex]::Match([string]$release.body, '(?i)\b([0-9a-f]{64});\s*\d+B\b')
if (-not $hashMatch.Success) { throw 'RPCS3 release does not publish the expected SHA-256 checksum.' }
$expected = $hashMatch.Groups[1].Value.ToUpperInvariant()
$downloadDir = Join-Path $root 'userdata\tools\rpcs3-download'
$unpack = Join-Path $downloadDir 'unpacked'
$archive = Join-Path $downloadDir $asset[0].name
New-Item -ItemType Directory -Path $unpack -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri $asset[0].browser_download_url -OutFile $archive
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw "RPCS3 archive checksum mismatch: $actual" }
& tar.exe -xf $archive -C $unpack
if ($LASTEXITCODE -ne 0) { throw 'Could not extract the verified RPCS3 archive.' }
$binary = Get-ChildItem -LiteralPath $unpack -Recurse -File -Filter 'rpcs3.exe' |
    Select-Object -First 1
if (-not $binary) { throw 'RPCS3 executable not found in downloaded archive.' }
$source = Split-Path -Parent $binary.FullName
if (Test-Path -LiteralPath $destination) {
    if ((Get-ChildItem -LiteralPath $destination -Force | Measure-Object).Count -gt 0) {
        throw "RPCS3 target exists but is incomplete: $destination. Move it aside, then rerun setup."
    }
    Remove-Item -LiteralPath $destination -Force
}
New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
Move-Item -LiteralPath $source -Destination $destination
& (Join-Path $PSScriptRoot 'prepare-rpcs3.ps1') -Rpcs3Root $destination
Write-Host "Verified RPCS3 ${tag}: $destination"
