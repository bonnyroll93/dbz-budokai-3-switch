# Shared configuration for the build/release scripts (dot-source this file).
#
# Layout under build\:
#   cmake\switch-<game>                     CMake/ninja build tree
#   releases\<game>\v<X.Y.Z>-<feature>\    one folder per version+feature:
#       <NspBase>-v<X.Y.Z>.nsp             the installable package
#       build-info.txt                     when and from what it was built
#   logs\<game>\v<X.Y.Z>-<feature>\runNN\  logs pulled from the console
#   dev\<game>\<NspBase>-dev.nsp           the build under test, overwritten
#                                          by every switch-dev.ps1 run
#   logs\<game>\dev\runNN\                 logs of dev runs; switch-publish.ps1
#                                          moves them to the release's folder
#   staging\<game>\                        packaging scratch, wiped every time
#   tools\                                 packer sources and binaries
#
# Versions are MAJOR.MINOR.PATCH: MAJOR = feature milestone (0 until the game
# is playable at 30 FPS in gameplay, which is 1.0.0), MINOR = a new feature or
# optimisation, PATCH = a fix to the same feature. Re-running a release with
# the same version and feature overwrites that folder instead of creating a
# new one.

$SwitchGames = @{
    dbz3 = @{
        Tree    = 'switch-dbz3'
        TitleId = '0100BDBA30968000'
        AppName = 'Dragonball Z Budokai 3'
        NspBase = 'Dragonball-Z-Budokai-3'
        Icon    = 'generated\dbz3\assets\icon.jpg'
        Port    = 'generated\dbz3'
        Elf     = 'userdata\game\BLUS30966\LAUNCH\DBZ3.elf'
        SdElf   = 'sdmc:/switch/dbz-budokai-3-switch/game/PS3_GAME/USRDIR/LAUNCH/DBZ3.elf'
    }
}

function Get-SwitchProject {
    (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-SwitchGame([string]$Game) {
    if (-not $SwitchGames.ContainsKey($Game)) {
        throw "Unknown game '$Game'. Valid values: $($SwitchGames.Keys -join ', ')"
    }
    $SwitchGames[$Game]
}

function Test-SwitchVersion([string]$Version) {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') {
        throw "Invalid version '$Version': use MAJOR.MINOR.PATCH (for example, 0.15.0)"
    }
}

# 'v0.14.0-fpalpha' from '0.14.0' and 'FP alpha' (feature: a-z 0-9 and dashes).
function Get-SwitchTag([string]$Version, [string]$Feature) {
    Test-SwitchVersion $Version
    $slug = ($Feature.ToLowerInvariant() -replace '[^a-z0-9]+', '-').Trim('-')
    if (-not $slug) { throw 'A feature name is required (for example, -Feature fpalpha)' }
    'v{0}-{1}' -f $Version, $slug
}

function Get-SwitchReleaseDir([string]$Game, [string]$Tag) {
    Join-Path (Get-SwitchProject) "build\releases\$Game\$Tag"
}

function Get-SwitchNspPath([string]$Game, [string]$Version, [string]$Tag) {
    $g = Get-SwitchGame $Game
    Join-Path (Get-SwitchReleaseDir $Game $Tag) ("{0}-v{1}.nsp" -f $g.NspBase, $Version)
}

# Development build: one per game, overwritten by every switch-dev.ps1 run.
function Get-SwitchDevDir([string]$Game) {
    Join-Path (Get-SwitchProject) "build\dev\$Game"
}

function Get-SwitchDevNsp([string]$Game) {
    $g = Get-SwitchGame $Game
    Join-Path (Get-SwitchDevDir $Game) ("{0}-dev.nsp" -f $g.NspBase)
}

function Get-SwitchElf([string]$Game) {
    $g = Get-SwitchGame $Game
    Join-Path (Get-SwitchProject) "build\cmake\$($g.Tree)\dbz-budokai-3-switch.elf"
}

# The dev SD deployment, DBI MTP deployment, and full NSP must contain the
# same game files. DBZ1.BIN is not used by this DBZ3 port; the decrypted ELF is
# kept separately from the RPCS3 extraction and replaces the encrypted BIN.
function Get-SwitchGameDataFiles([string]$Game) {
    $g = Get-SwitchGame $Game
    $project = Get-SwitchProject
    $source = Join-Path $project 'userdata\Extracted\dev_hdd0\GAMES\BLUS30966\PS3_GAME'
    $elf = Join-Path $project $g.Elf
    $usrdir = Join-Path $source 'USRDIR'
    $dbz3Dir = Join-Path $usrdir 'DBZ3'
    $launchDir = Join-Path $usrdir 'LAUNCH'
    if (-not (Test-Path -LiteralPath $dbz3Dir -PathType Container) -or
        -not (Test-Path -LiteralPath $launchDir -PathType Container) -or
        -not (Test-Path -LiteralPath $elf -PathType Leaf)) {
        throw 'Game data is missing; run scripts/setup/import-game.ps1 first.'
    }

    # Keep the small set needed by the DBZ3 runtime: the DBZ3 data tree, the
    # launch files, and PS3_GAME files outside USRDIR. Do not copy DBZ1 assets.
    $files = @(Get-ChildItem -LiteralPath $source -File -Recurse | Where-Object {
        $relative = $_.FullName.Substring($source.Length).TrimStart('\')
        -not $relative.StartsWith('USRDIR\', [StringComparison]::OrdinalIgnoreCase)
    } | ForEach-Object {
        $relative = $_.FullName.Substring($source.Length).TrimStart('\')
        [pscustomobject]@{ Source = $_.FullName; Relative = $relative; Length = $_.Length }
    })
    $files += Get-ChildItem -LiteralPath $dbz3Dir -File -Recurse | ForEach-Object {
        [pscustomobject]@{
            Source = $_.FullName
            Relative = $_.FullName.Substring($source.Length).TrimStart('\')
            Length = $_.Length
        }
    }
    $files += Get-ChildItem -LiteralPath $launchDir -File | Where-Object {
        $_.Name -ine 'DBZ1.BIN' -and $_.Name -ine 'DBZ3.elf'
    } | ForEach-Object {
        [pscustomobject]@{
            Source = $_.FullName
            Relative = $_.FullName.Substring($source.Length).TrimStart('\')
            Length = $_.Length
        }
    }
    $elfFile = Get-Item -LiteralPath $elf
    $files += [pscustomobject]@{
        Source = $elfFile.FullName
        Relative = 'USRDIR\LAUNCH\DBZ3.elf'
        Length = $elfFile.Length
    }
    $files | Sort-Object Relative
}

# key=value lines of a build-info.txt as a hashtable (empty if missing).
function Read-SwitchBuildInfo([string]$Path) {
    $info = @{}
    if (Test-Path -LiteralPath $Path) {
        foreach ($line in Get-Content -LiteralPath $Path) {
            $i = $line.IndexOf('=')
            if ($i -gt 0) { $info[$line.Substring(0, $i)] = $line.Substring($i + 1) }
        }
    }
    $info
}

# The MSYS2 path of the project, for bash -lc commands.
function Get-SwitchMsysPath {
    $p = ((Get-SwitchProject) -replace '\\', '/') -replace '^([A-Za-z]):', '/$1'
    $p = $p.Substring(0, 2).ToLowerInvariant() + $p.Substring(2)
    if ($p.Contains("'")) { throw "Project paths containing an apostrophe are unsupported: $p" }
    $p
}
