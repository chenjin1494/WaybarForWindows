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

---

## 6. P1 实施记录（核心 Shell 已跑通）

以下问题是在真实构建/运行中暴露并已修复的，属于移植时容易踩的坑：

| 问题 | 根因 | 处理 |
|---|---|---|
| `Gtk::Application` 报 `Unknown option --config` | GOption 会解析 `argv`，不认识我们自己的 CLI 参数 | 只把 `argv[0]` 交给 `Gtk::Application::create`，其余参数先由 clara 解析 |
| `Cannot convert character sequence: Illegal byte sequence`，进程退出 | Windows 的 `std::filesystem::path(std::string)` 按 ANSI 解释窄串；`C:\Users\<中文>\...` 直接抛异常 | 新增 `include/util/fs.hpp`：`to_path()` 在 Windows 先转 UTF-16；所有 `ifstream/ofstream/fs::exists/directory_iterator` 改走该封装 |
| `%LOCALAPPDATA%` 读到乱码、实例目录创建 "Permission denied" | `std::getenv` 返回 ANSI 字节 | 新增 `win32::get_env()`（`GetEnvironmentVariableW`），路径展开与 `WAYBAR_CONFIG_DIR` 均改用宽 API |
| `uint` 未定义 | `uint` 是 POSIX `sys/types.h` 的扩展类型，MinGW 不提供 | 上游代码中改用 `unsigned int`（`AModule.hpp/.cpp`、`ALabel.cpp`） |
| `fmt::localtime` 不存在 | 本机为 fmt 12，`fmt::localtime` 已被移除 | `simpleclock.cpp` 改用 `localtime_s`/`localtime_r` 包装 |
| `PowerRegisterSuspendResumeNotification` 不可用 | MSYS2 头文件缺少新式 `DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS` | 改用隐藏顶层窗口接收 `WM_POWERBROADCAST`（`src/win32/sleep.cpp`） |
| `gdesktopappinfo.h` / `Gio::DesktopAppInfo` 缺失 | 该 API 属于 gio-unix，Windows 不提供 | `icon_loader.cpp` 仅在非 Windows 编译；`image-name` 类应用图标在 Windows 暂不可用（P3 视需要实现） |
| `Gdk::Monitor::get_geometry()` 编译失败 | gtkmm 3 的签名是 `void get_geometry(Gdk::Rectangle&)` | 改为传引用 |
| 圆角/透明是否可行 | 未知 | 已验证：GTK 自动 `WS_EX_LAYERED` + RGBA visual，逐像素 alpha 与 CSS `border-radius` 直接生效 |
| 配置高度小于内容最小高度 | GTK 会强制窗口不小于内容尺寸 | `BarBackend` 取 `max(配置高度, 内容高度)`，同时保留上游的 "less than the minimum" 警告 |

### 已端到端验证（P1 验收）

- `config.jsonc`（含注释/尾逗号）与 `style.css` 原样加载；
- 两条 bar（双显示器）分别定位到各自显示器顶部，`exclusive: true` 通过 AppBar 预留工作区；
- 半透明背景、圆角、`linear-gradient`、中文与 emoji 正常渲染（截图为证）；
- `custom/os`（`ver`）与 `custom/uptime`（PowerShell）执行成功；`clock` 正常走时；
- 控制通道：`waybar --toggle/--show/--reload` 全部返回 `OK`，隐藏/恢复/重载生效；
- 退出后工作区恢复、实例登记文件清理。

---

## 7. P2 模块实现记录

| 模块 | Windows 数据源 | 关键实现点 |
|---|---|---|
| `cpu` / `cpu_usage` / `cpu_graph` | `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` | 每逻辑处理器 idle/kernel/user 累计时间；索引 0 为总体，与 `/proc/stat` 语义对齐；`kernel` 已含 `idle` |
| `cpu_frequency` | 注册表 `HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\<n>\~MHz` | 标称频率，非实时（MSYS2 头文件缺 `PROCESSOR_POWER_INFORMATION`） |
| `memory` | `GlobalMemoryStatusEx` | 单位换算沿用上游（kiB）；swap 用“提交上限 − 物理内存”近似 |
| `load` | PDH `\System\Processor Queue Length` + EMA | Windows 无 loadavg；1/5/15 分钟为指数滑动平均，属于 B 级语义映射 |
| `disk` | `GetDiskFreeSpaceExW` | 以字节为“块”、`frsize=1`，复用上游百分比/fmt 逻辑；`/` → `%SystemDrive%`，`/mnt/c` → `C:\` |
| `user` | `GetTickCount64` + GLib 用户名/家目录 | `{user}`/`{work_H}`/`{work_M}`/`{work_S}`/`{work_d}` 与上游一致 |
| `battery` | `GetSystemPowerStatus` | 完整复刻上游 `format-{status}`/`format-{state}`/`format-time`/`full-at`/`events` 解析链；无电池时隐藏 |
| `idle_inhibitor` | `SetThreadExecutionState` | 在 GTK 主线程调用（ES 状态按线程）；保留 `timeout`/`timeout-step`/`dynamic-timeouts`/滚动调时 |
| `keyboard-state` | `GetKeyState` + `AttachThreadInput` | 必须先 attach 到前台窗口线程才能读到真实锁定状态 |

新增链接库：`ntdll`（`NtQuerySystemInformation`）、`advapi32`（注册表）、`ole32`/`oleaut32`（后续 WMI/Core Audio）。
### P2 第二批：network / pulseaudio

| 模块 | 实现要点 |
|---|---|
| `network` | `GetAdaptersAddresses` 选路（优先有网关、其次无线）→ IPv4/IPv6/cidr；无线再走 `WlanQueryInterface` 取 SSID 与信号质量；带宽用 `GetIfEntry2` 的 `InOctets`/`OutOctets` 差分并按秒归一化。注意 `winsock2.h` 必须先于 `windows.h`，并链接 `ws2_32`。 |
| `pulseaudio` | Core Audio：`MMDeviceEnumerator` → 默认 `eRender/eConsole` 端点 → `IAudioEndpointVolume` 读/写音量与静音。每次采样独立 `CoInitializeEx`，避免跨线程持有 COM；链接 `ole32`/`uuid`。 |
### 控制通道的命名管道注意事项（P4 测试发现）

- `CreateNamedPipeW` 的 `dwOpenMode` **必须**包含 `FILE_FLAG_OVERLAPPED`；否则即使给 `ConnectNamedPipe` 传 `OVERLAPPED`，调用仍会同步阻塞，导致 `stop()` 无法用 `SetEvent` 唤醒监听线程（测试中表现为进程挂起）。
- 监听循环用独立 `connect_event` + `WaitForMultipleObjects({stop_event, connect_event})`，避免把停止事件复用为 OVERLAPPED 的完成事件（后者会被连接完成误触发）。
- 客户端 `CreateFileW` 前先 `WaitNamedPipeW(name, timeout)`，避免服务端暂时没有实例时无限等待。