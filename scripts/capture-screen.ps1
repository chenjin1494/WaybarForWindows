#Requires -Version 5.1
<#
.SYNOPSIS
    Capture the screen (or a region / window) to PNG for Waybar for Windows visual checks.

.DESCRIPTION
    Uses GDI BitBlt with CAPTUREBLT so layered windows (the transparent, rounded bar) are included.
    Note: gdk_pixbuf_get_from_window returns an empty image for layered windows and must not be used.

.PARAMETER Path
    Output PNG path. Defaults to <repo>\dist\screenshots\screen-<timestamp>.png.

.PARAMETER X / Y / Width / Height
    Capture region. When Width/Height are omitted the whole virtual screen is captured.

.PARAMETER WindowTitle
    Capture the window with this exact title (via FindWindow + GetWindowRect).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\capture-screen.ps1
    powershell -ExecutionPolicy Bypass -File scripts\capture-screen.ps1 -X 0 -Y 0 -Width 800 -Height 40
#>
[CmdletBinding()]
param(
    [string]$Path,
    [int]$X = 0,
    [int]$Y = 0,
    [int]$Width = 0,
    [int]$Height = 0,
    [string]$WindowTitle
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

if (-not ('WaybarCapture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class WaybarCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    [DllImport("user32.dll")]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int nIndex);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll", SetLastError = true)]
    public static extern bool BitBlt(IntPtr hdcDest, int xDest, int yDest, int width, int height,
                                     IntPtr hdcSrc, int xSrc, int ySrc, int rop);

    public const int SRCCOPY = 0x00CC0020;
    public const int CAPTUREBLT = 0x40000000;
}
'@
}

if ($WindowTitle) {
    $hwnd = [WaybarCapture]::FindWindow($null, $WindowTitle)
    if ($hwnd -eq [IntPtr]::Zero) { throw "Window not found: $WindowTitle" }
    $rect = New-Object WaybarCapture+RECT
    if (-not ([WaybarCapture]::GetWindowRect($hwnd, [ref]$rect))) { throw "GetWindowRect failed" }
    $X = $rect.Left
    $Y = $rect.Top
    $Width = $rect.Right - $rect.Left
    $Height = $rect.Bottom - $rect.Top
}

if ($Width -le 0 -or $Height -le 0) {
    # SM_XVIRTUALSCREEN=76, SM_YVIRTUALSCREEN=77, SM_CXVIRTUALSCREEN=78, SM_CYVIRTUALSCREEN=79
    $X = [WaybarCapture]::GetSystemMetrics(76)
    $Y = [WaybarCapture]::GetSystemMetrics(77)
    $Width = [WaybarCapture]::GetSystemMetrics(78)
    $Height = [WaybarCapture]::GetSystemMetrics(79)
}

if (-not $Path) {
    $repoRoot = Split-Path $PSScriptRoot -Parent
    $dir = Join-Path $repoRoot 'dist\screenshots'
    New-Item -ItemType Directory -Force $dir | Out-Null
    $Path = Join-Path $dir ("screen-{0:yyyyMMdd-HHmmss}.png" -f (Get-Date))
}
else {
    $parent = Split-Path $Path -Parent
    if ($parent) { New-Item -ItemType Directory -Force $parent | Out-Null }
}

$bitmap = New-Object System.Drawing.Bitmap($Width, $Height)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$screenDc = [WaybarCapture]::GetDC([IntPtr]::Zero)
$targetDc = $graphics.GetHdc()
try {
    $rop = [WaybarCapture]::SRCCOPY -bor [WaybarCapture]::CAPTUREBLT
    if (-not [WaybarCapture]::BitBlt($targetDc, 0, 0, $Width, $Height, $screenDc, $X, $Y, $rop)) {
        throw "BitBlt failed"
    }
}
finally {
    $graphics.ReleaseHdc($targetDc)
    [void][WaybarCapture]::ReleaseDC([IntPtr]::Zero, $screenDc)
    $graphics.Dispose()
}
try {
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
}
finally {
    $bitmap.Dispose()
}

Write-Host ("screenshot: {0} ({1}x{2} at {3},{4})" -f $Path, $Width, $Height, $X, $Y) -ForegroundColor Green
