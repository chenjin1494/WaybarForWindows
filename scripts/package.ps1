#Requires -Version 5.1
<#
.SYNOPSIS
    打包可独立运行的 Waybar for Windows（portable zip）。

.DESCRIPTION
    1) 可选地先执行 release 构建；
    2) 以 waybar.exe 为根递归解析 PE 依赖，从 MSYS2 mingw64/bin 收集第三方 DLL；
    3) 复制 GTK 运行期资源（glib schemas、图标主题、gdk-pixbuf loaders、字体配置）；
    4) 生成 bin/waybar.exe + share/… + 示例配置 + 启动脚本，并压缩为 zip。

    产物默认在 dist\waybar-<version>-windows-x64.zip。
    注意：DLL 解析依赖 MSYS2 的 objdump（binutils），仅收集 mingw64 内的库；
    系统 DLL（kernel32/user32/…）不打包。

.PARAMETER BuildDir
    构建目录，默认 build。

.PARAMETER Configuration
    打包前构建使用的配置；配合 -SkipBuild 时忽略。

.PARAMETER SkipBuild
    跳过构建，直接使用现有 build/waybar.exe。

.PARAMETER OutputDir
    输出目录，默认 <repo>\dist。

.PARAMETER InstallDir
    MSYS2 安装目录，默认 D:\tools\msys64。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\package.ps1
    powershell -ExecutionPolicy Bypass -File scripts\package.ps1 -SkipBuild
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build',
    [ValidateSet('release', 'debug', 'debugoptimized')]
    [string]$Configuration = 'release',
    [switch]$SkipBuild,
    [string]$OutputDir = 'dist',
    [string]$InstallDir = 'D:\tools\msys64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path $PSScriptRoot -Parent
$bash = Join-Path $InstallDir 'usr\bin\bash.exe'
$mingw = Join-Path $InstallDir 'mingw64'
if (-not (Test-Path $bash)) { throw "未找到 MSYS2: $bash" }
if (-not (Test-Path (Join-Path $mingw 'bin'))) { throw "未找到 mingw64: $mingw" }

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "构建失败" }
}

$exe = Join-Path (Join-Path $repoRoot $BuildDir) 'waybar.exe'
if (-not (Test-Path $exe)) { throw "未找到 $exe，请先构建" }

$version = '0.0.0'
$mesonText = Get-Content (Join-Path $repoRoot 'meson.build') -Raw -Encoding UTF8
if ($mesonText -match "version\s*:\s*'([^']+)'") { $version = $Matches[1] }

$staging = Join-Path (Join-Path $repoRoot $OutputDir) "waybar-$version-windows-x64"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $staging 'bin') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $staging 'share') | Out-Null

Write-Host "==> 复制 waybar.exe" -ForegroundColor Cyan
Copy-Item $exe (Join-Path $staging 'bin\waybar.exe')

# ---------------------------------------------------------------- DLL 依赖
Write-Host "==> 解析并收集 DLL 依赖" -ForegroundColor Cyan

$posixRepo = (& $bash -lc "cygpath -u '$repoRoot'").Trim()
$posixMingwBin = (& $bash -lc "cygpath -u '$(Join-Path $mingw 'bin')'").Trim()
$exePosix = "$posixRepo/$BuildDir/waybar.exe"

