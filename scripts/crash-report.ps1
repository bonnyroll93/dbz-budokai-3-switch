# Summarise the crashes of a play session (build made with -CrashTrail):
#
#   .\scripts\crash-report.ps1                       pull logs (DBI MTP) and summarise new crashes
#   .\scripts\crash-report.ps1 -RunDir build\logs\dbz3\dev\run12   use logs already pulled
#
# For every Atmosphere crash report not summarised before, finds the ELF of
# that build in build\symbols (by the report's Module Id), turns the crashed
# thread's PC, LR and stack trace into function names and source lines, and
# adds the end of the session's crash-trail log. Writes crash-summary.txt in
# the run folder and prints it.
[CmdletBinding()]
param(
    [string]$Game = 'dbz3',
    [string]$RunDir,
    [string]$DeviceName = 'Switch',
    # Summarise every report in the run folder, also those done before.
    [switch]$All,
    [int]$TrailLines = 30,
    [string]$DevkitPro = $(if ($env:DEVKITPRO) { $env:DEVKITPRO } else { 'C:\devkitPro' })
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'switch-games.ps1')
$g = Get-SwitchGame $Game
$addr2line = Join-Path $DevkitPro 'devkitA64\bin\aarch64-none-elf-addr2line.exe'
if (-not (Test-Path -LiteralPath $addr2line)) { throw "addr2line not found: $addr2line" }

if (-not $RunDir) {
    $RunDir = & (Join-Path $PSScriptRoot 'pull-switch-logs.ps1') -Game $Game -DeviceName $DeviceName |
        Select-Object -Last 1
}
$RunDir = (Resolve-Path -LiteralPath $RunDir).Path
$symbolsRoot = Get-SwitchSymbolsRoot $Game
New-Item -ItemType Directory -Force -Path $symbolsRoot | Out-Null
$seenFile = Join-Path $symbolsRoot 'reported.txt'
$seen = @{}
if (Test-Path -LiteralPath $seenFile) { foreach ($n in Get-Content -LiteralPath $seenFile) { $seen[$n] = 1 } }

$reports = @(Get-ChildItem -LiteralPath $RunDir -Filter "*_$($g.TitleId.ToLowerInvariant()).log" |
    Sort-Object Name | Where-Object { $All -or -not $seen.ContainsKey($_.Name) })
$trails = @(Get-ChildItem -LiteralPath $RunDir -Filter 'crash-trail-*.log' | ForEach-Object {
    if ($_.Name -match '^crash-trail-(\d+)\.log$') {
        [pscustomobject]@{ Start = [int64]$Matches[1]; File = $_ }
    }
} | Sort-Object Start)

$out = New-Object System.Collections.Generic.List[string]
$out.Add("Crash summary for $RunDir ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))")
$out.Add('')
if (-not $reports.Count) { $out.Add('No new crash reports.') }

function Find-Symbols([string]$ModuleId) {
    foreach ($dir in Get-ChildItem -LiteralPath $symbolsRoot -Directory) {
        if ($ModuleId.StartsWith($dir.Name)) {
            $elf = Join-Path $dir.FullName 'dbz-budokai-3-switch.elf'
            if (Test-Path -LiteralPath $elf) { return $elf }
        }
    }
    $null
}

