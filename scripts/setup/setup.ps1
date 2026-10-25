[CmdletBinding()]
param([switch]$SkipPython, [switch]$SkipShaderGeneration, [switch]$SkipRpcs3)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sdk = Join-Path $root 'third_party\ps3recomp'
$revision = 'a2c20108af45174dbb7d3e2f87f6acaaef09c6b4'
$patch = Join-Path $root 'patches\ps3recomp.patch'
$overlay = Join-Path $root 'patches\ps3recomp-overlay'
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git is required to fetch ps3recomp.' }
if (-not (Test-Path -LiteralPath (Join-Path $sdk '.git'))) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $sdk) -Force | Out-Null
    & git clone https://github.com/sp00nznet/ps3recomp.git $sdk
    if ($LASTEXITCODE -ne 0) { throw 'Could not download ps3recomp.' }
    & git -C $sdk checkout --detach $revision
    if ($LASTEXITCODE -ne 0) { throw 'Could not select pinned ps3recomp revision.' }
    & git -C $sdk apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'Local ps3recomp patch does not apply cleanly.' }
    & git -C $sdk apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'Could not apply local ps3recomp patch.' }
    Get-ChildItem -LiteralPath $overlay -Recurse -File | ForEach-Object {
        $relative = $_.FullName.Substring($overlay.Length + 1)
        $destination = Join-Path $sdk $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
        Copy-Item -LiteralPath $_.FullName -Destination $destination
    }
} elseif ((& git -C $sdk rev-parse HEAD).Trim() -ne $revision) {
    throw 'Existing ps3recomp checkout has a different base revision. Preserve it and use a fresh directory.'
}
if (-not $SkipPython) { & (Join-Path $PSScriptRoot '..\setup-python.ps1') }
if (-not $SkipRpcs3) { & (Join-Path $PSScriptRoot '..\fetch-rpcs3.ps1') }
if (-not $SkipRpcs3) { & (Join-Path $PSScriptRoot '..\prepare-rpcs3.ps1') }
& (Join-Path $PSScriptRoot '..\fetch-switch-packer.ps1')
if (-not $SkipPython -and -not (Test-Path -LiteralPath (Join-Path $root '.venv\Scripts\python.exe') -PathType Leaf)) {
    throw 'Python environment required by import-game.ps1 was not created.'
}
if (-not $SkipRpcs3 -and -not (Test-Path -LiteralPath (Join-Path $root 'userdata\tools\rpcs3\rpcs3.exe') -PathType Leaf)) {
    throw 'RPCS3 required by import-game.ps1 was not installed.'
}
foreach ($tool in @('ppu_lifter.py', 'gen_hle_nids.py', 'extract_spu_images.py', 'build_spu_workloads.py')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdk "tools\$tool") -PathType Leaf)) {
        throw "ps3recomp tool required by import-game.ps1 is missing: $tool"
    }
}
if (-not $SkipShaderGeneration) {
    $uam = if ($env:DEVKITPRO) { Join-Path $env:DEVKITPRO 'tools\bin\uam.exe' } else { 'C:\devkitPro\tools\bin\uam.exe' }
    if (Test-Path -LiteralPath $uam) {
        $python = Join-Path $root '.venv\Scripts\python.exe'
        if (-not (Test-Path -LiteralPath $python)) { $python = 'python' }
        & $python (Join-Path $PSScriptRoot '..\gen-deko3d-shaders.py') --uam $uam
        if ($LASTEXITCODE -ne 0) { throw 'Shader generation failed.' }
    } else {
        Write-Warning 'uam not found; generate the deko3d shader header after devkitPro is installed.'
    }
}
Write-Host 'Setup complete. Supply your own game PKG and PS3UPDAT.PUP to import-game.ps1.'
