# Edit these values before running scripts/build/switch-dev.ps1.
# $true enables a feature; $false disables it. Lightweight removes the
# high-frequency profilers; ShaderCapture and ScreenshotCapture are separate.
# Logic60 keeps the gameplay timing used by the playable build.
$SwitchBuild = @{
    Lightweight             = $true
    ProfilePpuFunctions     = $false
    ProfileSpuFunctions     = $false
    LevelFunctionProfile    = $false
    InputQueueProfile       = $false
    # Detailed texture/draw logs; unrelated to saving missing shaders.
    DekoDiagnostics         = $false
    LoopMetrics             = $false
    FrameClockMetrics       = $false
    # Keep shader dumps and capture code out of the FPS test build.
    ShaderCapture           = $false
    ScreenshotCapture       = $false
    Logic60                 = $true
}
