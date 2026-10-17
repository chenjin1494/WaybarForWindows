#Requires -Version 5.1
<#
.SYNOPSIS
    ZEK-niri 主题配色切换（Windows 版）。

.DESCRIPTION
    在两套配色之间切换：
      - colors-windows.css：Windows 移植版紫色配色（默认）
      - colors.css：原版 ZEK-niri 暖色配色（基于原配置）
    改写 style.css 里的 @import 行，然后调用 waybar.exe --reload 重载。

    status 模式输出 waybar custom 模块需要的 JSON（return-type: json）。

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File toggle-colors.ps1 status
    powershell -NoProfile -ExecutionPolicy Bypass -File toggle-colors.ps1 toggle
#>
[CmdletBinding()]
param(
    [ValidateSet('status', 'toggle')]
    [string]$Mode = 'status'
)

$ErrorActionPreference = 'Stop'

$configDir = Split-Path $PSScriptRoot -Parent
$stylePath = Join-Path $configDir 'style.css'
$palettes = @('colors-windows.css', 'colors.css')

if (-not (Test-Path $stylePath)) {
    if ($Mode -eq 'status') {
        Write-Output '{"text":"","alt":"windows","class":"windows","tooltip":"找不到 style.css"}'
    }
    exit 1
}

$styleText = [System.IO.File]::ReadAllText($stylePath, [System.Text.Encoding]::UTF8)
$active = $palettes[0]
if ($styleText -match '@import\s+"([^"]+\.css)"') {
    $name = $Matches[1]
    if ($palettes -contains $name) { $active = $name }
}
$isWindows = $active -eq 'colors-windows.css'
$next = if ($isWindows) { 'colors.css' } else { 'colors-windows.css' }

if ($Mode -eq 'status') {
    $payload = if ($isWindows) {
        '{"text":"\uE0C8","alt":"windows","class":"windows","tooltip":"当前：Windows 紫配色\n左键：切换到暖色（原版配色）"}'
    } else {
        '{"text":"\uE0C8","alt":"warm","class":"warm","tooltip":"当前：暖色（原版配色）\n左键：切换到 Windows 紫配色"}'
    }
    [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
    Write-Output $payload
    exit 0
}

$updated = [regex]::Replace($styleText, '@import\s+"[^"]+\.css"', ('@import "' + $next + '"'))
[System.IO.File]::WriteAllText($stylePath, $updated, (New-Object System.Text.UTF8Encoding($false)))

$exe = Join-Path $env:LOCALAPPDATA 'Programs\Waybar\bin\waybar.exe'
if (Test-Path $exe) {
    & $exe --reload 2>$null | Out-Null
}
Write-Output ("已切换到 " + $next)
