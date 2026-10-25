[CmdletBinding()]
param([Parameter(Mandatory)][string]$InputPng,
      [Parameter(Mandatory)][string]$OutputJpeg)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$source = [System.Drawing.Image]::FromFile((Resolve-Path -LiteralPath $InputPng).Path)
try {
    $bitmap = New-Object System.Drawing.Bitmap 256, 256
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($source, 0, 0, 256, 256)
        } finally { $graphics.Dispose() }
        New-Item -ItemType Directory -Path (Split-Path -Parent $OutputJpeg) -Force | Out-Null
        $bitmap.Save($OutputJpeg, [System.Drawing.Imaging.ImageFormat]::Jpeg)
    } finally { $bitmap.Dispose() }
} finally { $source.Dispose() }
