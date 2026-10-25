[CmdletBinding()]
param(
    [string]$DeviceName = 'Switch',
    [int]$FileTimeoutSeconds = 900
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\switch-games.ps1')
$files = @(Get-SwitchGameDataFiles 'dbz3')

function Get-MtpChild($folder, [string]$name) {
    return $folder.Items() | Where-Object { $_.Name -eq $name } | Select-Object -First 1
}

function Ensure-MtpFolder($folder, [string]$name) {
    $item = Get-MtpChild $folder $name
    if (-not $item) {
        $folder.NewFolder($name)
        $deadline = (Get-Date).AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 500
            $item = Get-MtpChild $folder $name
        } while (-not $item -and (Get-Date) -lt $deadline)
    }
    if (-not $item -or -not $item.IsFolder) { throw "Cannot create MTP folder: $name" }
    return $item.GetFolder
}

$shell = New-Object -ComObject Shell.Application
$computer = $shell.NameSpace(17)
if (-not $computer) { throw 'Cannot open This PC through Windows Shell.' }
$device = $computer.Items() | Where-Object { $_.Name -eq $DeviceName } | Select-Object -First 1
if (-not $device) { throw "MTP device not found: $DeviceName. Start DBI > Run MTP responder." }
$storage = $device.GetFolder.Items() | Where-Object { $_.Name -match '^\d+: SD Card$' } | Select-Object -First 1
if (-not $storage) { throw 'DBI SD Card folder not found.' }
$target = Ensure-MtpFolder $storage.GetFolder 'switch'
$target = Ensure-MtpFolder $target 'dbz-budokai-3-switch'
$target = Ensure-MtpFolder $target 'game'
$target = Ensure-MtpFolder $target 'PS3_GAME'

$files = $files | Sort-Object Length

foreach ($file in $files) {
    $parts = $file.Relative -split '\\'
    $folder = $target
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        $folder = Ensure-MtpFolder $folder $parts[$i]
    }
    $name = $parts[-1]
    $existing = Get-MtpChild $folder $name
    if ($existing -and [long]$existing.ExtendedProperty('System.Size') -eq $file.Length) {
        Write-Host "Already present: $($file.Relative)"
        continue
    }
    Write-Host ("Copying {0} ({1:N1} MiB)..." -f $file.Relative, ($file.Length / 1MB))
    $folder.CopyHere($file.Source, 16)
    $deadline = (Get-Date).AddSeconds($FileTimeoutSeconds)
    do {
        Start-Sleep -Seconds 2
        $copied = Get-MtpChild $folder $name
        if ($copied -and [long]$copied.ExtendedProperty('System.Size') -eq $file.Length) { break }
    } while ((Get-Date) -lt $deadline)
    if (-not $copied -or [long]$copied.ExtendedProperty('System.Size') -ne $file.Length) {
        throw "MTP copy did not complete: $($file.Relative)"
    }
    Write-Host "Verified: $($file.Relative)"
}

Write-Host "All $($files.Count) game files are present in Switch/1: SD Card/switch/dbz-budokai-3-switch/game/PS3_GAME"
