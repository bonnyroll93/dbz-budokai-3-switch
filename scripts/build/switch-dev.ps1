# Development build: compile and package to build\dev\<game>\ (overwriting the
# previous dev build). Copy and install the NSP manually.
#
#   .\scripts\build\switch-dev.ps1 -Game dbz3 -Feature alphafix
#
# Test it, pull logs with pull-switch-logs.ps1 (they go to build\logs\<game>\dev),
# iterate; when the feature is done, publish it with switch-publish.ps1.
[CmdletBinding()]
param(
    [string]$Game = 'dbz3',
    # What is being worked on; recorded in build-info.txt and used by publish.
    [string]$Feature = 'wip',
    # Skip compilation and package the existing ELF.
    [switch]$NoBuild,
    # Record the innermost lifted PPU function every millisecond.
    [switch]$ProfilePpuFunctions,
    [ValidateRange(1, 16)]
    [int]$Jobs = 4,
    [string]$DevkitPro = $(if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' })
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\switch-games.ps1')
$g = Get-SwitchGame $Game
$nsp = Get-SwitchDevNsp $Game

if (-not $NoBuild) {
    $DevkitPro = (Resolve-Path -LiteralPath $DevkitPro -ErrorAction Stop).Path
    $env:DEVKITPRO = $DevkitPro.Replace('\', '/')
    $env:DEVKITA64 = (Join-Path $DevkitPro 'devkitA64').Replace('\', '/')
    & (Join-Path $PSScriptRoot 'prebuild-shaders.ps1') -DevkitPro $DevkitPro
    # Reconfigure on every run so changed CMake options and SDK paths are used.
    & (Join-Path $PSScriptRoot 'configure-switch.ps1') -DevkitPro $DevkitPro `
        -ProfilePpuFunctions:$ProfilePpuFunctions
    $bash = Join-Path $DevkitPro 'msys2\usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash -PathType Leaf)) { throw "MSYS2 not found: $bash" }
    $tree = "build/cmake/$($g.Tree)"
    # Recompile main.cpp every time so console logs identify this build.
    $mainSrc = Join-Path (Get-SwitchProject) 'ports\dbz3\main.cpp'
    if (Test-Path -LiteralPath $mainSrc) { (Get-Item -LiteralPath $mainSrc).LastWriteTime = Get-Date }
    Write-Host "Building $Game ($tree)..."
    & $bash -lc "cd '$(Get-SwitchMsysPath)' && ninja -C $tree -j$Jobs"
    if ($LASTEXITCODE -ne 0) { throw "Switch build failed (exit $LASTEXITCODE)" }
}

Write-Host "Creating development NSP ($Feature)..."
& (Join-Path $PSScriptRoot 'package-switch.ps1') -Game $Game -OutputNsp $nsp `
    -DisplayVersion 'dev' -Feature $Feature -DevkitPro $DevkitPro
Write-Host "NSP created at $nsp. Copy and install it manually."
