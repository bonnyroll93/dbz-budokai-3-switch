[CmdletBinding()]
param([Parameter(Mandatory)][string]$CaptureDir,
      [string]$Uam = $(if ($env:DEVKITPRO) { Join-Path $env:DEVKITPRO 'tools\bin\uam.exe' } else { 'C:\devkitPro\tools\bin\uam.exe' }))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$python = Join-Path $root '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run setup.ps1 first.' }
$resolved = (Resolve-Path -LiteralPath $CaptureDir -ErrorAction Stop).Path
& $python (Join-Path $PSScriptRoot 'gen-deko3d-shaders.py') --uam $Uam --capture-dir $resolved
if ($LASTEXITCODE -ne 0) { throw 'Shader import/compilation failed.' }
Write-Host 'Shader import and compilation completed.'
