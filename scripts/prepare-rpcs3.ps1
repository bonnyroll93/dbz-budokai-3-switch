[CmdletBinding()]
param([string]$Rpcs3Root)

$ErrorActionPreference = 'Stop'
$workspace = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Rpcs3Root) { $Rpcs3Root = Join-Path $workspace 'userdata\tools\rpcs3' }
if (-not (Test-Path -LiteralPath (Join-Path $Rpcs3Root 'rpcs3.exe') -PathType Leaf)) {
    throw "RPCS3 executable not found under $Rpcs3Root"
}

# Keep RPCS3's virtual console and generated configuration beside the portable binary.
foreach ($relative in @(
    'config', 'dev_bdvd', 'dev_flash', 'dev_hdd0\game', 'dev_hdd0\home\00000001\exdata',
    'dev_hdd1', 'dev_usb000', 'GuiConfigs', 'Icons', 'input_configs', 'log', 'patches', 'cache'
)) {
    New-Item -ItemType Directory -Path (Join-Path $Rpcs3Root $relative) -Force | Out-Null
}
Write-Host "RPCS3 portable folders are ready: $Rpcs3Root"
