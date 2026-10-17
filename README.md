# Waybar for Windows

[Waybar](https://github.com/Alexays/Waybar)（Linux/Wayland 状态栏）的 **Windows 复刻移植版**。

目标是让 **同一份 `config.jsonc` 与 `style.css` 无需修改** 即可在 Windows 上运行：沿用上游
C++ / GTK3（gtkmm-3.0）代码库，新增 Windows 平台层（窗口、AppBar 工作区预留、原生数据源），
因此 GTK CSS 主题、`format`/`states`/`format-icons` 等配置语义与上游保持一致。

```
┌──────────────────────────────────────────────────────────────┐
│ 记忆 12:34   CPU 12%   RAM 61%   🔋 85%   ● WLAN   ⏻        │  ← 无边框、可透明、圆角
└──────────────────────────────────────────────────────────────┘
```

## 状态

| 阶段 | 内容 | 状态 |
|---|---|---|
| P0 | 工具链、仓库骨架、GTK/Win32 关键行为验证 | ✅ 完成 |
| P1 | 核心 Shell：配置/CSS/窗口/AppBar/模式/CLI/控制通道 | ✅ 完成 |
| P2 | 核心模块（clock/cpu/memory/battery/network/audio/custom…） | ✅ 完成 |
| P3 | 桌面集成模块（foreground window / taskbar / 输入法语言） | ⏳ 计划中 |
| P4 | 测试矩阵、文档、示例配置 | ✅ 完成 |
| P5 | 打包、CI、发布 | 🚧 进行中 |

模块兼容性分级见 [`docs/MODULE-MATRIX.md`](docs/MODULE-MATRIX.md)；Windows 与上游的差异见
[`docs/WINDOWS-DIFFERENCES.md`](docs/WINDOWS-DIFFERENCES.md)。

## 快速开始

前置：Windows 10 1809+ / Windows 11 x64，约 1.5 GB 磁盘空间；**不需要管理员权限**。

```powershell
# 1) 安装 MSYS2 MINGW64 工具链与 GTK3/gtkmm3 依赖（安装到 D:\tools\msys64）
powershell -ExecutionPolicy Bypass -File scripts\bootstrap-msys2.ps1

# 2) 编译
powershell -ExecutionPolicy Bypass -File scripts\build.ps1

# 3) 运行（自动注入 GTK 运行库路径）
powershell -ExecutionPolicy Bypass -File scripts\run.ps1
```

默认读取 `%USERPROFILE%\.config\waybar\`（或 `%APPDATA%\waybar\`、`%XDG_CONFIG_HOME%\waybar`）下的
`config.jsonc`（或 `config`）与 `style.css`。首次使用可复制仓库内模板：

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\.config\waybar" | Out-Null
Copy-Item resources\config.jsonc, resources\style.css "$env:USERPROFILE\.config\waybar\"
```

命令行与上游一致：

```
waybar --config <path> --style <path> --log-level <trace|debug|info|warning|error> --bar <id>
waybar --version | --help
```

Windows 额外提供控制命令（替代 POSIX 信号）：

```powershell
waybar --toggle          # 对应 SIGUSR1（受 on-sigusr1 配置控制）
waybar --reload          # 重载配置与样式（对应 SIGUSR2）
waybar --show / --hide
waybar --list-outputs    # 列出显示器名称/标识/几何，便于配置 output
```

## 打包与分发

生成免安装的 portable zip（含全部第三方 DLL 与 GTK 运行期资源）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
```

产物：`dist\waybar-<version>-windows-x64.zip`，解压后运行 `start-waybar.cmd` 即可（无需安装 MSYS2）。
运行单元测试：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Tests $true
powershell -ExecutionPolicy Bypass -File scripts\test.ps1
```

CI：`.github/workflows/build-windows.yml` 在 GitHub Actions 上用 MSYS2 构建、运行测试并上传 zip。

## 构建细节

见 [`docs/BUILD.md`](docs/BUILD.md)。要点：

- 工具链：MSYS2 MINGW64（gcc 16、meson、ninja），安装到仓库之外的 `D:\tools\msys64`。
- **GTK3 的 C++ 绑定（gtkmm3/glibmm）已被 MSYS2 移出仓库索引**，安装脚本会用 `pacman -U`
  精确安装镜像上保留的版本，详见构建文档。
- 字体：图标使用 Nerd Font 字形；未安装时默认样式回退到 `Segoe UI Symbol`。

## 与上游的关系

- 基线提交 `4e207a0`（Waybar v0.15.0），上游代码以 MIT 许可证发布，见 [`LICENSE`](LICENSE) 与
  [`NOTICE`](NOTICE)；同步流程见 [`docs/UPSTREAM.md`](docs/UPSTREAM.md)。
- 上游 `README` 原文存档于 [`docs/UPSTREAM-README.md`](docs/UPSTREAM-README.md)。

## 明确不支持

Wayland/合成器专有模块（`sway/*`、`hyprland/*`、`niri/*`、`river/*`、`dwl/*`、`wayfire/*`、
`ext/workspaces`）、SNI 托盘（`tray`）、以及依赖 Linux 服务的模块（`mpd`、`sndio`、`jack`、
`wireplumber`、`inhibitor`、`gamemode`、`systemd-failed-units`、`gps`、`wwan`、`privacy`、
`cava`）在 Windows 上会按上游「未知模块」的行为告警并禁用（配置仍可正常载入）。
详见 [`docs/MODULE-MATRIX.md`](docs/MODULE-MATRIX.md)。

## 许可证

MIT，见 [`LICENSE`](LICENSE)。
