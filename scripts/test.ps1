#Requires -Version 5.1
<#
.SYNOPSIS
    运行 WaybarForWindows 的单元测试（meson test / Catch2）。

.DESCRIPTION
    需要在 MSYS2 MINGW64 环境中执行，测试目标需先以 -Dtests=true 编译。

.PARAMETER BuildDir
    构建目录，默认 <repo>\build。

.PARAMETER Filter
    meson test 的 --suite/名称过滤（传给 meson test 的位置参数）。

.PARAMETER Verbose
    透传 --print-errorlogs 与 --verbose。

.PARAMETER InstallDir
    MSYS2 安装目录，默认 D:\tools\msys64。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\test.ps1
    powershell -ExecutionPolicy Bypass -File scripts\test.ps1 -Filter waybar -Verbose
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [string]$Filter,
    [switch]$Verbose,
    [string]$InstallDir = 'D:\tools\msys64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$bash = Join-Path $InstallDir 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "未找到 MSYS2: $bash" }

$repoRoot = Split-Path $PSScriptRoot -Parent
$buildPath = Join-Path $repoRoot $BuildDir
if (-not (Test-Path (Join-Path $buildPath 'build.ninja'))) {
    throw "构建目录未配置: $buildPath。请先运行 scripts\build.ps1"
}

$posixRoot = (& $bash -lc "cygpath -u '$repoRoot'").Trim()
$args = @('test', '-C', "'$BuildDir'", '--print-errorlogs')
if ($Verbose) { $args += '--verbose' }
if ($Filter) { $args += $Filter }

Write-Host "==> meson $($args -join ' ')" -ForegroundColor Cyan
$script = @"
set -o pipefail
export PATH=/mingw64/bin:/usr/bin:/bin
cd '$posixRoot'
meson $($args -join ' ')
"@
& $bash -lc $script
$code = $LASTEXITCODE
if ($code -ne 0) {
    Write-Host "测试失败（退出码 $code）" -ForegroundColor Red
    exit $code
}
Write-Host "测试通过" -ForegroundColor Green
