#Requires -Version 5.1
<#
.SYNOPSIS
    安装/修复 WaybarForWindows 的 MSYS2 MINGW64 构建环境。

.DESCRIPTION
    本脚本是幂等的：已安装的部分会被跳过。
      1. 若缺少 MSYS2 运行时，下载官方 base 归档（msys2-x86_64-latest.tar.zst）并解压；
      2. 写入 pacman 镜像源（默认国内镜像优先，官方镜像兜底）；
      3. 初始化 pacman keyring、更新基础包、安装 GTK3/gtkmm3 等构建依赖；
      4. 自检关键包与 gtkmm3 pkg-config。

    工具链默认安装到 D:\tools\msys64（仓库之外），因此不会被 git 跟踪。

.PARAMETER InstallDir
    MSYS2 安装目录，默认 D:\tools\msys64。

.PARAMETER Mirror
    首选镜像根地址（不含 /distrib 等路径），默认清华镜像。

.PARAMETER FallbackMirror
    兜底镜像根地址，默认官方 mirror.msys2.org。

.PARAMETER SkipPackages
    只准备运行时与镜像源，不安装构建依赖。

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1

.NOTES
    需要约 1.5 GB 磁盘空间；不需要管理员权限。
#>
[CmdletBinding()]
param(
    [string]$InstallDir = 'D:\tools\msys64',
    [string]$Mirror = 'https://mirrors.tuna.tsinghua.edu.cn/msys2',
    [string]$FallbackMirror = 'https://mirror.msys2.org',
    [switch]$SkipPackages
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Step([string]$Message) {
    Write-Host ""
    Write-Host "==> $Message" -ForegroundColor Cyan
}

function Write-Ok([string]$Message) {
    Write-Host "    $Message" -ForegroundColor Green
}

function Write-Warn([string]$Message) {
    Write-Host "    $Message" -ForegroundColor Yellow
}

$bash = Join-Path $InstallDir 'usr\bin\bash.exe'
$mingwGtkmmPc = Join-Path $InstallDir 'mingw64\lib\pkgconfig\gtkmm-3.0.pc'

# ---------------------------------------------------------------- 1. 运行时
Write-Step "检查 MSYS2 运行时: $InstallDir"
if (Test-Path $bash) {
    Write-Ok "已存在，跳过下载"
}
else {
    $archive = Join-Path (Split-Path $InstallDir -Parent) 'msys64-base.tar.zst'
    $urls = @(
        "$Mirror/distrib/msys2-x86_64-latest.tar.zst",
        "$FallbackMirror/distrib/msys2-x86_64-latest.tar.zst"
    )
    $downloaded = $false
    foreach ($url in $urls) {
        try {
            Write-Host "    下载 $url"
            $sw = [System.Diagnostics.Stopwatch]::StartNew()
            Invoke-WebRequest -Uri $url -OutFile $archive -TimeoutSec 1800 -UseBasicParsing
            $sw.Stop()
            Write-Ok ("完成 {0:N1} MB / {1:N0}s" -f ((Get-Item $archive).Length / 1MB), $sw.Elapsed.TotalSeconds)
            $downloaded = $true
            break
        }
        catch {
            Write-Warn "失败: $($_.Exception.Message)"
        }
    }
    if (-not $downloaded) { throw "无法下载 MSYS2 base 归档，请检查网络后重试。" }

    Write-Host "    解压到 $InstallDir"
    New-Item -ItemType Directory -Path (Split-Path $InstallDir -Parent) -Force | Out-Null
    if (Test-Path $InstallDir) { Remove-Item $InstallDir -Recurse -Force }
    # 归档根目录为 msys64/，故解压到其父目录
    tar -xf $archive -C (Split-Path $InstallDir -Parent)
    if (-not (Test-Path $bash)) { throw "解压后仍未找到 $bash" }
    Write-Ok "解压完成"
}

# ------------------------------------------------------------ 2. 镜像源配置
Write-Step "配置 pacman 镜像源"
$mirrorDir = Join-Path $InstallDir 'etc\pacman.d'
$msysList = @(
    "Server = $Mirror/msys/`$arch/",
    "Server = $FallbackMirror/msys/`$arch/"
) -join "`n"
$mingwList = @(
    "Server = $Mirror/mingw/`$repo/",
    "Server = $FallbackMirror/mingw/`$repo/"
) -join "`n"
Set-Content -Path (Join-Path $mirrorDir 'mirrorlist.msys') -Value $msysList -Encoding ascii
Set-Content -Path (Join-Path $mirrorDir 'mirrorlist.mingw') -Value $mingwList -Encoding ascii
Write-Ok "mirrorlist.msys / mirrorlist.mingw 已写入"

# ------------------------------------------------------------ 3. 包管理器
Write-Step "初始化 pacman 并更新基础包"
& $bash -lc 'pacman-key --init >/dev/null 2>&1; pacman-key --populate msys2 >/dev/null 2>&1; pacman -Sy --noconfirm'
if ($LASTEXITCODE -ne 0) { throw "pacman -Sy 失败" }

# 基础包升级可能替换 msys2-runtime，导致本次 bash 失效，因此失败后重试一次。
& $bash -lc 'pacman -Syu --noconfirm'
if ($LASTEXITCODE -ne 0) {
    Write-Warn "首次 -Syu 返回 $LASTEXITCODE，重试"
    & $bash -lc 'pacman -Syu --noconfirm'
    if ($LASTEXITCODE -ne 0) { throw "pacman -Syu 失败" }
}
Write-Ok "基础包已更新"

if ($SkipPackages) {
    Write-Warn "已指定 -SkipPackages，跳过构建依赖安装"
    return
}

# ------------------------------------------------------------ 4. 构建依赖
# 说明：MSYS2 已把 GTK3 的 C++ 绑定（gtkmm3 / glibmm 2.66 / cairomm 1.14 /
# pangomm 2.46 / atkmm 2.28 / libsigc++ 2.12）从仓库索引中移除，但镜像仍保留
# 对应 .pkg.tar.zst。因此分两步：先装仓库内依赖，再用 pacman -U 精确安装绑定库。
Write-Step "安装构建依赖（首次约需 5-10 分钟）"

$repoPackages = @(
    'git',
    'mingw-w64-x86_64-toolchain',
    'mingw-w64-x86_64-jsoncpp',
    'mingw-w64-x86_64-fmt',
    'mingw-w64-x86_64-spdlog',
    'mingw-w64-x86_64-meson',
    'mingw-w64-x86_64-ninja',
    'mingw-w64-x86_64-pkgconf',
    'mingw-w64-x86_64-ccache',
    'mingw-w64-x86_64-cppwinrt',
    'mingw-w64-x86_64-icu',
    'mingw-w64-x86_64-adwaita-icon-theme',
    'mingw-w64-x86_64-ntldd'
) -join ' '

$bindingPackages = @(
    'mingw-w64-x86_64-libsigc%2B%2B-2.12.2-1-any.pkg.tar.zst',
    'mingw-w64-x86_64-glibmm-2.66.10-1-any.pkg.tar.zst',
    'mingw-w64-x86_64-cairomm-1.14.6-1-any.pkg.tar.zst',
    'mingw-w64-x86_64-pangomm-2.46.4-2-any.pkg.tar.zst',
    'mingw-w64-x86_64-atkmm-2.28.5-1-any.pkg.tar.zst',
    'mingw-w64-x86_64-gtkmm3-3.24.10-3-any.pkg.tar.zst'
) -join "`n"

$env:WB_MIRROR = $Mirror
$env:WB_FALLBACK = $FallbackMirror
$env:WB_REPO_PKGS = $repoPackages
$env:WB_BINDING_PKGS = $bindingPackages

$installScript = @'
set -e
export PATH=/mingw64/bin:/usr/bin:/bin
pacman -S --needed --noconfirm --noprogressbar ${WB_REPO_PKGS}

mkdir -p /tmp/wb-bindings
cd /tmp/wb-bindings
FILES=()
while IFS= read -r f; do
  [ -z "$f" ] && continue
  local_name="${f//%2B/+}"
  if [ ! -f "$local_name" ]; then
    if ! curl -fSL --retry 3 --max-time 600 -o "$local_name" \
        "${WB_MIRROR}/mingw/mingw64/$f"; then
      curl -fSL --retry 3 --max-time 600 -o "$local_name" \
        "${WB_FALLBACK}/mingw/mingw64/$f"
    fi
  fi
  FILES+=("$local_name")
  echo "    $local_name"
done <<< "${WB_BINDING_PKGS}"

pacman -U --noconfirm --noprogressbar "${FILES[@]}"
'@

& $bash -lc $installScript
if ($LASTEXITCODE -ne 0) {
    throw "依赖安装失败（退出码 $LASTEXITCODE）"
}
Write-Ok "依赖安装完成（含 GTK3 C++ 绑定库）"

# ------------------------------------------------------------ 5. 自检
Write-Step "环境自检"
& $bash -lc @'
set -e
export PATH=/mingw64/bin:/usr/bin:/bin
echo "    gcc      : $(gcc --version | head -1)"
echo "    meson    : $(meson --version)"
echo "    ninja    : $(ninja --version)"
echo "    pkgconf  : $(pkgconf --version)"
echo "    gtkmm3   : $(pkgconf --modversion gtkmm-3.0)"
echo "    gtk3     : $(pkgconf --modversion gtk+-3.0)"
echo "    jsoncpp  : $(pkgconf --modversion jsoncpp)"
echo "    fmt      : $(pkgconf --modversion fmt)"
echo "    spdlog   : $(pkgconf --modversion spdlog)"
echo "    icu      : $(pkgconf --modversion icu-uc)"
'@
if ($LASTEXITCODE -ne 0) { throw "环境自检失败：请检查 gtkmm3 是否正确安装" }

Write-Host ""
Write-Host "MSYS2 构建环境就绪。" -ForegroundColor Green
Write-Host "  安装目录: $InstallDir"
Write-Host "  下一步  : powershell -ExecutionPolicy Bypass -File scripts\build.ps1"
