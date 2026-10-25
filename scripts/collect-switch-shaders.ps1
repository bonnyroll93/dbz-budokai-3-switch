# Pull a Switch run, compile its shaders from the project folder, and keep them for GitHub.
# Use -CaptureDir to process a run already copied to the PC without DBI.
[CmdletBinding()]
param(
    [string]$CaptureDir,
    [string]$Game = 'dbz3',
    [string]$DeviceName = 'Switch'
)

$ErrorActionPreference = 'Stop'
$project = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $CaptureDir) {
    $run = & (Join-Path $PSScriptRoot 'pull-switch-logs.ps1') -Game $Game -DeviceName $DeviceName -ShadersOnly
    if (-not $run) { throw 'The Switch run could not be copied.' }
    $CaptureDir = Join-Path ($run | Select-Object -Last 1) 't23-shaders'
}
if (-not (Test-Path -LiteralPath $CaptureDir -PathType Container)) {
    throw "No shader capture folder at $CaptureDir. Run the game on Switch first, then start DBI > Run MTP responder."
}
$CaptureDir = (Resolve-Path -LiteralPath $CaptureDir).Path
$captures = @(Get-ChildItem -LiteralPath $CaptureDir -File | Where-Object {
    $_.Name -match '^(vs|ps)_[0-9a-fA-F]{16}\.hlsl$'
})
if ($captures.Count -eq 0) {
    throw "No Switch shader captures in $CaptureDir. Run a scene that uses Deko3D, then copy the run again."
}

$published = Join-Path $project 'shaders\deko3d\hlsl'
New-Item -ItemType Directory -Force -Path $published | Out-Null
$newNames = @()
foreach ($capture in $captures) {
    $target = Join-Path $published $capture.Name.ToLowerInvariant()
    if (Test-Path -LiteralPath $target) {
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne
            (Get-FileHash -LiteralPath $capture.FullName -Algorithm SHA256).Hash) {
            throw "Conflicting published shader: $target"
        }
        continue
    }
    $newNames += $capture.Name.ToLowerInvariant()
}

# The importer verifies each content hash and compiles the complete directory.
# Roll back only files introduced by this run if a shader fails compilation.
try {
    & (Join-Path $PSScriptRoot 'import-shaders.ps1') -CaptureDir $CaptureDir
} catch {
    foreach ($name in $newNames) {
        $target = Join-Path $published $name
        if (Test-Path -LiteralPath $target -PathType Leaf) {
            Remove-Item -LiteralPath $target -Force
        }
    }
    throw
}
Write-Host "$($captures.Count) valid captures; $($newNames.Count) new shader files saved in $published"
Write-Host 'Rebuild the NSP with .\scripts\build\switch-dev.ps1 -Game dbz3'
Write-Host 'The shaders/deko3d/hlsl folder is ready to commit when this project is connected to GitHub.'
