[CmdletBinding()]
param(
    [switch]$NoBuild,
    [ValidateRange(1, 16)][int]$Jobs = 4,
    [string]$DevkitPro = $(if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' })
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\switch-games.ps1')
$project = Get-SwitchProject
if (-not $NoBuild) {
    $DevkitPro = (Resolve-Path -LiteralPath $DevkitPro -ErrorAction Stop).Path
    $env:DEVKITPRO = $DevkitPro.Replace('\', '/')
    $env:DEVKITA64 = (Join-Path $DevkitPro 'devkitA64').Replace('\', '/')
    & (Join-Path $PSScriptRoot 'prebuild-shaders.ps1') -DevkitPro $DevkitPro
    & (Join-Path $PSScriptRoot 'configure-switch.ps1') -DevkitPro $DevkitPro
    $bash = Join-Path $DevkitPro 'msys2\usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash)) { throw "MSYS2 not found: $bash" }
    & $bash -lc "cd '$(Get-SwitchMsysPath)' && ninja -C build/cmake/switch-dbz3 -j$Jobs"
    if ($LASTEXITCODE -ne 0) { throw 'Switch build failed.' }
}
$output = Join-Path $project 'build\dev\dbz3\Dragonball-Z-Budokai-3-full.nsp'
& (Join-Path $PSScriptRoot 'package-switch.ps1') -Game dbz3 -OutputNsp $output `
    -DisplayVersion 'full' -Feature 'local-full' -Full
Write-Host "Full NSP: $output"
