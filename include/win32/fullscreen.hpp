#pragma once

#include <windows.h>

#include <vector>

namespace waybar::win32 {

// 检测“前台存在全屏应用”：
//   1) SHQueryUserNotificationState：覆盖 D3D 独占全屏、演示模式、全屏视频等；
//   2) 几何兜底：前台窗口（DWM 扩展边框）铺满所在显示器且不是 shell/任务栏/我们的 bar。
//
// own_windows 传入所有 waybar 自己的顶层 HWND，用于排除误判。
bool foreground_is_fullscreen(const std::vector<HWND>& own_windows);

// 判断指定窗口是否处于“全屏”状态（供进入全屏后保持隐藏状态使用）
bool window_is_fullscreen(HWND window, const std::vector<HWND>& own_windows);

}  // namespace waybar::win32
