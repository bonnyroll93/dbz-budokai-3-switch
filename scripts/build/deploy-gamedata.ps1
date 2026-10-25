[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdRoot)

$ErrorActionPreference = 'Stop'
$sd = (Resolve-Path -LiteralPath $SdRoot -ErrorAction Stop).Path
$gamesScript = Join-Path $PSScriptRoot '..\switch-games.ps1'
. $gamesScript
$files = @(Get-SwitchGameDataFiles 'dbz3')
$target = Join-Path $sd 'switch\dbz-budokai-3-switch\game\PS3_GAME'
foreach ($file in $files) {
    $destination = Join-Path $target $file.Relative
    $directory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Copy-Item -LiteralPath $file.Source -Destination $destination -Force
    $copied = Get-Item -LiteralPath $destination
    if ($copied.Length -ne $file.Length) {
        throw "Game data copy verification failed: $($file.Relative)"
    }
}
Write-Host "Copied and verified $($files.Count) game files to $target"
