# ZEK-niri 主题（Windows 适配版）

来源：<https://github.com/sky1234762/ZEK-niri/tree/main/configs/waybar>
（原配置面向 niri + Noctalia，作者 sky1234762；此处仅做 Windows 移植适配，版权归原作者）

本目录是可直接用于 `WaybarForWindows` 的版本：保留原主题的配色、分隔符、分组与图标布局，
把 niri/Linux 专有部分替换成 Windows 等价实现。

## 与原配置的差异

| 原配置 | Windows 适配 |
|---|---|
| `niri/workspaces` | 移除（无 niri 合成器） |
| `niri/window` | `sway/window`（前台窗口标题，支持 `max-length`/`rewrite`/tooltip） |
| `tray` | 移除（Windows 通知区域由 Explorer 独占） |
| `privacy`、`bluetooth` | 移除（依赖 Linux 服务） |
| `custom/cava` | 移除（无 cava 服务；Linux bash 脚本） |
| `custom/ddcutil-*`（`ddcutil` 外接屏亮度） | 移除，`group/ddcutil` 改为内置屏 `backlight` + `backlight/slider`（WMI） |
| `custom/swaync`（`exec` 取通知数） | 改为静态通知图标，点击打开 Windows 通知设置 |
| `noctalia`、`setsid`、`ddcutil`、bash 脚本 | 换成 Windows 命令：`explorer.exe`、`cmd /c start ms-settings:*`、`ms-screenclip:` 等 |
| `pulseaudio` 的 `on-click`/`on-scroll-*` | 移除，改用本移植版原生行为（左键静音、滚轮调音量） |
| 字体 `JetBrainsMono Nerd Font Propo` | 首选 **FiraCode Nerd Font**（回退原字体链） |
| `custom/colors-theme`（bash 切换配色） | `scripts/toggle-colors.ps1`（改写 `@import` 后 `waybar --reload`） |

> `fixed-center`、`layer: top`、`include`、`#` 实例、`group/*` 抽屉、`format-icons`、
> `@define-color`/`@import` 等配置语义保持不变。

## 使用

安装包默认把配置放在 `%LOCALAPPDATA%\Programs\Waybar\config\`。安装完成后：

```powershell
$dst = "$env:LOCALAPPDATA\Programs\Waybar\config"
Copy-Item .\* $dst -Recurse -Force    # 在本目录执行
# 启动（开始菜单里的 Waybar 也使用这份配置）
& "$env:LOCALAPPDATA\Programs\Waybar\bin\waybar.exe"
```

也可以只把 `config.jsonc`、`modules.jsonc`、`modules-dividers.jsonc`、`style.css`、
`colors*.css`、`scripts\` 复制到自己的配置目录，然后用 `--config/--style` 指定。

## 配色切换

- 左键点击调色板图标（`custom/colors-theme`）在 **预设配色** 与 **Noctalia 取色** 之间切换；
- 脚本位置：`scripts/toggle-colors.ps1`，手动执行 `status` 可查看当前状态。

## 字体

需要 Nerd Font：推荐 **FiraCode Nerd Font**（本项目内置样式也首选它）。
未安装时字形会显示为方框，可安装 [Nerd Fonts](https://www.nerdfonts.com/) 任一字体后
修改 `style.css` 的 `font-family` 首项。
