#include "win32/fullscreen.hpp"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>

namespace waybar::win32 {

namespace {

bool is_own_window(HWND window, const std::vector<HWND>& own_windows) {
  return std::find(own_windows.begin(), own_windows.end(), window) != own_windows.end();
}

bool is_shell_window(HWND window) {
  wchar_t class_name[128] = {};
  if (GetClassNameW(window, class_name, static_cast<int>(std::size(class_name))) <= 0) {
    return false;
  }
  return wcscmp(class_name, L"Progman") == 0 || wcscmp(class_name, L"WorkerW") == 0 ||
         wcscmp(class_name, L"Shell_TrayWnd") == 0 ||
         wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0 ||
         wcscmp(class_name, L"Windows.UI.Core.CoreWindow") == 0 ||
         wcscmp(class_name, L"ApplicationFrameWindow") == 0;
}

// 前台窗口是否铺满其所在显示器（含任务栏区域）
bool covers_monitor(HWND window) {
  RECT bounds{};
  if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &bounds,
                                   sizeof(bounds))) &&
      !GetWindowRect(window, &bounds)) {
    return false;
  }

  HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return false;
  }

  const RECT& target = info.rcMonitor;
  constexpr LONG kTolerance = 2;
  return bounds.left <= target.left + kTolerance && bounds.top <= target.top + kTolerance &&
         bounds.right >= target.right - kTolerance &&
         bounds.bottom >= target.bottom - kTolerance;
}

}  // namespace

bool window_is_fullscreen(HWND window, const std::vector<HWND>& own_windows) {
  if (window == nullptr || !IsWindow(window) || !IsWindowVisible(window)) {
    return false;
  }
  if (is_own_window(window, own_windows) || is_shell_window(window)) {
    return false;
  }
  if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
    return false;
  }
  // 只把“无边框且铺满显示器”的窗口当作全屏；带标题栏的最大化窗口在任务栏自动隐藏时
  // 也会铺满显示器，不应触发收起。
  if ((GetWindowLongPtrW(window, GWL_STYLE) & WS_CAPTION) != 0) {
    return false;
  }
  return covers_monitor(window);
}

bool foreground_is_fullscreen(const std::vector<HWND>& own_windows) {
  // 1) 官方 shell 状态：游戏独占全屏、演示模式、全屏视频时返回 QUNS_BUSY 等
  QUERY_USER_NOTIFICATION_STATE state = QUNS_NOT_PRESENT;
  if (SUCCEEDED(SHQueryUserNotificationState(&state))) {
    if (state == QUNS_BUSY || state == QUNS_RUNNING_D3D_FULL_SCREEN ||
        state == QUNS_PRESENTATION_MODE) {
      return true;
    }
  }

  // 2) 几何兜底（无边框全屏窗口，例如浏览器 F11、视频播放器）
  return window_is_fullscreen(GetForegroundWindow(), own_windows);
}

}  // namespace waybar::win32
