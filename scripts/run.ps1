#Requires -Version 5.1
<#
.SYNOPSIS
    运行编译好的 waybar.exe（自动注入 MSYS2 运行库搜索路径）。

.DESCRIPTION
    waybar.exe 依赖 MSYS2 mingw64 目录下的 GTK3 运行库。直接双击 exe 会因找不到
    DLL 而失败，因此本脚本把 <InstallDir>\mingw64\bin 放到 PATH 最前面再启动。

.PARAMETER ConfigPath
    传给 waybar 的 -c/--config 路径。

.PARAMETER StylePath
    传给 waybar 的 -s/--style 路径。

.PARAMETER LogLevel
    传给 waybar 的 -l/--log-level。

.PARAMETER Arguments
    额外的原样参数（例如 --toggle、-b bar-0）。

.PARAMETER ExePath
    指定可执行文件，默认 <repo>\build\waybar.exe。

.PARAMETER Wait
    等待进程退出（默认立即返回，便于 CI 之外的交互使用）。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\run.ps1
    powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -ConfigPath examples\windows\config.jsonc
    powershell -ExecutionPolicy Bypass -File scripts\run.ps1 -Arguments '--toggle'
#>
[CmdletBinding()]
param(
    [string]$ConfigPath,
    [string]$StylePath,
    [string]$LogLevel,
    [string[]]$Arguments = @(),
    [string]$ExePath,
    [string]$InstallDir = 'D:\tools\msys64',
    [switch]$Wait
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path $PSScriptRoot -Parent
if (-not $ExePath) { $ExePath = Join-Path $repoRoot 'build\waybar.exe' }
if (-not (Test-Path $ExePath)) { throw "未找到 $ExePath，请先运行 scripts\build.ps1" }

$mingwBin = Join-Path $InstallDir 'mingw64\bin'
if (-not (Test-Path $mingwBin)) { throw "未找到 MSYS2 mingw64: $mingwBin" }

$oldPath = $env:PATH
$env:PATH = "$mingwBin;$oldPath"

$argList = @()
if ($ConfigPath) { $argList += @('--config', (Resolve-Path $ConfigPath).Path) }
if ($StylePath) { $argList += @('--style', (Resolve-Path $StylePath).Path) }
if ($LogLevel) { $argList += @('--log-level', $LogLevel) }
if ($Arguments) { $argList += $Arguments }

try {
    Write-Host "==> $ExePath $($argList -join ' ')" -ForegroundColor Cyan
    if ($Wait) {
        & $ExePath @argList
        exit $LASTEXITCODE
    }
    Start-Process -FilePath $ExePath -ArgumentList $argList -WorkingDirectory $repoRoot | Out-Null
}
finally {
    $env:PATH = $oldPath
}
