# Publish the tested development build as a release.
#
#   .\scripts\build\switch-publish.ps1 -Game dbz3 -Version 0.15.0 -Feature alphafix
#
# Repackages the ELF the dev build was made from with the release version into
# build\releases\<game>\v<X.Y.Z>-<feature>\<NspBase>-v<X.Y.Z>.nsp, and moves the
# dev logs (build\logs\<game>\dev\runNN) into build\logs\<game>\v<X.Y.Z>-<feature>\.
# It refuses when the ELF has been rebuilt since the dev build was packaged, so
# what gets published is exactly what was tested. Publishing the same version
# and feature again overwrites that release.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Game,
    [Parameter(Mandatory)][string]$Version,
    [Parameter(Mandatory)][string]$Feature,
    # Publish even if the ELF no longer matches the dev build.
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\switch-games.ps1')
$tag = Get-SwitchTag $Version $Feature
$elf = Get-SwitchElf $Game
$devInfo = Read-SwitchBuildInfo (Join-Path (Get-SwitchDevDir $Game) 'build-info.txt')
if (-not $devInfo.ContainsKey('elf_sha256')) {
    throw "No development build for ${Game}: run switch-dev.ps1 first"
}
$sha = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
if ($sha -ne $devInfo['elf_sha256'] -and -not $Force) {
    throw ("The ELF was rebuilt after the development build ({0}); this release would " +
           "differ from the tested build. Run switch-dev.ps1 again, or use -Force.") -f $devInfo['packaged']
}

$nsp = Get-SwitchNspPath $Game $Version $tag
Write-Host "Publishing $Game $tag..."
& (Join-Path $PSScriptRoot 'package-switch.ps1') -Game $Game -OutputNsp $nsp `
    -DisplayVersion $Version -Feature $Feature
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw "Packaging failed (exit $LASTEXITCODE)" }
Add-Content -LiteralPath (Join-Path (Split-Path -Parent $nsp) 'build-info.txt') `
    -Value ("tested_as_dev=" + $devInfo['packaged']) -Encoding utf8

# Dev runs become this release's runs, numbered after any already there.
$project = Get-SwitchProject
$devLogs = Join-Path $project "build\logs\$Game\dev"
$relLogs = Join-Path $project "build\logs\$Game\$tag"
if (Test-Path -LiteralPath $devLogs) {
    New-Item -ItemType Directory -Force -Path $relLogs | Out-Null
    $n = 1
    foreach ($run in Get-ChildItem -LiteralPath $devLogs -Directory | Sort-Object Name) {
        while (Test-Path -LiteralPath (Join-Path $relLogs ('run{0:D2}' -f $n))) { $n++ }
        Move-Item -LiteralPath $run.FullName -Destination (Join-Path $relLogs ('run{0:D2}' -f $n))
    }
    Write-Host "Development logs moved to: $relLogs"
}
Write-Host "Release NSP created at $nsp. Copy and install it manually."
