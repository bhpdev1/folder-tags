# Renders the brand assets (logo PNGs, English UI mockups, README hero, GitHub social preview)
# from assets/brand/*.svg|html using headless Microsoft Edge.
#   .\scripts\render-brand.ps1              # everything
#   .\scripts\render-brand.ps1 -Only menu   # sources whose path contains "menu"
param([string]$Only)
$ErrorActionPreference = "Stop"

$root  = Split-Path $PSScriptRoot -Parent
$brand = Join-Path $root "assets\brand"
$edge  = @(
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe",
    "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $edge) { throw "Microsoft Edge not found." }

$profileDir = Join-Path $env:TEMP "foldertags-render"

function Render([string]$src, [string]$out, [int]$w, [int]$h, [double]$scale, [switch]$Transparent) {
    $url  = "file:///" + ((Join-Path $brand $src) -replace '\\', '/')
    $argv = @(
        "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-first-run",
        "--user-data-dir=`"$profileDir`"", "--allow-file-access-from-files",
        "--window-size=$w,$h", "--force-device-scale-factor=$scale",
        "--virtual-time-budget=3000", "--screenshot=`"$out`""
    )
    if ($Transparent) { $argv += "--default-background-color=00000000" }
    $argv += $url
    Remove-Item $out -ErrorAction SilentlyContinue
    Start-Process $edge -ArgumentList $argv -Wait -NoNewWindow
    if (-not (Test-Path $out)) { throw "Render failed: $out" }
    Write-Host ("{0,-40} {1}x{2} @{3}x" -f (Split-Path $out -Leaf), $w, $h, $scale)
}

$shots = Join-Path $root "assets\screenshots"
New-Item -ItemType Directory -Force $shots | Out-Null

$jobs = @(
    # source (relative to assets/brand)   output                                         w     h    scale
    @("logo.svg",                    (Join-Path $brand "logo-256.png"),               256,  256, 1, $true),
    @("logo.svg",                    (Join-Path $brand "logo-512.png"),               256,  256, 2, $true),
    # English UI mockups (CSS px = 125 % screenshot px)
    @("mockups\context-menu.html",   (Join-Path $shots "context-menu.png"),          1000,  700, 1.6, $false),
    @("mockups\tags-window.html",    (Join-Path $shots "tags-window.png"),            640,  448, 2.5, $false),
    @("mockups\explorer.html",       (Join-Path $shots "explorer.png"),              1000,  700, 1.6, $false),
    @("mockups\desktop.html",        (Join-Path $shots "desktop.png"),               1000,  700, 1.6, $false),
    # README hero + GitHub social preview
    @("hero.html",                   (Join-Path $root  "assets\hero.png"),           1280,  640, 2, $false),
    @("hero.html",                   (Join-Path $brand "social-preview.png"),        1280,  640, 1, $false),
    # Setup wizard side panel (Inno Setup picks the file closest to the DPI)
    @("installer.html",              (Join-Path $brand "installer\wizard-262.png"),   164,  314, 1.6, $false),
    @("installer.html",              (Join-Path $brand "installer\wizard-541.png"),   164,  314, 3.3, $false)
)
New-Item -ItemType Directory -Force (Join-Path $brand "installer") | Out-Null
foreach ($j in $jobs) {
    if ($Only -and ($j[0] -notlike "*$Only*")) { continue }
    Render $j[0] $j[1] $j[2] $j[3] $j[4] -Transparent:$j[5]
}

# logo.ico (setup and Apps & Features icon): PNG frames downscaled from logo-512.png.
if (-not $Only -or "logo.ico" -like "*$Only*" -or "installer" -like "*$Only*") {
    Add-Type -AssemblyName System.Drawing
    $srcPng = [System.Drawing.Image]::FromFile((Join-Path $brand "logo-512.png"))
    $frames = foreach ($s in 16, 20, 24, 32, 40, 48, 64, 96, 128, 256) {
        $bmp = New-Object System.Drawing.Bitmap $s, $s
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.DrawImage($srcPng, 0, 0, $s, $s)
        $g.Dispose()
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        [pscustomobject]@{ Size = $s; Bytes = $ms.ToArray() }
    }
    $srcPng.Dispose()
    $ico = New-Object System.IO.MemoryStream
    $w = New-Object System.IO.BinaryWriter $ico
    $w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$frames.Count)
    $offset = 6 + 16 * $frames.Count
    foreach ($f in $frames) {
        $dim = if ($f.Size -ge 256) { 0 } else { $f.Size }
        $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
        $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$f.Bytes.Length); $w.Write([uint32]$offset)
        $offset += $f.Bytes.Length
    }
    foreach ($f in $frames) { $w.Write($f.Bytes) }
    $w.Flush()
    [System.IO.File]::WriteAllBytes((Join-Path $brand "logo.ico"), $ico.ToArray())
    Write-Host "logo.ico                                 $($frames.Count) frames"
}