foreach ($report in $reports) {
    $lines = Get-Content -LiteralPath $report.FullName
    $crashUnix = if ($report.Name -match '^(\d+)_') { [int64]$Matches[1] } else { 0 }
    $when = if ($crashUnix) {
        [DateTimeOffset]::FromUnixTimeSeconds($crashUnix).LocalDateTime.ToString('yyyy-MM-dd HH:mm:ss')
    } else { '?' }
    $out.Add("=== $($report.Name)  ($when)")

    $result = ($lines | Where-Object { $_ -match '^Result:' } | Select-Object -First 1)
    if ($result) { $out.Add($result.Trim()) }
    # Exception type and fault address: the indented lines after "Exception Info:".
    $i = [array]::IndexOf($lines, ($lines | Where-Object { $_ -match '^Exception Info:' } | Select-Object -First 1))
    if ($i -ge 0) {
        for ($j = $i + 1; $j -lt $lines.Count -and $lines[$j] -match '^\s+\S'; $j++) { $out.Add($lines[$j].TrimEnd()) }
    }

    $moduleLine = $lines | Where-Object { $_ -match 'Module Id:\s*([0-9A-Fa-f]+)' } | Select-Object -First 1
    $moduleId = if ($moduleLine -and $moduleLine -match 'Module Id:\s*([0-9A-Fa-f]+)') { $Matches[1].ToLowerInvariant() } else { '' }
    $elf = if ($moduleId) { Find-Symbols $moduleId } else { $null }

    # Crashed thread: from "Crashed Thread Info:" to the next top-level section.
    $start = [array]::IndexOf($lines, ($lines | Where-Object { $_ -match '^Crashed Thread Info:' } | Select-Object -First 1))
    $frames = New-Object System.Collections.Generic.List[object]
    if ($start -ge 0) {
        for ($j = $start + 1; $j -lt $lines.Count -and $lines[$j] -notmatch '^\S'; $j++) {
            if ($lines[$j] -match '^\s+(PC|LR|ReturnAddress\[\d+\]):\s+\S+ \(dbz-budokai-3-switch \+ (0x[0-9a-fA-F]+)\)') {
                $frames.Add([pscustomobject]@{ Label = $Matches[1]; Offset = $Matches[2] })
            }
        }
    }

    if (-not $elf) {
        $out.Add("No ELF for this build (Module Id $($moduleId.Substring(0, [Math]::Min(16, $moduleId.Length)))...): built without -CrashTrail or older than the last 10 builds.")
    } elseif ($frames.Count) {
        $out.Add("Build: $(Split-Path -Leaf (Split-Path -Parent $elf))")
        $prefix = ((Get-SwitchProject) -replace '\\', '/') + '/'
        $resolved = & $addr2line -a -f -C -i -p -e $elf ($frames | ForEach-Object { $_.Offset }) |
            ForEach-Object { $_.Replace($prefix, '') }
        # -a -p: each address starts a line "0x...: func at file:line", inlined
        # callers follow on " (inlined by) ..." lines.
        $k = -1
        foreach ($r in $resolved) {
            if ($r -match '^0x[0-9a-fA-F]+:\s*(.*)$') {
                $k++
                $label = if ($k -lt $frames.Count) { $frames[$k].Label } else { '?' }
                $out.Add(('  {0,-17} {1}' -f $label, $Matches[1]))
            } else {
                $out.Add(('  {0,-17} {1}' -f '', $r.Trim()))
            }
        }
    } else {
        $out.Add('No crashed thread frames found in the report.')
    }

    $trail = $trails | Where-Object { $_.Start -le $crashUnix } | Select-Object -Last 1
    if ($trail) {
        $t = Get-Content -LiteralPath $trail.File.FullName
        $out.Add("Session log $($trail.File.Name) (last $TrailLines lines):")
        $t | Select-Object -First 1 | ForEach-Object { $out.Add("  $_") }
        $t | Select-Object -Skip 1 | Select-Object -Last $TrailLines | ForEach-Object { $out.Add("  $_") }
    } else {
        $out.Add('No session log from before this crash in the run folder.')
    }
    $out.Add('')
    if (-not $seen.ContainsKey($report.Name)) {
        Add-Content -LiteralPath $seenFile -Value $report.Name -Encoding utf8
        $seen[$report.Name] = 1
    }
}

$summary = Join-Path $RunDir 'crash-summary.txt'
$out | Set-Content -LiteralPath $summary -Encoding utf8
$out | ForEach-Object { Write-Host $_ }
Write-Host "Written to $summary"
