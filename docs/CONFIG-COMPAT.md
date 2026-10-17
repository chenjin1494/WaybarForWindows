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
## 6. 第二次实测发现并修复：`include` 相对路径解析

把主题部署到**安装目录**（`%LOCALAPPDATA%\Programs\Waybar\config\`）后，从开始菜单启动时出现：

```
[warning] Unable to find resource file: modules.jsonc
[warning] Unable to find resource file: modules-dividers.jsonc
```

原因：`include` 之前只在「当前工作目录 + CONFIG_DIRS（`%USERPROFILE%\.config\waybar` 等）」中查找，
而安装目录不在其中；快捷方式的工作目录是 `bin\`，于是 include 全部失败（分组/自定义模块随之消失）。

修复（`src/config.cpp`）：`include` 的相对路径**优先按当前配置文件所在目录**解析，
其次才是 CWD / `WAYBAR_CONFIG_DIR` / `CONFIG_DIRS`；嵌套 include 逐层使用各自文件所在目录。
该行为与上游把 `config.jsonc` 放在 `~/.config/waybar` 时的直觉一致，也兼容 glob include。

## 7. 随安装包附带主题

安装包 payload 增加 `themes/`：

- `themes/zek-niri-windows/` —— 本文实测的适配版主题（本机已部署到安装目录 `config\`）
- `themes/all-modules/` —— 全模块示例配置

用法：复制到 `%LOCALAPPDATA%\Programs\Waybar\config\`（或自己的配置目录）即可。
## 8. 第三轮：高度、点击、焦点与滚轮（本次修复）

把主题部署到安装目录并实际使用后，发现并修复了 4 个 Windows 平台层问题：

### 8.1 高度被撑到 47px

GTK3 的 Win32 后端在没有 `default_size` 时，会把顶层窗口的最小高度钳制到“装饰尺寸”
（本机 47px），即使 `set_decorated(false)`；`height:30` 的配置因此无效。

修复：Windows 下对 bar 窗口设置 `set_resizable(false)` + `set_size_request(显示器宽, height)`
+ `set_default_size(显示器宽, height)`。实测该主题最终高度为 34px（内容自然高度），
与上游观感一致。

### 8.2 透明区域点击穿透（很多按钮点不到）

GTK 的 RGBA 顶层窗口在 Windows 上使用 `UpdateLayeredWindow`，**完全透明的像素不参与命中测试**：
主题大量使用透明背景，导致只有文字/图标笔画能点，模块空白处点击直接落到桌面。

修复：在 bar 正上方增加一个几乎不可见（uniform alpha=1）的输入窗口，把
`WM_MOUSEMOVE / WM_LBUTTON* / WM_RBUTTON* / WM_MBUTTON* / WM_MOUSEWHEEL / WM_MOUSEHWHEEL`
转发给 bar 窗口。整条 bar 任意位置都可点击，拖拽仍由 GTK 的鼠标捕获处理。

### 8.3 部分模块点击/滚轮无效

`ALabel` 的点击/滚轮事件默认不注册（`enable_click=false`、`enable_scroll=false`），
需要模块显式开启。修复：

| 模块 | 修复 |
|---|---|
| `power-profiles-daemon` | `enable_click`/`enable_scroll`（左键/滚轮循环电源计划，实测切换到“高性能”） |
| `idle_inhibitor` | `enable_click`（左键切换防休眠） |
| `backlight` | `enable_scroll`（滚轮调亮度） |
| `pulseaudio` | `enable_click` + `enable_scroll`（左键静音、滚轮调音量） |

### 8.4 bar 抢占前台窗口

状态栏窗口曾成为前台窗口，导致 `sway/window` 显示为空、用户正在输入的应用失焦。
修复：`WM_MOUSEACTIVATE -> MA_NOACTIVATE`、`gtk_window_set_accept_focus(false)`、
`gtk_window_set_focus_on_map(false)`。修复后 `sway/window` 能正确显示
“主文件夹 - 文件资源管理器”等真实前台窗口标题。

另外：从开始菜单/资源管理器启动时隐藏控制台窗口（`GetConsoleProcessList` 判断），
从 cmd/PowerShell 启动时保留输出。

### 8.5 验证结果

- 高度：`Bar configured (width: 1920, height: 34)`（此前恒为 47）
- 点击：颜色主题切换、截图（SnippingTool）、终端、资源管理器、网络/电池设置、
  电源计划切换、通知/电源设置均实测生效
- 前台窗口标题、配色切换脚本（`style.css` 的 `@import` 双向切换）正常
- `meson test` 通过
## 9. 全屏应用时“保持显示但允许被覆盖”（默认策略）

早期实现是「全屏时隐藏 bar」，切换有可见的收起/展开动画与工作区抖动。现改为与主流桌面一致的策略：

**bar 常态显示；检测到全屏应用时，把 bar（和它的透明输入层）压到 z-order 底部，
让全屏窗口自然覆盖它；退出全屏后恢复置顶。** 不隐藏窗口、不注销 AppBar、工作区不变。

- 触发：`SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`（前台切换即时响应，日志显示激活瞬间生效）
  + 150ms 轮询兜底（覆盖同一窗口内 F11 切换全屏的情况）；
- 判定：`SHQueryUserNotificationState`（独占全屏/演示/全屏视频）或
  「无标题栏且铺满显示器的前台窗口」；排除 shell、任务栏与 waybar 自身；
- 覆盖：`SetWindowPos(HWND_BOTTOM)`，bar 与输入层一起下沉，点击直接给全屏应用；
- 恢复：退出全屏并稳定约 800ms 后 `HWND_TOPMOST`，重新位于最上层。

实测（本机双屏）：

```
baseline:            bars=visible, point(10,10)=WaybarInput
全屏窗口打开:         bars=visible, point(10,10)=FULLSCREEN-TEST 窗口   ← bar 被覆盖但仍显示
全屏窗口关闭:         bars=visible, point(10,10)=WaybarInput           ← 恢复置顶
```

配置（bar 级，默认 `cover`）：

```jsonc
"fullscreen-behavior": "cover"   // cover=保持显示但可被覆盖；hide=全屏时收起；ignore=不处理
```

兼容旧键：`"hide-on-fullscreen": false` 等价于 `ignore`。
## 10. ZEK-niri 布局在 Windows 上的逐项对应

参考图（用户提供）为左/中/右三段结构，本移植版按下表对应：

| 参考模块 | Windows 实现 | 说明 |
|---|---|---|
| `niri/workspaces`（1 2 3） | 新增原生 `workspaces` 模块 | 1..3 数字按钮；点击/滚轮模拟 Ctrl+Win+←/→ 切换 Windows 虚拟桌面并本地高亮（Windows 无公开的虚拟桌面枚举 API） |
| `niri/window` | `sway/window` | 前台窗口标题，已修复 bar 抢焦点问题 |
| `custom/colors-theme` | 原脚本 `toggle-colors.ps1` | 切换 `style.css` 的 @import 并触发热重载 |
| `custom/actions`（Kitty） | Windows Terminal (PowerShell) | 左键 `wt.exe -p "Windows PowerShell"`，右键 `cmd.exe` |
| `custom/wallpapers` | `ms-settings:personalization-background` | 左键壁纸设置，右键个性化 |
| `custom/screenshot` | `ms-screenclip:` + 自带脚本 | 左键区域截图、中键全屏 PNG、右键截图工具 |
| `custom/clipboard` | `ms-settings:clipboard` | 右键清空剪贴板 |
| `idle_inhibitor`（小眼睛） | 原生 Windows 实现 | **小眼睛 = 防息屏**：眼睛图标表示已禁止自动熄屏，眼睛划线表示正常熄屏；左键切换，`start-activated` 开机即禁止 |
| `clock#date` / `clock` | 原生时钟 | 左键打开 Windows 日历 |
| `power-profiles-daemon` | 原生 Windows 实现 | 左键/滚轮循环电源计划（高性能/平衡/节能） |
| `custom/applauncher`（Arch 徽标） | 改为 Windows 11 徽标（FA windows 字形） | 左键发送 Win 键打开开始菜单，右键设置；背景改为深色以对应参考图的菱形徽标 |
| `custom/cava`（音频柱） | 新增原生 `custom/cava` 模块 | WASAPI 回环采集 + Goertzel 频谱，18 段方块字符渲染，随系统声音跳动 |
| `tray`（后台任务） | 新增原生 `tray` 模块 | Windows 通知区域无法嵌入：点击弹出当前窗口列表（可切换），右键打开任务管理器 |
| `group/ddcutil`（亮度） | `group/ddcutil` + 原生 `backlight`/`backlight/slider` | 滚轮调亮度，抽屉内滑块 |
| `group/audio`（音量/麦克风） | 原生 `pulseaudio` + 滑块 | 左键静音、右键麦克风、滚轮音量 |
| `bluetooth` | 新增原生 `bluetooth` 模块 | 检测蓝牙无线电，左键打开设置，右键运行 WinRT 切换脚本 |
| `network` | 原生 Windows 网络模块 | 左键网络设置 |
| `battery` | 原生 Windows 电池模块 | 左键电源设置 |
| `custom/swaync`（通知） | 静态铃铛 + `ms-settings:notifications` | Windows 通知计数无公开 API |
| `group/powermenu` | `custom/wlogout` | 左键重启 waybar（`--reload`），右键电源设置 |
| `privacy`（摄像头/麦克风占用） | 移除 | Windows 无对应查询接口 |

