# Compile shaders on the host BEFORE Ninja and NSP packaging.
# Reads published HLSL from shaders/deko3d/hlsl.
[CmdletBinding()]
param(
    [string]$DevkitPro = $(if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' }),
    [string]$Python
)
$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Python) {
    $Python = Join-Path $project '.venv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { $Python = 'python' }
}
$uam = Join-Path $DevkitPro 'tools\bin\uam.exe'
if (-not (Test-Path -LiteralPath $uam -PathType Leaf)) { throw "uam not found: $uam" }
$engine = Join-Path $project 'third_party\ps3recomp\libs\video\rsx_draw_engine.c'
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) {
    throw 'ps3recomp not set up; run scripts/setup/setup.ps1 first.'
}
$arguments = @((Join-Path $project 'scripts\gen-deko3d-shaders.py'),
               '--uam', $uam, '--incremental')
& $Python @arguments
if ($LASTEXITCODE -ne 0) { throw "Deko3D shader prebuild failed (exit $LASTEXITCODE); NSP NOT generated." }
