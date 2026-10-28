# Import still images from DBI MTP into userdata and convert them to PNG.
# Run while DBI > Run MTP responder is active:
#   .\scripts\import-switch-images.ps1
#   .\scripts\import-switch-images.ps1 -IncludeRecordings
[CmdletBinding()]
param(
    [string]$DeviceName = 'Switch',
    [string]$Destination,
    [switch]$SkipAlbum,
    [switch]$IncludeRecordings,
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -lt 1) { throw 'TimeoutSeconds must be positive.' }

$project = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Destination) { $Destination = Join-Path $project 'userdata\screenshoot\switch-dbi-import' }
$Destination = [System.IO.Path]::GetFullPath($Destination)
$rawRoot = Join-Path $Destination 'raw'
$pngRoot = Join-Path $Destination 'png'
New-Item -ItemType Directory -Force -Path $rawRoot, $pngRoot | Out-Null

$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
if (-not $ffmpeg) { throw 'ffmpeg not found in PATH. Install ffmpeg to convert PPM/JPEG/MJPEG images to PNG.' }

$shell = New-Object -ComObject Shell.Application
$device = $shell.NameSpace(17).Items() | Where-Object { $_.Name -like "*$DeviceName*" } | Select-Object -First 1
if (-not $device) { throw 'Switch not found. Start DBI > Run MTP responder and connect the USB cable.' }

function Get-MtpChild($parent, [string]$name) {
    if (-not $parent) { return $null }
    $parent.GetFolder.Items() | Where-Object { $_.Name -eq $name } | Select-Object -First 1
}

$sd = $device.GetFolder.Items() | Where-Object { $_.Name -like '*SD Card' -or $_.Name -like '1: SD*' } | Select-Object -First 1
if (-not $sd) { throw 'SD Card storage not found on the Switch.' }
$app = Get-MtpChild (Get-MtpChild $sd 'switch') 'dbz-budokai-3-switch'
$album = Get-MtpChild (Get-MtpChild $sd 'Nintendo') 'Album'

$sources = @()
if ($app) {
    foreach ($name in @('captures', 'draw-captures')) {
        $folder = Get-MtpChild $app $name
        if ($folder) { $sources += @{ Item = $folder; Relative = "game\$name" } }
    }
}
if (-not $SkipAlbum -and $album) { $sources += @{ Item = $album; Relative = 'album' } }
if ($sources.Count -eq 0) { throw 'No game captures, draw captures, or Nintendo Album folder found on the SD card.' }

$counts = @{ Copied = 0; Converted = 0; Skipped = 0; Failed = 0 }
$errors = New-Object System.Collections.Generic.List[string]
$imageExtensions = @('.ppm', '.jpg', '.jpeg', '.png', '.bmp', '.webp')

function Copy-MtpFile($item, [string]$relative) {
    $target = Join-Path $rawRoot $relative
    $parent = Split-Path -Parent $target
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
    $expectedValue = $item.ExtendedProperty('System.Size')
    $expected = if ($null -ne $expectedValue) { [long]$expectedValue } else { -1L }
    if ((Test-Path -LiteralPath $target) -and $expected -ge 0 -and (Get-Item -LiteralPath $target).Length -eq $expected) {
        $counts.Skipped++
        return $target
    }
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
    Write-Host "Copy: $relative"
    $shell.NameSpace($parent).CopyHere($item, 16)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $lastSize = -1L
    $stable = 0
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 300
        if (-not (Test-Path -LiteralPath $target)) { continue }
        $size = (Get-Item -LiteralPath $target).Length
        if ($expected -ge 0) {
            if ($size -eq $expected) { $stable++ } else { $stable = 0 }
        } else {
            if ($size -gt 0 -and $size -eq $lastSize) { $stable++ } else { $stable = 0 }
        }
        if ($stable -ge 3) { $counts.Copied++; return $target }
        $lastSize = $size
    }
    throw "MTP copy timed out or was incomplete: $relative (expected $expected bytes)."
}