配色：新增 `colors-windows.css`，按参考截图取样（深色底 `#1a1f23`、紫色 `#a180c8`、蓝紫 `#7b7ecd`、天蓝 `#7ac1fc`）。

字体：`FiraCode Nerd Font` 优先。
### 分隔符（尖角）为什么之前只在中间

GTK CSS 无法匹配以数字开头的 class 选择器：主题里的 `#custom-right_div.5` 这类规则在
Windows/GTK 上被忽略，于是分隔符既没有背景色，也没拿到 `font-size: 1.39em`，箭头三角
只有默认字体那么高（约 16px），在 34px 高的 bar 里就表现为“尖角缩在中间”。

修复：

1. 把分隔符模块从 `custom/right_div#5` 改名为 `custom/right_div_5`（`left_div`、`inv` 同理），
   CSS 选择器同步改为 `#custom_right_div_5` → `#custom-right_div_5`，规则终于生效；
2. bar 高度从 34px 降到 24px（`"height": 20` + workspaces 按钮 4px），箭头三角的斜边
   正好从 bar 最上面延伸到最下面，与参考图一致；
3. 右侧亮度/音量分组改为直接模块（`backlight` / `pulseaudio`），避免抽屉滑块把 bar 撑高到 34px。
### 分隔符改为原生 Cairo 三角形（精确几何）

