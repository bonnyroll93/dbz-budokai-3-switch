[CmdletBinding()]
param(
    [string]$Python = "C:\Python314\python.exe",
    [switch]$Recreate
)

$ErrorActionPreference = "Stop"
$workspace = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$venv = Join-Path $workspace ".venv"
$venvPython = Join-Path $venv "Scripts\python.exe"
$requirements = Join-Path $workspace 'requirements.txt'
$taskTemp = Join-Path $workspace ".tmp\python-setup"

New-Item -ItemType Directory -Path $taskTemp -Force | Out-Null
$env:TEMP = $taskTemp
$env:TMP = $taskTemp

if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) {
    $detected = Get-Command python.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $detected) {
        throw "Python was not found. Pass its path with -Python."
    }
    $Python = $detected.Source
}

if (-not (Test-Path -LiteralPath $requirements -PathType Leaf)) {
    throw "Requirements file not found: $requirements"
}

if ($Recreate -and (Test-Path -LiteralPath $venv)) {
    $resolvedVenv = (Resolve-Path -LiteralPath $venv).Path
    if ($resolvedVenv -ne (Join-Path $workspace ".venv")) {
        throw "Refusing to remove unexpected virtual environment path: $resolvedVenv"
    }
    Remove-Item -LiteralPath $resolvedVenv -Recurse -Force
}

if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    Write-Host "Creating virtual environment: $venv"
    & $Python -m venv $venv
    if ($LASTEXITCODE -ne 0) {
        throw "Python failed to create the virtual environment (exit $LASTEXITCODE)."
    }
}

Write-Host "Installing ps3recomp Python dependencies..."
& $venvPython -m pip install --disable-pip-version-check -r $requirements
if ($LASTEXITCODE -ne 0) {
    throw "pip failed with exit code $LASTEXITCODE."
}

Write-Host "Python environment ready: $venvPython"
& $venvPython -c "import capstone, construct, Crypto, tabulate, tqdm; print('Dependencies: OK')"
if ($LASTEXITCODE -ne 0) {
    throw "Dependency import check failed."
}
