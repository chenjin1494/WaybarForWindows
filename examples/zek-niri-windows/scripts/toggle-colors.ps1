#Requires -Version 5.1
<#
.SYNOPSIS
    ZEK-niri 主题配色切换（Windows 版）。

.DESCRIPTION
    在「预设配色（colors.css）」与「Noctalia 取色（colors-noctalia.css）」之间切换：
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
$manual = 'colors.css'
$noctalia = 'colors-noctalia.css'

if (-not (Test-Path $stylePath)) {
    if ($Mode -eq 'status') {
        Write-Output '{"text":"","alt":"manual","class":"manual","tooltip":"找不到 style.css"}'
    }
    exit 1
}

$styleText = [System.IO.File]::ReadAllText($stylePath, [System.Text.Encoding]::UTF8)
$active = $manual
if ($styleText -match '@import\s+"([^"]+\.css)"') {
    $active = $Matches[1]
}

$isNoctalia = $active -eq $noctalia
$next = if ($isNoctalia) { $manual } else { $noctalia }

if ($Mode -eq 'status') {
    $payload = if ($isNoctalia) {
        '{"text":"\uE0C8","alt":"noctalia","class":"noctalia","tooltip":"当前状态：Noctalia 取色\n左键：切换到预设配色"}'
    } else {
        '{"text":"\uE0C8","alt":"manual","class":"manual","tooltip":"当前状态：预设配色\n左键：切换到 Noctalia 取色"}'
    }

    # 以 UTF-8 输出，保证 Nerd Font 字形不被控制台代码页破坏
    [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false)
    Write-Output $payload
    exit 0
}

# toggle：改写 @import 行
$updated = [regex]::Replace($styleText, '@import\s+"[^"]+\.css"', ('@import "' + $next + '"'))
[System.IO.File]::WriteAllText($stylePath, $updated, (New-Object System.Text.UTF8Encoding($false)))

# 重载运行中的 waybar（命名管道控制通道）
$exe = Join-Path $env:LOCALAPPDATA 'Programs\Waybar\bin\waybar.exe'
if (Test-Path $exe) {
    & $exe --reload 2>$null | Out-Null
}
Write-Output ("已切换到 " + $next)