字体里的 powerline 字形（U+E0B0/E0B2）竖边只有约半个字高，且字形自带上下水平边，
在 24px 高的 bar 里仍然不是“底边铺满上下”的三角形。参考图要求的几何是：

- 三角形顶点在分隔符右缘 `x=0`，上下顶点 `(0,±1)`，左尖点 `(-√3,0)`；
- 底边（竖边）覆盖 bar 的整个高度，斜率 `±1/√3`，宽高比 `√3/2 ≈ 0.866`。

因此新增原生 `Divider` 模块（`src/modules/windows/divider.cpp`）：

- 用 Cairo `move_to/line_to/fill` 直接画等腰三角形，**底边向上下各延伸 2px**，
  抗锯齿后仍从 bar 顶边铺到底边；
- 背景色/三角形颜色仍由主题 CSS 的 `#custom-right_div_5 { background-color; color; }` 提供
  （GTK StyleContext 读取），因此每个分隔符的颜色保持原主题设定；
- 宽度固定 21px（24 × √3/2 ≈ 20.8），不再依赖字体字形高度；
- 工厂里拦截 `custom/left_div*`、`custom/right_div*`、`*_inv*`，其余 custom 模块不受影响。

实测（PrintWindow 抓取 1920×24）：三角形竖边在 y=0..23 全部着色，向右逐渐收窄到尖点。
### Windows 11 徽标（原生绘制）

`custom/applauncher` 在 Windows 下由原生 `WindowsLogo` 模块接管：

- Cairo 绘制 2×2 四格方块（官方 Win11 徽标样式，白色方块）；
- 背景色与两侧引导三角形统一为 `#75AAD7`（CSS：`#custom-applauncher { background-color }`、
  `#custom-left_div_1, #custom-right_div_1, #custom-left_div_11, #custom-right_div_11 { color }`）；
- 左键发送 Win 键打开开始菜单，右键打开 Windows 设置。
### 菱形（六边形）由徽标模块绘制

上一版徽标底色是矩形，两侧分隔符只把“三角形”染成 #75AAD7，两者之间留下深色楔形，
看上去像几块拼不齐的蓝色。现改为：徽标模块自己绘制一个六边形（左右各一个 11px 箭头
+ 中间矩形），填充 #75AAD7，并在中间画 2×2 白色方块；配置中移除徽标两侧的
`custom/left_div_1/11`、`custom/right_div_1/11`，不再需要外部三角形拼接。
### 中心组对齐与 cava 右侧三角形

- 原主题 `modules-center` 末尾还有 `custom/right_div#3`（cava 右边的过渡三角形），
  移植时漏掉；已补回，并同样加 `animation: none`（避免无窗口 DrawingArea 透明度问题）。
- 新增原生 `Spacer` 模块（`custom/spacer`，宽度取自配置 `"width": 47`），
  放在中心组最左侧，使徽标左侧总宽 = 右侧总宽（实测各 246px），
  徽标中心 x=960（1920/2）严格居中；调整 `width` 即可在布局变化后重新平衡。