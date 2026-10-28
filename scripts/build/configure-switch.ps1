[CmdletBinding()]
param(
    [string]$DevkitPro = $(if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' }),
    [switch]$ProfilePpuFunctions,
    [switch]$LevelFunctionProfile,
    [switch]$InputQueueProfile
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\switch-games.ps1')
. (Join-Path $PSScriptRoot 'switch-build-config.ps1')
$project = Get-SwitchProject
$DevkitPro = (Resolve-Path -LiteralPath $DevkitPro -ErrorAction Stop).Path
$bash = Join-Path $DevkitPro 'msys2\usr\bin\bash.exe'
$toolchain = Join-Path $DevkitPro 'cmake\Switch.cmake'
foreach ($required in @($bash, $toolchain,
    (Join-Path $project 'generated\dbz3\recompiled\ppu_recomp.h'),
    (Join-Path $project 'third_party\ps3recomp\CMakeLists.txt'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing: $required" }
}
$devkitMsys = ($DevkitPro -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
$devkitMsys = $devkitMsys.Substring(0, 2).ToLowerInvariant() + $devkitMsys.Substring(2)
$projectMsys = Get-SwitchMsysPath
$env:DEVKITPRO = $DevkitPro.Replace('\', '/')
$env:DEVKITA64 = (Join-Path $DevkitPro 'devkitA64').Replace('\', '/')
$profileFlag = if ($ProfilePpuFunctions -or $SwitchBuild.ProfilePpuFunctions) { 'ON' } else { 'OFF' }
$levelFlag = if ($LevelFunctionProfile -or $SwitchBuild.LevelFunctionProfile) { 'ON' } else { 'OFF' }
$inputQueueFlag = if ($InputQueueProfile -or $SwitchBuild.InputQueueProfile) { 'ON' } else { 'OFF' }
$spuProfileFlag = if ($SwitchBuild.ProfileSpuFunctions) { 'ON' } else { 'OFF' }
$dekoDiagFlag = if ($SwitchBuild.DekoDiagnostics) { 'ON' } else { 'OFF' }
$loopMetricsFlag = if ($SwitchBuild.LoopMetrics) { 'ON' } else { 'OFF' }
$frameMetricsFlag = if ($SwitchBuild.FrameClockMetrics) { 'ON' } else { 'OFF' }
$shaderCaptureFlag = if ($SwitchBuild.ShaderCapture) { 'ON' } else { 'OFF' }
$screenshotCaptureFlag = if ($SwitchBuild.ScreenshotCapture) { 'ON' } else { 'OFF' }
$logic60Flag = if ($SwitchBuild.Logic60) { 'ON' } else { 'OFF' }
$lightFlag = if ($SwitchBuild.Lightweight) { 'ON' } else { 'OFF' }
& $bash -lc "cd '$projectMsys' && cmake -S ports/dbz3 -B build/cmake/switch-dbz3 -G Ninja -DCMAKE_TOOLCHAIN_FILE='$devkitMsys/cmake/Switch.cmake' -DCMAKE_BUILD_TYPE=Release -DPS3RECOMP_DIR='$projectMsys/third_party/ps3recomp' -DRECOMP_DIR='$projectMsys/generated/dbz3/recompiled' -DSPU_DIR='$projectMsys/generated/dbz3/spu' -DPORT_SWITCH_PPU_FUNC_PROFILE=$profileFlag -DPORT_DBZ3_LEVEL_FUNCTION_PROFILE=$levelFlag -DPORT_SWITCH_INPUT_QUEUE_PROFILE=$inputQueueFlag -DPORT_SWITCH_SPU_PROFILE=$spuProfileFlag -DPORT_DBZ3_DEKO_DIAG=$dekoDiagFlag -DPORT_DBZ3_LOOP_METRICS=$loopMetricsFlag -DPORT_DBZ3_FRAME_CLOCK_METRICS=$frameMetricsFlag -DPORT_DBZ3_SHADER_CAPTURE=$shaderCaptureFlag -DPORT_DBZ3_SCREENSHOT_CAPTURE=$screenshotCaptureFlag -DPORT_DBZ3_LOGIC60=$logic60Flag -DPORT_DBZ3_LIGHT_BUILD=$lightFlag -DPORT_GAME_ELF=sdmc:/switch/dbz-budokai-3-switch/game/PS3_GAME/USRDIR/LAUNCH/DBZ3.elf"
if ($LASTEXITCODE -ne 0) { throw 'Switch CMake configuration failed.' }
Write-Host 'Switch DBZ3 build configured.'
