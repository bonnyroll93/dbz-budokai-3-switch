# Edit these values before running scripts/build/switch-dev.ps1.
# $true enables a feature; $false disables it. The lightweight preset keeps
# gameplay timing (Logic60) and native optimizations, while removing captures
# and the high-frequency profilers from the Switch build.
$SwitchBuild = @{
    Lightweight             = $true
    ProfilePpuFunctions     = $false
    ProfileSpuFunctions     = $false
    LevelFunctionProfile    = $false
    InputQueueProfile       = $false
    DekoDiagnostics         = $false
    LoopMetrics             = $false
    FrameClockMetrics       = $false
    ShaderCapture           = $false
    ScreenshotCapture       = $false
    Logic60                 = $true
}
