# 构建指南（Windows）

## 1. 前置条件

| 项 | 要求 |
|---|---|
| 系统 | Windows 10 1809+ / Windows 11，x64 |
| 磁盘 | 约 1.5 GB（MSYS2 + 依赖），另加构建目录 |
| 权限 | 普通用户即可（工具链默认装在 `D:\tools\msys64`，仓库之外） |
| 网络 | 能访问 `mirrors.tuna.tsinghua.edu.cn` 或 `mirror.msys2.org` |

不需要 Visual Studio、CMake、Python 或 WSL；WSL 仅用于拉取上游源码（见 `UPSTREAM.md`）。

## 2. 一次性环境准备

```powershell
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1
```

脚本做四件事（幂等）：

1. 下载官方 `msys2-x86_64-latest.tar.zst`（约 42 MB）并解压到 `D:\tools\msys64`；
2. 写入 pacman 镜像源（清华优先、官方兜底）；
3. `pacman-key` 初始化 + `pacman -Sy/-Syu` 更新基础包；
4. 安装构建依赖，并**补装仓库索引中已移除的 GTK3 C++ 绑定库**。

自定义安装位置：

```powershell
... -InstallDir 'E:\dev\msys64' -Mirror 'https://mirrors.ustc.edu.cn/msys2'
```

> **关于 gtkmm3**：MSYS2 已把 GTK3 的 C++ 绑定（`gtkmm3` 3.24.10、`glibmm` 2.66.10、
> `cairomm` 1.14.6、`pangomm` 2.46.4、`atkmm` 2.28.5、`libsigc++` 2.12.2）从仓库索引移除，
> 但镜像目录仍保留这些 `.pkg.tar.zst`。bootstrap 脚本会用 `pacman -U` 精确安装这些版本，
> 并且是幂等的（已安装则跳过）。

依赖清单（脚本内维护）：

```
mingw-w64-x86_64-toolchain  gtk3(传递)  gtkmm3  glibmm  cairomm  pangomm  atkmm  libsigc++
jsoncpp  fmt  spdlog  meson  ninja  pkgconf  ccache  cppwinrt  icu  adwaita-icon-theme  ntldd
msys: git curl
```

## 3. 编译

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
# 调试构建 / 完全重配
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Configuration debug -Clean
```

等价于在 MSYS2 MINGW64 终端中：

```bash
export PATH=/mingw64/bin:/usr/bin:/bin
cd /d/data/git/WaybarForWindows
meson setup build --buildtype=release -Dtests=true
meson compile -C build
```

产物：`build\waybar.exe`。

## 4. 运行

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run.ps1
powershell -ExecutionPolicy Bypass -File scripts\run.ps1 `
  -ConfigPath examples\windows\config.jsonc `
  -StylePath examples\windows\style.css `
  -LogLevel debug
```

`run.ps1` 会把 `D:\tools\msys64\mingw64\bin` 注入 `PATH`，否则 Windows 找不到 GTK 运行库。
直接双击 `waybar.exe` 通常因缺 DLL 失败——这是预期行为，请用脚本或打包后的目录。

## 5. 测试

```powershell
powershell -ExecutionPolicy Bypass -File scripts\test.ps1
powershell -ExecutionPolicy Bypass -File scripts\test.ps1 -Filter waybar -Verbose
```

## 6. 打包（P5）

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

（脚本在 P5 阶段实现：用 `ntldd` 递归收集 DLL，复制 GTK share/schemas/loaders 与默认配置到
`dist\waybar\`，生成便携 zip。）

## 7. 故障排查

| 症状 | 原因 / 处理 |
|---|---|
| `error: target not found: mingw-w64-x86_64-gtkmm3` | MSYS2 已移除该包；改用 `scripts\bootstrap-msys2.ps1`（它会 `pacman -U` 预编译包），不要手动 `pacman -S gtkmm3` |
| 启动即退出且无提示 | 缺 GTK DLL：用 `scripts\run.ps1`，或检查 `PATH` 是否包含 `mingw64\bin` |
| 界面全白/无样式 | `style.css` 未找到：检查 `%USERPROFILE%\.config\waybar\style.css` 或 `--style` |
| 图标显示为方框 | 未安装 Nerd Font；默认样式已回退 `Segoe UI Symbol`，安装字体后可显示完整图标 |
| `pacman` 下载慢/失败 | bootstrap 已用清华镜像；可加 `-Mirror <url>` 指定其他镜像 |
| 首次运行 MSYS2 提示初始化 | 正常：脚本首次调用 `bash` 时会自动生成 keyring 与 `/etc` 配置 |
| 中文路径下编译失败 | 本仓库已避开上游非法文件名（`.github/wiki`）；若仍报错，检查是否在 WSL 侧复制了完整上游树 |

## 8. 目录约定

| 路径 | 说明 |
|---|---|
| `build/` | meson 构建目录（git 忽略） |
| `dist/` | 打包产物（git 忽略） |
| `D:\tools\msys64` | 工具链（仓库之外，git 忽略） |
| `src/win32/`、`include/win32/` | Windows 平台层 |
| `scripts/` | 环境/构建/运行/测试/打包脚本 |
