[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Pkg,
    [Parameter(Mandatory)][string]$Pup,
    [string]$Rpcs3,
    [ValidateRange(1, 64)][int]$Jobs = 1
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$pkgPath = (Resolve-Path -LiteralPath $Pkg).Path
$pupPath = (Resolve-Path -LiteralPath $Pup).Path
if (-not $Rpcs3) { $Rpcs3 = Join-Path $root 'userdata\tools\rpcs3\rpcs3.exe' }
$rpcs3Exe = (Resolve-Path -LiteralPath $Rpcs3 -ErrorAction Stop).Path
$rpcs3Root = Split-Path -Parent $rpcs3Exe
$installed = Join-Path $rpcs3Root 'dev_hdd0\game\BLUS30966\PS3_GAME'
$extractRoot = Join-Path $root 'userdata\Extracted\dev_hdd0\GAMES\BLUS30966\PS3_GAME'
if (-not (Test-Path -LiteralPath (Join-Path $extractRoot 'USRDIR\LAUNCH\DBZ3.BIN'))) {
    if (-not (Test-Path -LiteralPath (Join-Path $rpcs3Root 'dev_flash'))) {
        & (Join-Path $PSScriptRoot '..\install-rpcs3-firmware.ps1') -FirmwarePath $pupPath -Rpcs3 $rpcs3Exe
    }
    Write-Host 'Install the PKG in the RPCS3 window, then close RPCS3.'
    $process = Start-Process -FilePath $rpcs3Exe -WorkingDirectory $rpcs3Root `
        -ArgumentList @('--installpkg', ('"' + $pkgPath + '"')) -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "RPCS3 PKG installation failed: $($process.ExitCode)" }
    if (-not (Test-Path -LiteralPath (Join-Path $installed 'USRDIR\LAUNCH\DBZ3.BIN'))) {
        $found = Get-ChildItem -LiteralPath (Join-Path $rpcs3Root 'dev_hdd0') `
            -Recurse -File -Filter 'DBZ3.BIN' -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '[\\/]USRDIR[\\/]LAUNCH[\\/]DBZ3\.BIN$' } |
            Select-Object -First 1
        if ($found) { $installed = $found.FullName -replace '[\\/]USRDIR[\\/]LAUNCH[\\/]DBZ3\.BIN$', '' }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $installed 'USRDIR\LAUNCH\DBZ3.BIN'))) {
        throw "DBZ3.BIN not found after RPCS3 installation under $rpcs3Root"
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $extractRoot) -Force | Out-Null
    Copy-Item -LiteralPath $installed -Destination $extractRoot -Recurse
}
$game = Join-Path $root 'userdata\game\BLUS30966\LAUNCH'
New-Item -ItemType Directory -Path $game -Force | Out-Null
$bin = Join-Path $game 'DBZ3.BIN'
$elf = Join-Path $game 'DBZ3.elf'
$expected = 'AB4D04482948F51D7E3D15F7C8183BDD8DDF15957D03BC6571ED6634628B3276'
$elfIsValid = (Test-Path -LiteralPath $elf -PathType Leaf) -and
    (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash -eq $expected
if (-not $elfIsValid) {
    Copy-Item -LiteralPath (Join-Path $extractRoot 'USRDIR\LAUNCH\DBZ3.BIN') -Destination $bin
    # A prior interrupted/failed decrypt may have left a partial or stale ELF.
    Remove-Item -LiteralPath $elf -Force -ErrorAction SilentlyContinue
    $log = Join-Path $rpcs3Root 'log\RPCS3.log'
    if (Test-Path -LiteralPath $log) { Clear-Content -LiteralPath $log }
    $decryptStarted = Get-Date
    Push-Location $rpcs3Root
    try {
        & $rpcs3Exe --decrypt $bin
        $decryptExitCode = $LASTEXITCODE
    } finally { Pop-Location }
    # RPCS3 builds differ in where they place the decrypted output. Accept it
    # only when its content matches the known retail ELF, then normalize its path.
    $searchRoots = @($rpcs3Root, (Split-Path -Parent $bin)) | Select-Object -Unique
    $decrypted = foreach ($searchRoot in $searchRoots) {
        if (Test-Path -LiteralPath $searchRoot -PathType Container) {
            Get-ChildItem -LiteralPath $searchRoot -Recurse -File -ErrorAction SilentlyContinue |
                Where-Object {
                    $_.LastWriteTime -ge $decryptStarted.AddSeconds(-2) -and
                    ($_.Name -match '(?i)^DBZ3.*(\.elf|\.bin|\.decrypted)?$' -or
                     $_.Extension -ieq '.elf')
                }
        }
    }
    $validOutput = $decrypted | Where-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash -eq $expected
    } | Select-Object -First 1
    if ($validOutput -and $validOutput.FullName -ne $elf) {
        Copy-Item -LiteralPath $validOutput.FullName -Destination $elf -Force
    }
    if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) {
        $diagnostic = if (Test-Path -LiteralPath $log) {
            (Get-Content -LiteralPath $log -Tail 30) -join [Environment]::NewLine
        } else { 'RPCS3.log was not created.' }
        throw "RPCS3 did not produce the supported decrypted ELF (exit code $decryptExitCode). Searched for SHA-256 $expected under $($searchRoots -join ', '). Log tail:`n$diagnostic"
    }
}
$actual = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
if ($actual -ne $expected) { throw "Unsupported DBZ3.elf SHA-256: $actual" }
$icon = Join-Path $root 'generated\dbz3\assets\icon.jpg'
if (-not (Test-Path -LiteralPath $icon)) {
    & (Join-Path $PSScriptRoot '..\make-icon.ps1') `
        -InputPng (Join-Path $extractRoot 'ICON0.PNG') -OutputJpeg $icon
}
$python = Join-Path $root '.venv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $python)) { throw 'Run scripts\setup\setup.ps1 first.' }
$sdkTools = Join-Path $root 'third_party\ps3recomp\tools'
$analysis = Join-Path $root 'ports\dbz3\analysis'
$out = Join-Path $root 'generated\dbz3\recompiled'
New-Item -ItemType Directory -Path $out -Force | Out-Null
& $python (Join-Path $sdkTools 'ppu_lifter.py') $elf `
    --functions (Join-Path $analysis 'DBZ3.functions.expanded.json') `
    --hle-stubs (Join-Path $analysis 'DBZ3.imports.json') `
    --code-end 0x2B84C0 --chunk-lines 150000 --jobs $Jobs --output $out
if ($LASTEXITCODE -ne 0) { throw 'PPU lift failed.' }
& $python (Join-Path $sdkTools 'gen_hle_nids.py') --all `
    --out (Join-Path $out 'ppu_hle_nids.cpp')
if ($LASTEXITCODE -ne 0) { throw 'HLE NID generation failed.' }
& $python (Join-Path $PSScriptRoot '..\postprocess-lift.py') $out `
    (Join-Path $root 'generated\dbz3\inc')
if ($LASTEXITCODE -ne 0) { throw 'Lift postprocessing failed.' }
$spu = Join-Path $root 'generated\dbz3\spu'
$images = Join-Path $spu 'images'
New-Item -ItemType Directory -Path $images -Force | Out-Null
& $python (Join-Path $sdkTools 'extract_spu_images.py') $elf --output $images
if ($LASTEXITCODE -ne 0) { throw 'SPU extraction failed.' }
& $python (Join-Path $sdkTools 'build_spu_workloads.py') `
    --images $images --lifted $spu --out (Join-Path $spu 'spu_workloads.c') `
    --register-fn blus30966_spu_register_all --title BLUS30966-DBZ3 --constructor `
    --extra-funcs spu_0001_at_002E2580=0x72D8 --relift
if ($LASTEXITCODE -ne 0) { throw 'SPU lift failed.' }
Write-Host 'DBZ3 import complete. Generated output is ignored by Git.'
