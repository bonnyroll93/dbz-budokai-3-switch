[CmdletBinding()]
param(
    # DBZ3 is the supported Switch game.
    [Parameter(Mandatory)][string]$Game,
    # Where the package goes (overwritten) and what it reports: the dev and
    # publish scripts decide both. build-info.txt is written beside it.
    [Parameter(Mandatory)][string]$OutputNsp,
    [Parameter(Mandatory)][string]$DisplayVersion,
    [switch]$Full,
    [string]$Feature = '',
    [string]$DevkitPro = $(if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' }),
    [string]$HacBrewPack,
    [string]$Keyset = (Join-Path $env:USERPROFILE '.switch\prod.keys')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\switch-games.ps1')
$project = Get-SwitchProject
$g = Get-SwitchGame $Game
$elf = Join-Path $project "build\cmake\$($g.Tree)\dbz-budokai-3-switch.elf"
$destination = [IO.Path]::GetFullPath($OutputNsp)
$releaseDir = Split-Path -Parent $destination
$titleId = $g.TitleId
$appName = $g.AppName

if (-not $HacBrewPack) {
    $HacBrewPack = Join-Path $project 'build\tools\nton-src\nton\bin\hacbrewpack.exe'
}
$packer = (Resolve-Path -LiteralPath $HacBrewPack -ErrorAction Stop).Path
$keys = (Resolve-Path -LiteralPath $Keyset -ErrorAction Stop).Path
if ($keys.StartsWith($project + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Keep the console keyset outside the project directory'
}
$elf2nso = Join-Path $DevkitPro 'tools\bin\elf2nso.exe'
$npdmtool = Join-Path $DevkitPro 'tools\bin\npdmtool.exe'
$nacptool = Join-Path $DevkitPro 'tools\bin\nacptool.exe'
$config = Join-Path $project 'ports\dbz3\runner.json'
$icon = Join-Path $project $g.Icon
foreach ($required in @($elf, $elf2nso, $npdmtool, $nacptool, $config, $icon)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing input: $required"
    }
}

# One scratch folder per game, emptied before and removed after.
$stage = Join-Path $project "build\staging\$Game"
$stage = [IO.Path]::GetFullPath($stage)
$stagingRoot = [IO.Path]::GetFullPath((Join-Path $project 'build\staging'))
if (-not $stage.StartsWith($stagingRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging directory: $stage"
}
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
$exefs = Join-Path $stage 'exefs'
$control = Join-Path $stage 'control'
$ncas = Join-Path $stage 'nca'
$package = Join-Path $stage 'package'
$temporary = Join-Path $stage 'temporary'
$backup = Join-Path $stage 'backup'
foreach ($directory in @($exefs, $control, $ncas, $package, $temporary, $backup)) {
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
}
$romfs = Join-Path $stage 'romfs'
if ($Full) {
    if ($Game -ne 'dbz3') { throw 'Full packaging currently supports DBZ3 only.' }
    $gameFiles = @(Get-SwitchGameDataFiles $Game)
    $gameStage = Join-Path $romfs 'game\PS3_GAME'
    foreach ($gameFile in $gameFiles) {
        $gameDestination = Join-Path $gameStage $gameFile.Relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $gameDestination) -Force | Out-Null
        Copy-Item -LiteralPath $gameFile.Source -Destination $gameDestination
    }
}

$json = [IO.File]::ReadAllText($config)
$json = $json.Replace('BudokaiProbe', 'BudokaiRunner').Replace(
    '0100BDBA30966000', $titleId)
$svcNeedle = '"svcGetInfo": "0x29",'
if (-not $json.Contains($svcNeedle)) {
    throw 'Probe NPDM config has no svcGetInfo entry; cannot add exception-return SVC'
}
$json = $json.Replace($svcNeedle,
    '"svcReturnFromException": "0x28",' + [Environment]::NewLine +
    '      "svcGetInfo": "0x29",')
# T22: kernel bookkeeping for the 4 GiB guest mapping plus the deko3d GPU
# mappings outgrew 16 MiB; give the runner 64 MiB.
$sysResNeedle = '"system_resource_size": "0x01000000"'
if (-not $json.Contains($sysResNeedle)) {
    throw 'Probe NPDM config has no 16 MiB system_resource_size entry'
}
$json = $json.Replace($sysResNeedle, '"system_resource_size": "0x04000000"')
$configOut = Join-Path $stage 'runner.json'
[IO.File]::WriteAllText($configOut, $json, [Text.UTF8Encoding]::new($false))
& $elf2nso $elf (Join-Path $exefs 'main')
if ($LASTEXITCODE -ne 0) { throw 'elf2nso failed' }
& $npdmtool $configOut (Join-Path $exefs 'main.npdm')
if ($LASTEXITCODE -ne 0) { throw 'npdmtool failed' }
$npdm = [IO.File]::ReadAllBytes((Join-Path $exefs 'main.npdm'))
if ($npdm.Length -lt 0x18 -or [Text.Encoding]::ASCII.GetString($npdm, 0, 4) -ne 'META' -or
        [BitConverter]::ToUInt32($npdm, 0x14) -ne 0x04000000) {
    throw 'Runner NPDM is missing its 64 MiB SystemResource reservation'
}
Copy-Item -LiteralPath $icon -Destination (Join-Path $control 'icon_AmericanEnglish.dat')
& $nacptool --create $appName 'dbz-budokai-3-switch' $DisplayVersion `
    (Join-Path $control 'control.nacp') "--titleid=$titleId"
if ($LASTEXITCODE -ne 0) { throw 'nacptool failed' }
$packerArgs = @('-k', $keys, '--titleid', $titleId, '--exefsdir', $exefs,
    '--controldir', $control, '--nspdir', $package, '--ncadir', $ncas,
    '--tempdir', $temporary, '--backupdir', $backup, '--nologo', '--keepncadir')
if ($Full) { $packerArgs += @('--romfsdir', $romfs) } else { $packerArgs += '--noromfs' }
& $packer @packerArgs
if ($LASTEXITCODE -ne 0) { throw "hacBrewPack failed (exit $LASTEXITCODE)" }
$ncaFiles = @(Get-ChildItem -LiteralPath $ncas -Filter '*.nca' -File)
if ($ncaFiles.Count -lt 3) { throw "Expected program, control and meta NCAs; found $($ncaFiles.Count)" }
$packages = @(Get-ChildItem -LiteralPath $package -Filter '*.nsp' -File)
if ($packages.Count -ne 1) { throw "Expected one installable NSP, found $($packages.Count)" }

New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null
Copy-Item -LiteralPath $packages[0].FullName -Destination $destination -Force
if (-not (Test-Path -LiteralPath $destination -PathType Leaf) -or
    (Get-Item -LiteralPath $destination).Length -ne $packages[0].Length) {
    throw "Packaged NSP was not copied correctly to $destination"
}
$elfItem = Get-Item -LiteralPath $elf
$hash = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash
@(
    "game=$Game"
    "version=$DisplayVersion"
    "feature=$Feature"
    "title_id=$titleId"
    "app_name=$appName"
    "packaged=$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))"
    "elf=$elf"
    "elf_modified=$($elfItem.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))"
    "elf_sha256=$hash"
) | Set-Content -LiteralPath (Join-Path $releaseDir 'build-info.txt') -Encoding utf8
Remove-Item -LiteralPath $stage -Recurse -Force
Write-Host "NSP: $destination"
