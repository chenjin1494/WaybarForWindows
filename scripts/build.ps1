#Requires -Version 5.1
<#
.SYNOPSIS
    在 MSYS2 MINGW64 环境中配置并编译 WaybarForWindows。

.DESCRIPTION
    等价于在 MSYS2 MINGW64 终端里执行:
        meson setup build --buildtype=<type> -Dtests=<bool>
        meson compile -C build

    首次编译前请先执行 scripts\bootstrap-msys2.ps1。

.PARAMETER Configuration
    release / debug / debugoptimized，默认 release。

.PARAMETER BuildDir
    构建目录，默认 <repo>\build。

.PARAMETER Tests
    是否启用 Catch2 测试目标，默认 true。

.PARAMETER Clean
    编译前删除构建目录（完全重配）。

.PARAMETER InstallDir
    MSYS2 安装目录，默认 D:\tools\msys64。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Configuration debug -Clean
#>
[CmdletBinding()]
param(
    [ValidateSet('release', 'debug', 'debugoptimized')]
    [string]$Configuration = 'release',
    [string]$BuildDir = 'build',
    [bool]$Tests = $true,
    [switch]$Clean,
    [string]$InstallDir = 'D:\tools\msys64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$bash = Join-Path $InstallDir 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) {
    throw "未找到 MSYS2: $bash。请先运行 scripts\bootstrap-msys2.ps1"
}

$repoRoot = Split-Path $PSScriptRoot -Parent
if ($Clean) {
    $target = Join-Path $repoRoot $BuildDir
    if (Test-Path $target) {
        Write-Host "清理 $target" -ForegroundColor Yellow
        Remove-Item $target -Recurse -Force
    }
}

# 转成 MSYS2 路径（/d/data/git/...）
$posixRoot = (& $bash -lc "cygpath -u '$repoRoot'").Trim()
if ($LASTEXITCODE -ne 0 -or -not $posixRoot) { throw "无法转换仓库路径为 MSYS2 路径" }

$testsValue = if ($Tests) { 'enabled' } else { 'disabled' }
$ninjaJobs = [Math]::Max(1, [Environment]::ProcessorCount)

Write-Host "==> 配置与编译" -ForegroundColor Cyan
Write-Host "    源码: $repoRoot"
Write-Host "    构建: $BuildDir ($Configuration, tests=$testsValue, jobs=$ninjaJobs)"

$script = @"
set -e
export PATH=/mingw64/bin:/usr/bin:/bin
cd '$posixRoot'
if [ ! -f '$BuildDir/build.ninja' ]; then
  meson setup '$BuildDir' --buildtype=$Configuration -Dtests=$testsValue
else
  meson configure '$BuildDir' --buildtype=$Configuration -Dtests=$testsValue >/dev/null
fi
meson compile -C '$BuildDir' -j $ninjaJobs
"@

& $bash -lc $script
if ($LASTEXITCODE -ne 0) { throw "编译失败（退出码 $LASTEXITCODE）" }

$exe = Join-Path (Join-Path $repoRoot $BuildDir) 'waybar.exe'
if (Test-Path $exe) {
    $size = [Math]::Round((Get-Item $exe).Length / 1MB, 2)
    Write-Host ""
    Write-Host "编译成功: $exe ($size MB)" -ForegroundColor Green
    Write-Host "运行: powershell -ExecutionPolicy Bypass -File scripts\run.ps1"
}
else {
    Write-Warning "编译命令成功，但未找到 $exe"
}
