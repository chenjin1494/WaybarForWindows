# Windows 与上游的差异

本文件记录移植过程中 **已实测** 的平台事实、以及由此产生的与上游 Waybar 的差异。
只写有依据的结论；未验证的内容标为「待验证」。

## 1. 已实测的平台事实（P0 冒烟，Windows 11 26300 / GTK3 3.24.52 / gtkmm 3.24.10）

| 能力 | 结果 | 结论 |
|---|---|---|
| GDK 后端 | `GDK_IS_WIN32_DISPLAY=1` | 走 win32 后端，GdkMonitor/显示器热插拔可用 |
| RGBA visual | `rgba_visual=YES`、`composited=1` | **可做真透明与 CSS 圆角**；GTK 会自动加 `WS_EX_LAYERED` |
| 无边框 + DOCK 类型 + 置顶 | 生效 | `set_decorated(false)` + `WINDOW_TYPE_HINT_DOCK` + `set_keep_above` |
| 跨屏定位 | `gtk_window_move` **无效**；`SetWindowPos` 精确生效 | bar 位置必须由 Win32 在 map 之后设置 |
| AppBar 预留工作区 | `ABM_NEW/QUERYPOS/SETPOS` 生效，工作区 1032→998；`ABM_REMOVE` 后恢复 | `exclusive: true` 用 AppBar 实现 |
| click-through | `WS_EX_TRANSPARENT` 生效 | `passthrough` 用原生扩展风格实现 |
| CSS | 含 `rgba()`、`linear-gradient`、`border-radius`、`box-shadow` 全部解析成功 | 直接复用上游 GTK CSS 语义 |
| 中文渲染 | `记忆 12:34` 正常显示 | 需要语体正确的 UTF-8 → UTF-16 转换 |
| 多显示器 | 2 台，可拿到 geometry / workarea / scale / model | `output` 匹配基于 model/manufacturer |
| 屏幕截图 | `BitBlt(..., CAPTUREBLT)` 可截到分层窗口内容 | 用于视觉回归；`gdk_pixbuf_get_from_window` 对分层窗口**返回空图**，不可用 |
| 控制台编码 | 默认 ANSI(936)，`SetConsoleOutputCP(CP_UTF8)` 可输出 UTF-8 | 日志与 `--list-outputs` 需显式切 UTF-8 |

## 2. 窗口行为映射

| 上游（wlr-layer-shell） | Windows 实现 | 说明 |
|---|---|---|
| `layer: bottom/top/overlay` | 普通 z 序 / `HWND_TOPMOST` | `overlay` 与 `top` 都置顶，差异仅在于是否置顶 + 不参与 Alt-Tab |
| `exclusive`（exclusive zone） | `SHAppBarMessage` 注册为 AppBar，预留工作区 | 最大化窗口自动避让 |
| `passthrough` | `WS_EX_LAYERED \| WS_EX_TRANSPARENT` | 鼠标完全穿透 |
| `position: top/bottom/left/right` | 按显示器工作区 + `width/height/margin` 计算矩形后 `SetWindowPos` | |
| `width`/`height` = 0（自动） | 先测 GTK 自然尺寸，再按测得的尺寸定位 | 两段式布局 |
| 每显示器一条 bar | `Gdk::Display` 枚举 + monitor 增删信号 | 显示器热插拔可用 |
| `output` 匹配 | 匹配 Gdk 显示器 `model`（name）/`manufacturer + model`（identifier） | Windows 无 `DP-1` 这类 connector 名 |
| `mode`/`modes`（hide/invisible/overlay…） | 映射到可见性/透明度/置顶/穿透 | `.mode-*` CSS 类照旧 |

### 2.1 待验证

- AppBar 在**非主显示器**上的工作区预留是否只影响该显示器。
- DPI 缩放 ≠ 100% 时 `height`/`margin` 的像素语义（按 GTK 逻辑像素解释还是物理像素）。
- 多 bar 同时注册 AppBar、以及全屏应用（`ABN_FULLSCREENAPP`）时的让位行为。
- Explorer 重启后 AppBar 注册丢失的自动恢复（`TaskbarCreated`）。

## 3. 运行时接口差异

| 上游 | Windows | 说明 |
|---|---|---|
| `SIGUSR1`/`SIGUSR2`（`on-sigusr1`/`on-sigusr2`） | `waybar --toggle/--show/--hide/--reload` 经命名管道下发 | 配置键语义不变：`hide/show/toggle/reload/noop` |
| `SIGRTMIN+1..` 刷新模块 | 待实现（命名管道命令 `refresh`） | |
| `SIGINT` | 控制台 Ctrl+C / `--quit` | |
| `SIGCHLD` + `waitpid` | Job Object（`KILL_ON_JOB_CLOSE`）+ 进程句柄回收 | bar 退出连带结束子进程 |
| XDG 外观门户（D-Bus） | 注册表 `AppsUseLightTheme` + `WM_SETTINGCHANGE` | `style-light.css`/`style-dark.css` 回退链保留 |
| logind inhibitor | `PowerRegisterSuspendResumeNotification` | `disable-on-sleep` 语义保留 |
| `wordexp`（`~`、`$VAR`、通配） | 自实现展开器（`~`、`$VAR`、`%VAR%`、`*`/`?`） | `include` 通配与相对路径语义保持一致 |
| `fork`+`/bin/sh -c` | `CreateProcessW` + `cmd.exe /d /s /c` | 可选 `shell` 键（`cmd`/`powershell`/`pwsh`/`sh`）；`WAYBAR_OUTPUT_NAME` 仍注入 |

## 4. 已知视觉差异

- **字体**：上游示例依赖 `FontAwesome`/Nerd Font 字形。Windows 默认样式回退 `Segoe UI Symbol`，
  未安装字体会出现方框（豆腐块）。
- **圆角一致性**：RGBA 窗口逐像素 alpha 可用，但桌面合成器与边框的抗锯齿细节无法与 Wayland 逐像素一致。
- **窗口阴影**：GTK 不提供窗口阴影，`box-shadow` 仅作用于控件内部。

## 5. 明确不支持

见 `MODULE-MATRIX.md` 中级别 **D** 的条目：Wayland 合成器专有模块、SNI 托盘、Linux 服务型模块。
其行为与上游「未知模块」一致（告警 + 禁用），配置仍可正常载入。
