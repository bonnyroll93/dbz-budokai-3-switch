[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$FirmwarePath,
    [string]$Rpcs3
)

$ErrorActionPreference = "Stop"
$workspace = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Rpcs3) { $Rpcs3 = Join-Path $workspace 'userdata\tools\rpcs3\rpcs3.exe' }
$rpcs3 = $Rpcs3

if (-not (Test-Path -LiteralPath $rpcs3 -PathType Leaf)) {
    throw "RPCS3 not found at $rpcs3. Reinstall the official portable build first."
}

$firmware = (Resolve-Path -LiteralPath $FirmwarePath).Path
if ((Get-Item -LiteralPath $firmware).Length -lt 1MB) {
    throw "Firmware file is empty or too small to be a PS3UPDAT.PUP: $firmware"
}
if ([System.IO.Path]::GetFileName($firmware) -ine "PS3UPDAT.PUP") {
    throw "Firmware must be named PS3UPDAT.PUP: $firmware"
}

$rpcs3Root = Split-Path -Parent $rpcs3
& (Join-Path $PSScriptRoot 'prepare-rpcs3.ps1') -Rpcs3Root $rpcs3Root
Write-Host "Starting RPCS3 firmware installer..."
Write-Host "Confirm the firmware installation in the RPCS3 window, then close RPCS3."

$process = Start-Process -FilePath $rpcs3 `
    -WorkingDirectory $rpcs3Root `
    -ArgumentList @('--installfw', ('"' + $firmware + '"')) `
    -PassThru -Wait

if ($process.ExitCode -ne 0) {
    throw "RPCS3 firmware installer exited with code $($process.ExitCode)."
}

$devFlash = Join-Path $rpcs3Root 'dev_flash'
$requiredFirmware = @('sys', 'vsh', 'data') | ForEach-Object { Join-Path $devFlash $_ }
$missingFirmware = @($requiredFirmware | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Container) })
if ($missingFirmware.Count -gt 0) {
    throw "RPCS3 exited successfully, but no installed firmware was found in $devFlash."
}

Write-Host "Firmware installed under $devFlash"