# 单引号 here-string：内部不做 PowerShell 变量/反引号替换
$dllScriptTemplate = @'
set -e
export PATH=/mingw64/bin:/usr/bin:/bin
worklist="__EXE__"
seen=" "
result=""
while [ -n "$worklist" ]; do
  next=""
  for f in $worklist; do
    case " $seen " in *" $f "*) continue;; esac
    seen="$seen $f"
    for name in $(objdump -p "$f" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name: //p'); do
      candidate="__MINGWBIN__/$name"
      if [ -f "$candidate" ]; then
        result="$result
$candidate"
        next="$next $candidate"
      fi
    done
  done
  worklist="$next"
done
printf '%s\n' "$result" | sed '/^$/d' | sort -u
'@

$dllScript = $dllScriptTemplate.Replace('__EXE__', $exePosix).Replace('__MINGWBIN__', $posixMingwBin)

# 通过临时脚本文件执行，避免 Windows 命令行对引号/换行的再解析
$tempScript = Join-Path ([System.IO.Path]::GetTempPath()) ("waybar-dlls-{0}.sh" -f $PID)
[System.IO.File]::WriteAllText($tempScript, $dllScript.Replace("`r`n", "`n"),
                               (New-Object System.Text.UTF8Encoding($false)))
$posixTempScript = (& $bash -lc "cygpath -u '$tempScript'").Trim()
try {
    $dllList = & $bash -lc "bash '$posixTempScript'"
    if ($LASTEXITCODE -ne 0) { throw "解析 DLL 依赖失败" }
}
finally {
    Remove-Item $tempScript -Force -ErrorAction SilentlyContinue
}

$copied = 0
foreach ($dll in ($dllList -split "`r?`n" | Where-Object { $_ })) {
    $winPath = (& $bash -lc "cygpath -w '$dll'").Trim()
    if (-not (Test-Path $winPath)) { continue }
    Copy-Item $winPath (Join-Path $staging 'bin') -Force
    $copied++
}
Write-Host "    复制 $copied 个 DLL"

# ---------------------------------------------------------------- GTK 运行期资源
Write-Host "==> 复制 GTK 运行期资源" -ForegroundColor Cyan

function Copy-Tree($source, $destination) {
    if (-not (Test-Path $source)) { return }
    New-Item -ItemType Directory -Force $destination | Out-Null
    Copy-Item (Join-Path $source '*') $destination -Recurse -Force
}

# glib schemas（GSettings 必需，且必须是编译好的 gschemas.compiled）
$schemaSrc = Join-Path $mingw 'share\glib-2.0\schemas'
$schemaDst = Join-Path $staging 'share\glib-2.0\schemas'
New-Item -ItemType Directory -Force $schemaDst | Out-Null
if (Test-Path (Join-Path $schemaSrc 'gschemas.compiled')) {
    Copy-Item (Join-Path $schemaSrc 'gschemas.compiled') $schemaDst -Force
}
elseif (Test-Path $schemaSrc) {
    Copy-Item (Join-Path $schemaSrc '*.xml') $schemaDst -Force
    & $bash -lc "export PATH=/mingw64/bin:/usr/bin:/bin; glib-compile-schemas '$(($schemaDst -replace '\\','/') -replace '^(.).*', '/$1')'" 2>$null
}

# Adwaita 图标主题（bar 的 stock 图标/模块图标回退）
Copy-Tree (Join-Path $mingw 'share\icons\Adwaita') (Join-Path $staging 'share\icons\Adwaita')
Copy-Tree (Join-Path $mingw 'share\icons\hicolor') (Join-Path $staging 'share\icons\hicolor')

# gdk-pixbuf loaders（PNG/JPEG/SVG 等，image 模块需要）
$pixbufSrc = Join-Path $mingw 'lib\gdk-pixbuf-2.0'
$pixbufDst = Join-Path $staging 'lib\gdk-pixbuf-2.0'
if (Test-Path $pixbufSrc) {
    Copy-Tree $pixbufSrc $pixbufDst
}

# 字体配置（可选，缺失时 GTK 使用系统字体）
Copy-Tree (Join-Path $mingw 'etc\fonts') (Join-Path $staging 'etc\fonts')

# ---------------------------------------------------------------- 示例配置与启动脚本
$configDst = Join-Path $staging 'config'
New-Item -ItemType Directory -Force $configDst | Out-Null
Copy-Item (Join-Path $repoRoot 'resources\config.jsonc') $configDst -Force
Copy-Item (Join-Path $repoRoot 'resources\style.css') $configDst -Force
Copy-Item (Join-Path $repoRoot 'LICENSE') $staging -Force
Copy-Item (Join-Path $repoRoot 'README.md') (Join-Path $staging 'README.md') -Force

$startCmd = @"
@echo off
rem Start Waybar for Windows (portable). Adjust paths if needed.
setlocal
set ROOT=%~dp0
set PATH=%ROOT%bin;%PATH%
set GDK_PIXBUF_MODULE_FILE=%ROOT%lib\gdk-pixbuf-2.0\2.10.0\loaders.cache
set GSETTINGS_SCHEMA_DIR=%ROOT%share\glib-2.0\schemas
"%ROOT%bin\waybar.exe" --config "%ROOT%config\config.jsonc" --style "%ROOT%config\style.css" %*
endlocal
"@
[System.IO.File]::WriteAllText((Join-Path $staging 'start-waybar.cmd'), $startCmd, (New-Object System.Text.UTF8Encoding($false)))

# ---------------------------------------------------------------- 压缩
$zip = Join-Path (Join-Path $repoRoot $OutputDir) "waybar-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Write-Host "==> 压缩为 $zip" -ForegroundColor Cyan
Compress-Archive -Path $staging -DestinationPath $zip -CompressionLevel Optimal

$sizeMb = [Math]::Round((Get-Item $zip).Length / 1MB, 2)
Write-Host ""
Write-Host "打包完成: $zip ($sizeMb MB)" -ForegroundColor Green
