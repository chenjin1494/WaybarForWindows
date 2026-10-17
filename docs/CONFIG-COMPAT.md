# 真实世界配置兼容性报告

本页记录用别人的真实 Waybar 配置在 Windows 移植版上实测的结果与修复项，作为「配置可直接复用」
的持续验证。测试对象：

- 仓库：<https://github.com/sky1234762/ZEK-niri>
- 目录：`configs/waybar/`（`config.jsonc` + `modules.jsonc` + `modules-dividers.jsonc` +
  `style.css` + `colors-noctalia.css` + 自定义脚本）
- 移植版版本：v0.15.0（含 `niri` 之外的全部 A/B 级模块）

## 1. 结论

**可以正常加载与渲染。** 配置里的 JSONC 注释、`include`、`#instance` 后缀、`group/*`、
`format-icons`、CSS `@import`/`@define-color`、渐变与分隔符全部生效；仅有与 niri/Wayland/
Linux 服务强绑定的模块被禁用（按上游「未知模块」行为告警），以及两个 Linux shell 脚本无法执行。

## 2. 实测结果

| 配置项 | 结果 |
|---|---|
| `include: ["modules.jsonc","modules-dividers.jsonc"]` | ✅ 相对路径 include 正常，注释/尾逗号均可解析 |
| `#` 实例后缀（`custom/left_div#5`、`clock#date`） | ✅ 每个实例独立创建 |
| `group/ddcutil`、`group/audio`、`group/powermenu` | ✅ group 容器与子模块正常 |
| `style.css` + `@import "colors-noctalia.css"` + `@define-color` | ✅ 主题色、渐变、圆角、分隔符渲染正确 |
| `niri/workspaces`、`niri/window` | ❌ 禁用（无 niri 合成器）。可改成 `sway/window`/`wlr/taskbar` |
| `tray` | ❌ 禁用（Windows 通知区域由 Explorer 独占） |
| `privacy`、`bluetooth` | ❌ 禁用（依赖 Linux 服务；另有可选实现方向见矩阵） |
| `network`（`{icon}` + `format-icons.wifi[]`） | ✅ 补丁后正常（见下） |
| `pulseaudio`（`{format_source}` + `format-source`） | ✅ 补丁后正常（采集端点音量/静音） |
| `custom/cava`、`custom/colors-theme`（`exec` 为 bash 脚本） | ⚠️ 脚本无法执行 → 模块报 `stopped unexpectedly`，其余不受影响 |
| `on-click`/`on-scroll-*`（`noctalia`、`ddcutil`、`setsid` 等 Linux 命令） | ⚠️ 点击时命令失败（Windows 无这些程序），不影响显示 |
| 字体 | ⚠️ 需安装 Nerd Font（本次用 **FiraCode Nerd Font** 验证），否则 Nerd Font 私有区字形显示为方框 |

## 3. 本次发现并修复的兼容性问题

### 3.1 `network` 缺少 `{icon}` 占位符

该配置使用 `"format-wifi": "{icon}"` + `format-icons: {wifi: [...5 个图标], ethernet, disconnected}`。
Windows 实现此前只提供 `{essid}/{signalStrength}/{ipaddr}/...`，导致
`network: format error: argument not found`。

修复：按状态选择图标（`wifi` 按信号强度取数组、`ethernet`/`linked`/`disconnected` 取单值），
并把 `disconnected` 分支改为与其它状态一致的格式化流程，新增 `tooltip-format-disconnected` 支持。

### 3.2 `pulseaudio` 缺少 `{format_source}`

该配置使用 `"format": "{icon} {format_source} "`、`format-source`/`format-source-muted`、
`format-bluetooth`，并把 `{format_source}` 也放进 `format-muted`。Windows 实现此前只提供
`{volume}/{icon}`，导致 `pulseaudio: argument not found` 且整条 label 更新失败。

修复：
- 读取默认**采集端点**（`eCapture`）的音量与静音，按 `format-source`/`format-source-muted`
  模板渲染 `{format_source}`（缺省 `{volume}%` / `muted`）；
- 图标选择按 `speaker → headphone → headset → bluetooth → default` 回退；
- label 与 tooltip 都注入 `{format_source}`。

> `format-bluetooth` 暂未触发（未做蓝牙端点识别），会回退到 `format`。

## 4. 复现步骤

```powershell
git clone --depth 1 https://v4.gh-proxy.org/https://github.com/sky1234762/ZEK-niri.git D:\tools\ZEK-niri
copy D:\tools\ZEK-niri\configs\waybar D:\tools\zek-test\waybar -Recurse

# 安装 Nerd Font（示例：FiraCode Nerd Font，v3.4.0），并按需把 style.css 的 font-family
# 首项改为 "FiraCode Nerd Font"（或安装配置里原本指定的 JetBrainsMono Nerd Font Propo）

waybar.exe --config D:\tools\zek-test\waybar\config.jsonc `
           --style  D:\tools\zek-test\waybar\style.css --log-level debug
```

预期日志（仅这些告警/错误）：

```
[warning] module niri/workspaces: Unknown module: niri/workspaces
[warning] module niri/window: Unknown module: niri/window
[warning] module tray: Unknown module: tray
[warning] module privacy: Unknown module: privacy
[warning] module bluetooth: Unknown module: bluetooth
[error] cava stopped unexpectedly, is it endless?          # Linux bash 脚本
[error] colors-theme stopped unexpectedly, is it endless?  # Linux bash 脚本
```

## 5. Windows 化建议

- `niri/workspaces` → 如需类似信息，可用 `wlr/taskbar` 代替（枚举窗口）；
- `niri/window` → 直接换成 `sway/window`（本移植版提供前台窗口标题）；
- `tray` → 暂无替代（Windows 托盘由系统管理）；
- 快捷键/点击命令（`noctalia`、`ddcutil`、`setsid`、`bash` 脚本）→ 换成 Windows 对应程序，
  或写成 `.cmd`/`.ps1` 并替换 `on-click`；
- 字体 → 安装 FiraCode Nerd Font（推荐，本次验证用）或配置中指定的 JetBrainsMono Nerd Font Propo。
