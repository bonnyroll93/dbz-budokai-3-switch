# Copy a test run's logs from the console (DBI "Run MTP responder"), numbering
# runs automatically:
#
#   .\scripts\pull-switch-logs.ps1 -Game dbz3                                -> build\logs\dbz3\dev\runNN
#   .\scripts\pull-switch-logs.ps1 -Game dbz3 -Version 0.15.0 -Feature alphafix  -> that release's folder
#
# Copies *.log, t23-shaders\, t28-tex\ and captures\ from SD:/switch/dbz-budokai-3-switch, plus the
# Atmosphere crash reports of the game's title. Never the game\ or hdd0\ data.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Game,
    [string]$Version,
    [string]$Feature,
    [string]$DeviceName = 'Switch',
    [switch]$ShadersOnly,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'switch-games.ps1')
$g = Get-SwitchGame $Game
$sub = if ($Version -or $Feature) { Get-SwitchTag $Version $Feature } else { 'dev' }
$root = Join-Path (Get-SwitchProject) "build\logs\$Game\$sub"
$shell = New-Object -ComObject Shell.Application
$device = $shell.NameSpace(17).Items() | Where-Object { $_.Name -like "*$DeviceName*" } | Select-Object -First 1
if (-not $device) { throw 'Switch not found: start DBI > Run MTP responder.' }
$sd = $device.GetFolder.Items() | Where-Object { $_.Name -like '*SD Card' -or $_.Name -like '1: SD*' } | Select-Object -First 1
if (-not $sd) { throw '"SD Card" storage not found on the device.' }
function Get-Child($folder, [string]$name) {
    $folder.GetFolder.Items() | Where-Object { $_.Name -eq $name } | Select-Object -First 1
}
$app = Get-Child (Get-Child $sd 'switch') 'dbz-budokai-3-switch'
if (-not $app) { throw 'Folder switch/dbz-budokai-3-switch not found on the SD card.' }

New-Item -ItemType Directory -Force -Path $root | Out-Null
$n = 1
while (Test-Path -LiteralPath (Join-Path $root ('run{0:D2}' -f $n))) { $n++ }
$dst = Join-Path $root ('run{0:D2}' -f $n)
New-Item -ItemType Directory -Force -Path $dst | Out-Null

$target = $shell.NameSpace($dst)
$wanted = @()
foreach ($item in $app.GetFolder.Items()) {
    if ($item.Name -eq 't23-shaders' -or
        (-not $ShadersOnly -and ($item.Name -like '*.log' -or $item.Name -eq 't28-tex' -or
                                $item.Name -eq 'captures'))) {
        $wanted += $item
    }
}
$crashes = $null
if (-not $ShadersOnly) {
    $atm = Get-Child $sd 'atmosphere'
    $crashes = if ($atm) { Get-Child $atm 'crash_reports' } else { $null }
    if ($crashes) {
        foreach ($item in $crashes.GetFolder.Items()) {
            if ($item.Name -like "*_$($g.TitleId.ToLowerInvariant()).log") { $wanted += $item }
        }
    }
}

# MTP copies are asynchronous: start each one and wait for it to land.
foreach ($item in $wanted) {
    $target.CopyHere($item, 16)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (-not (Test-Path -LiteralPath (Join-Path $dst $item.Name)) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
}
Start-Sleep -Seconds 5
$missing = @($wanted | Where-Object { -not (Test-Path -LiteralPath (Join-Path $dst $_.Name)) } | ForEach-Object { $_.Name })
Write-Host "Logs copied to: $dst"
if ($missing.Count) { Write-Warning ("Not copied: " + ($missing -join ', ')) }
Write-Output $dst
