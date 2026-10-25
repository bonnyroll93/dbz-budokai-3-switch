[CmdletBinding()]
param([string]$Root)

$ErrorActionPreference = 'Stop'
if (-not $Root) { $Root = Join-Path $PSScriptRoot '..' }
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$hasGit = Test-Path -LiteralPath (Join-Path $rootPath '.git')
$ignored = @('userdata','generated','local-archive','third_party','tools','build',
    '.venv','.tmp','.git','ps3recomp','Switch','screenshoot')
$badExtensions = @('.pkg','.pup','.elf','.self','.bin','.nsp','.nro','.nca',
    '.png','.jpg','.jpeg','.hlsl','.glsl','.exe','.dll','.7z','.zip',
    '.afs','.sfd','.sfo','.trp','.adx','.sprx','.psarc','.dat','.keys')
$knownGameHashes = @(
    'AB4D04482948F51D7E3D15F7C8183BDD8DDF15957D03BC6571ED6634628B3276',
    '4C9186B97CD647F3EA2761505DFACFE827580F4F16E755284E0F7B353EDC181B'
)
$findings = New-Object 'System.Collections.Generic.List[string]'
$files = New-Object 'System.Collections.Generic.List[System.IO.FileInfo]'
function Scan-Directory([string]$directory) {
    foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
        if ($entry.PSIsContainer) {
            if ($ignored -notcontains $entry.Name) { Scan-Directory $entry.FullName }
        } else {
            $files.Add($entry)
        }
    }
}
Scan-Directory $rootPath
if ($hasGit) {
    $staged = & git -C $rootPath diff --cached --name-only --diff-filter=ACMR
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect staged files.' }
    foreach ($path in $staged) {
        $parts = $path -split '/'
        if ($parts | Where-Object { $ignored -contains $_ }) {
            $findings.Add("Ignored content staged: $path")
        }
    }
}
foreach ($file in $files) {
    $relative = $file.FullName.Substring($rootPath.Length + 1)
    if ($relative -match '^shaders[\\/]deko3d[\\/]glsl[\\/]' -or
        $relative -match '^shaders[\\/]deko3d[\\/]hlsl[\\/]diagnostic[\\/]' -or
        $relative -match '^shaders[\\/]deko3d[\\/]rsx_deko3d_shaders\.h$') { continue }
    if ($relative -match '^shaders[\\/]deko3d[\\/]hlsl[\\/](override[\\/])?(vs|ps)_[0-9a-f]{16}\.hlsl$') { continue }
    if ($hasGit) {
        & git -C $rootPath check-ignore -q -- $relative 2>$null
        if ($LASTEXITCODE -eq 0) { continue }
    }
    $extension = $file.Extension.ToLowerInvariant()
    if ($badExtensions -contains $extension -or
        $relative -match '(^|[\\/])(assets|recompiled|images)([\\/])' -or
        $relative -match '\.(image|loader)\.json$' -or
        $relative -match '_lifted\.inc$') {
        $findings.Add("Game or generated file: $relative")
        continue
    }
    if ($file.Length -gt 10MB) { $findings.Add("Large file: $relative ($($file.Length) bytes)"); continue }
    if ($file.Length -gt 0) {
        $stream = [IO.File]::OpenRead($file.FullName)
        try {
            $header = New-Object byte[] 4
            $read = $stream.Read($header, 0, 4)
        } finally { $stream.Dispose() }
        if ($read -eq 4 -and (($header[0] -eq 0x53 -and $header[1] -eq 0x43 -and
            $header[2] -eq 0x45) -or ($header[0] -eq 0x7F -and $header[1] -eq 0x45 -and
            $header[2] -eq 0x4C -and $header[3] -eq 0x46))) {
            $findings.Add("Game executable signature: $relative")
            continue
        }
    }
    if ($knownGameHashes -contains (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) {
        $findings.Add("Known game file hash: $relative")
    }
}
if ($findings.Count) {
    $findings | ForEach-Object { Write-Error $_ }
    exit 1
}
Write-Host "Clean: $($files.Count) candidate files scanned."