function Convert-ToPng([string]$source, [string]$relative) {
    $extension = [System.IO.Path]::GetExtension($relative).ToLowerInvariant()
    if ($extension -eq '.mjpg') {
        $frameDir = Join-Path $pngRoot ([System.IO.Path]::ChangeExtension($relative, $null))
        New-Item -ItemType Directory -Force -Path $frameDir | Out-Null
        $firstFrame = Join-Path $frameDir 'frame_00001.png'
        if (Test-Path -LiteralPath $firstFrame) { $counts.Skipped++; return }
        $pattern = Join-Path $frameDir 'frame_%05d.png'
        & $ffmpeg.Source -hide_banner -loglevel error -f mjpeg -i $source -vsync 0 $pattern
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $firstFrame)) { throw "ffmpeg failed to extract frames: $relative" }
        $counts.Converted++
        return
    }

    $pngRelative = [System.IO.Path]::ChangeExtension($relative, '.png')
    $target = Join-Path $pngRoot $pngRelative
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
    if ((Test-Path -LiteralPath $target) -and (Get-Item -LiteralPath $target).Length -gt 0 -and
        (Get-Item -LiteralPath $target).LastWriteTimeUtc -ge (Get-Item -LiteralPath $source).LastWriteTimeUtc) {
        $counts.Skipped++
        return
    }
    $temporary = "$target.importing"
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    if ($extension -eq '.png') {
        Copy-Item -LiteralPath $source -Destination $temporary
    } else {
        & $ffmpeg.Source -hide_banner -loglevel error -y -i $source -frames:v 1 -f image2 -vcodec png $temporary
        if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed to convert: $relative" }
    }
    if (-not (Test-Path -LiteralPath $temporary) -or (Get-Item -LiteralPath $temporary).Length -eq 0) {
        throw "PNG output is missing or empty: $relative"
    }
    Move-Item -LiteralPath $temporary -Destination $target -Force
    $counts.Converted++
    Write-Host "PNG:  $pngRelative"
}

function Import-MtpFolder($folder, [string]$relativeDirectory) {
    foreach ($item in $folder.GetFolder.Items()) {
        $relative = Join-Path $relativeDirectory $item.Name
        if ($item.IsFolder) {
            Import-MtpFolder $item $relative
            continue
        }
        $extension = [System.IO.Path]::GetExtension($item.Name).ToLowerInvariant()
        $isImage = $extension -in $imageExtensions
        $isRecording = $IncludeRecordings -and $extension -eq '.mjpg'
        $isMetadata = $extension -eq '.txt' -and $relativeDirectory -like 'game\*'
        $isRawTexture = $extension -eq '.bin' -and $relativeDirectory -like 'game\draw-captures\*'
        if (-not ($isImage -or $isRecording -or $isMetadata -or $isRawTexture)) { continue }
        try {
            $source = Copy-MtpFile $item $relative
            if ($isImage -or $isRecording) { Convert-ToPng $source $relative }
        } catch {
            $counts.Failed++
            $errors.Add("$relative`: $($_.Exception.Message)")
            Write-Warning $errors[$errors.Count - 1]
        }
    }
}

foreach ($source in $sources) { Import-MtpFolder $source.Item $source.Relative }

# DBI may initially expose only part of a freshly written folder. The capture
# log records how many checkpoints the Switch produced, so use it to verify the
# MTP listing and retry before reporting a successful import.
if ($app) {
    $drawRoot = Get-MtpChild $app 'draw-captures'
    if ($drawRoot) {
        foreach ($frame in $drawRoot.GetFolder.Items()) {
            if (-not $frame.IsFolder -or $frame.Name -notlike 'frame_*') { continue }
            $relative = Join-Path 'game\draw-captures' $frame.Name
            $log = Join-Path (Join-Path $rawRoot $relative) 'draws.txt'
            if (-not (Test-Path -LiteralPath $log)) { continue }
            $end = Get-Content -LiteralPath $log -Tail 1
            if ($end -notmatch '; (\d+) checkpoints;') { continue }
            $expected = [int]$Matches[1]
            $framePng = Join-Path $pngRoot $relative
            for ($attempt = 0; $attempt -lt 6; $attempt++) {
                $actual = @(Get-ChildItem -LiteralPath $framePng -Filter 'draw*_t0.png' -ErrorAction SilentlyContinue).Count
                if ($actual -ge $expected -and (Test-Path -LiteralPath (Join-Path $framePng 'final.png'))) { break }
                Start-Sleep -Seconds 2
                Import-MtpFolder $frame $relative
            }
            $actual = @(Get-ChildItem -LiteralPath $framePng -Filter 'draw*_t0.png' -ErrorAction SilentlyContinue).Count
            if ($actual -lt $expected -or -not (Test-Path -LiteralPath (Join-Path $framePng 'final.png'))) {
                $counts.Failed++
                $message = "Incomplete DBI listing for $relative`: $actual of $expected checkpoints; rerun the import when MTP finishes refreshing."
                $errors.Add($message)
                Write-Warning $message
            }
        }
    }
}

Write-Host "PNG folder: $pngRoot"
Write-Host "Originals and metadata: $rawRoot"
Write-Host "Copied: $($counts.Copied); converted: $($counts.Converted); already present: $($counts.Skipped); failed: $($counts.Failed)"
if ($counts.Failed) { throw "$($counts.Failed) files could not be imported. See warnings above." }
