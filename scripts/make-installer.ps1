#Requires -Version 5.1
<#
.SYNOPSIS
    构建 Waybar for Windows 安装程序（setup.exe + payload，输出可分发 zip）。

.DESCRIPTION
    1) 编译 installer/setup.cpp 为原生 setup.exe（MSYS2 g++，静态链接，无外部依赖）
    2) 复用 scripts/package.ps1 的便携目录作为 payload（bin/share/lib/config）
    3) 生成 dist\waybar-<version>-setup\ 与 dist\waybar-<version>-setup-windows-x64.zip

    用户解压后双击 setup.exe 即可：复制文件、写注册表、建开始菜单、加开机启动。
    静默安装：setup.exe /S；不设开机启动：setup.exe /S /no-autostart。

.PARAMETER InstallDir
    MSYS2 安装目录，默认 D:\tools\msys64。

.PARAMETER SkipPayload
    复用已存在的 dist\waybar-<version>-windows-x64（跳过 package.ps1）。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\make-installer.ps1
    powershell -ExecutionPolicy Bypass -File scripts\make-installer.ps1 -SkipPayload
#>
[CmdletBinding()]
param(
    [string]$InstallDir = 'D:\tools\msys64',
    [switch]$SkipPayload
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path $PSScriptRoot -Parent
$bash = Join-Path $InstallDir 'usr\bin\bash.exe'
if (-not (Test-Path $bash)) { throw "未找到 MSYS2: $bash" }

$version = '0.0.0'
$mesonText = Get-Content (Join-Path $repoRoot 'meson.build') -Raw -Encoding UTF8
if ($mesonText -match "version\s*:\s*'([^']+)'") { $version = $Matches[1] }

$distRoot = Join-Path $repoRoot 'dist'
$portableDir = Join-Path $distRoot "waybar-$version-windows-x64"

if (-not $SkipPayload -or -not (Test-Path (Join-Path $portableDir 'bin\waybar.exe'))) {
    Write-Host "==> 生成便携 payload（scripts\package.ps1）" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'package.ps1')
    if ($LASTEXITCODE -ne 0) { throw "package.ps1 失败" }
}

if (-not (Test-Path (Join-Path $portableDir 'bin\waybar.exe'))) {
    throw "未找到 payload: $portableDir"
}

# ---------------------------------------------------------------- 编译 setup.exe
Write-Host "==> 编译 setup.exe" -ForegroundColor Cyan

$posixRepo = (& $bash -lc "cygpath -u '$repoRoot'").Trim()
$buildScript = @"
set -e
export PATH=/mingw64/bin:/usr/bin:/bin
cd '$posixRepo'
mkdir -p build
g++ -std=c++20 -O2 -municode -mconsole -static -static-libgcc -static-libstdc++ \
    -o build/setup.exe installer/setup.cpp \
    -lole32 -luuid -lshell32 -ladvapi32 -luser32 -lkernel32
"@
& $bash -lc $buildScript
if ($LASTEXITCODE -ne 0) { throw "setup.exe 编译失败" }

$setupExe = Join-Path $repoRoot 'build\setup.exe'
if (-not (Test-Path $setupExe)) { throw "未生成 $setupExe" }

# ---------------------------------------------------------------- 组装安装包
$setupDir = Join-Path $distRoot "waybar-$version-setup"
if (Test-Path $setupDir) { Remove-Item $setupDir -Recurse -Force }
New-Item -ItemType Directory -Force $setupDir | Out-Null

Write-Host "==> 组装 payload" -ForegroundColor Cyan
Copy-Item $setupExe (Join-Path $setupDir 'setup.exe')
$payloadDst = Join-Path $setupDir 'payload'
New-Item -ItemType Directory -Force $payloadDst | Out-Null
foreach ($item in @('bin', 'share', 'lib', 'etc', 'config', 'LICENSE', 'README.md')) {
    $src = Join-Path $portableDir $item
    if (Test-Path $src) {
        Copy-Item $src $payloadDst -Recurse -Force
    }
}

# 附带主题（可选，安装后复制到 config 目录即可使用）
$themesDst = Join-Path $payloadDst 'themes'
New-Item -ItemType Directory -Force $themesDst | Out-Null
$themeSources = @{
    'zek-niri-windows' = Join-Path $repoRoot 'examples\zek-niri-windows'
    'all-modules'      = Join-Path $repoRoot 'examples\windows'
}
foreach ($name in $themeSources.Keys) {
    $src = $themeSources[$name]
    if (Test-Path $src) {
        Copy-Item $src (Join-Path $themesDst $name) -Recurse -Force
    }
}

# 安装说明
$readme = @"
Waybar for Windows 安装包（v$version）
========================================

一键安装：
  1. 双击 setup.exe
  2. 确认安装位置（默认 %LOCALAPPDATA%\Programs\Waybar，无需管理员权限）
  3. 完成后从开始菜单启动 "Waybar"

安装程序会：
  - 复制程序与运行库（bin/share/lib/config）
  - 在 设置 → 应用 中注册卸载信息
  - 创建开始菜单快捷方式（Waybar / Uninstall Waybar）
  - 添加开机启动项（HKCU\...\CurrentVersion\Run\Waybar）
  - 注册 App Paths，使命令行可直接运行 waybar

静默安装：
  setup.exe /S
  setup.exe /S /no-autostart
  setup.exe /S /dir="D:\Apps\Waybar"

卸载：
  设置 → 应用 → Waybar for Windows → 卸载
  或运行安装目录下的 uninstall.exe --uninstall（/S 静默）

配置：
  安装目录\config\config.jsonc 与 style.css
  也可放到 %USERPROFILE%\.config\waybar\ 覆盖
"@
[System.IO.File]::WriteAllText((Join-Path $setupDir 'INSTALL.txt'), $readme,
                               (New-Object System.Text.UTF8Encoding($false)))

# ---------------------------------------------------------------- 压缩
$zip = Join-Path $distRoot "waybar-$version-setup-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Write-Host "==> 压缩为 $zip" -ForegroundColor Cyan
Compress-Archive -Path $setupDir -DestinationPath $zip -CompressionLevel Optimal

$sizeMb = [Math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host ""
Write-Host "安装包完成: $zip ($sizeMb MB)" -ForegroundColor Green
Write-Host "解压后双击 setup.exe 即可安装。" -ForegroundColor Green
