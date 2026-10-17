# 模块兼容性矩阵

级别定义：

| 级别 | 含义 |
|---|---|
| **A** | 完全等价：模块名、配置键、`format`/`states`/`format-icons`/tooltip/交互语义与上游一致，数据来自 Windows 原生 API |
| **B** | 语义映射：模块名与主要配置键一致，含义映射到 Windows 概念；个别键不适用或被近似 |
| **D** | 不支持：与上游「未知模块」行为一致（`Disabling module "<name>"` 告警后禁用），配置可正常载入 |

状态列：`✅ 已实现` / `🚧 进行中` / `⏳ 计划` / `❌ 不做`

## A 级（完全等价）

| 模块 | Windows 数据源 | 状态 | 备注 |
|---|---|---|---|
| `clock` | 系统时间 + ICU tzdb | ⏳ | `timezone` 支持 IANA 名；`calendar-format`/`today-format` 保留 |
| `simpleclock` | 同上（无 tz 依赖时的上游回退实现） | ⏳ | 与上游一致按能力选择 |
| `user` | 进程 Token / 环境变量 | ⏳ | `format`/`icon` |
| `disk` | `GetDiskFreeSpaceEx` + `GetVolumeInformation` | ⏳ | POSIX 路径 `/` → 系统盘符 |
| `image` | gdk-pixbuf | ⏳ | `path`/`interval`/`size` |
| `custom/*` | `CreateProcessW`（`cmd.exe`） | ⏳ | 新增可选 `shell` 键；`exec-if`/`return-type`/`signal`/流式输出保留 |
| `custom-graph/*` | 同上 + `AGraph` | ⏳ | |
| `cffi/*` | `LoadLibraryW` | ⏳ | 替代 `dlopen` |
| `group/*` | 纯 GTK | ⏳ | `group/<name>` 与 `drawer` |
| `idle_inhibitor` | `SetThreadExecutionState` | ⏳ | `timeout`/`format-icons`/点击切换 |
| `keyboard-state` | `GetKeyState`（caps/num/scroll） | ⏳ | 比上游 Linux 实现更直接 |
| `cpu` | `GetSystemTimes` / PDH | ⏳ | `states` 阈值语义一致 |
| `cpu_usage` | PDH 进程级 | ⏳ | `interval`/`sort-by` |
| `cpu_graph` | 同上 + `AGraph` | ⏳ | |
| `memory` | `GlobalMemoryStatusEx` | ⏳ | `states` 阈值 |
| `battery` | `GetSystemPowerStatus` + SetupAPI 电池设备 | ⏳（B-） | 多电池聚合；`battery#bat2` 等同主电池 |

## B 级（语义映射）

| 模块 | Windows 数据源 | 状态 | 差异说明 |
|---|---|---|---|
| `load` | PDH `Processor Queue Length` 归一化 | ⏳ | Windows 无 loadavg，数值语义近似（文档化） |
| `cpu_frequency` | `CallNtPowerInformation` | ⏳ | 上游仅 Linux 提供 |
| `network` | WlanAPI + `GetAdaptersAddresses` + `GetIfTable2` | ⏳ | `bandwidth` 用计数器差值；`ifname` 匹配适配器名 |
| `pulseaudio` | Core Audio 默认端点 | ⏳ | `format-muted`/`format-source*`/`scroll-step` 支持；PulseAudio 专有键忽略 |
| `pulseaudio/slider` | Core Audio `IAudioEndpointVolume` | ⏳ | |
| `backlight` | WMI `WmiMonitorBrightness` | ⏳ | 仅内置屏；外接屏 DDC/CI 待定 |
| `backlight/slider` | 同上 + `WmiMonitorBrightnessMethods` | ⏳ | |
| `temperature` | WMI 热区 / LibreHardwareMonitor WMI | ⏳ | 多数台式机无热区 → 运行期降级并告警 |
| `power-profiles-daemon` | `PowerGet/SetActiveScheme` | ⏳ | balanced / high-performance / power-saver |
| `sway/window` | `GetForegroundWindow` + `GetWindowText` | ⏳ | `rewrite` 等规则键不适用 |
| `sway/language` | `GetKeyboardLayout` + `WM_INPUTLANGCHANGE` | ⏳ | |
| `wlr/taskbar` | `EnumWindows` + 图标 + 激活 | ⏳ | `sort-by-app-id` 近似 |
| `upower` | 聚合电池（等价 `battery`） | ⏳ | |

## D 级（不支持，告警禁用）

| 模块 | 原因 |
|---|---|
| `tray` | Windows 不提供外壳托盘宿主 API（通知区域由 Explorer 独占） |
| `sway/workspaces`、`sway/mode`、`sway/scratchpad` | 无对应合成器概念；虚拟桌面需未公开 COM，默认不做 |
| `hyprland/*`、`niri/*`、`mango/*`、`river/*`、`dwl/*`、`wayfire/*`、`ext/workspaces` | 无对应合成器 |
| `mpd`、`sndio`、`jack`、`wireplumber`、`privacy`、`cava`、`inhibitor`、`gamemode`、`systemd-failed-units`、`gps`、`wwan`、`mpris` | 依赖 Linux 专有服务/库 |

> D 级模块**不做假实现**：与上游一样在日志中告警并跳过该模块，保证 bar 其余部分正常工作。

## 可选扩展（P3+，默认关闭）

| 模块 | 说明 |
|---|---|
| `mpris`（SMTC） | 通过 C++/WinRT `GlobalSystemMediaTransportControlsSessionManager` 读取媒体会话 |
| `bluetooth` | SetupAPI `BTHENUM` 枚举 + 蓝牙无线电状态 |
| `mpd` | 最小 MPD 协议客户端（Winsock） |
| 虚拟桌面实验模块 | 依赖未公开 COM，版本敏感，仅实验开关 |
