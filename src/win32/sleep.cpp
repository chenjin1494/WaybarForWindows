#include "win32/sleep.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include "win32/common.hpp"

// 实现说明：MSYS2 的 headers 未提供 DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS
// （PowerRegisterSuspendResumeNotification 的新式回调签名），因此改用经典的
// 隐藏顶层窗口接收 WM_POWERBROADCAST（PBT_APMSUSPEND / PBT_APMRESUMEAUTOMATIC）。
// 注意：消息仅广播到顶层窗口，message-only 窗口收不到，所以这里创建隐藏的 WS_POPUP 窗口。

namespace waybar::win32 {

namespace {

waybar::SafeSignal<bool>& signal_instance() {
  static waybar::SafeSignal<bool> signal;
  return signal;
}

constexpr const wchar_t* kWindowClass = L"WaybarPowerNotifyWindow";
HANDLE g_power_window = nullptr;

LRESULT CALLBACK power_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_POWERBROADCAST) {
    switch (wparam) {
      case PBT_APMSUSPEND:
        signal_instance().emit(true);
        return TRUE;
      case PBT_APMRESUMEAUTOMATIC:
      case PBT_APMRESUMESUSPEND:
        signal_instance().emit(false);
        return TRUE;
      default:
        break;
    }
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

}  // namespace

void init_power_notification() {
  static bool initialized = false;
  if (initialized) return;
  initialized = true;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &power_window_proc;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = kWindowClass;
  if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    spdlog::warn("[sleep] RegisterClassEx failed: {}", last_error());
    return;
  }

  HWND hwnd = CreateWindowExW(0, kWindowClass, L"waybar-power", 0, 0, 0, 0, 0, nullptr, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
  if (hwnd == nullptr) {
    spdlog::warn("[sleep] CreateWindowEx failed: {}", last_error());
    return;
  }
  g_power_window = hwnd;  // 进程生命周期内保持
  spdlog::debug("[sleep] suspend/resume notifications registered");
}

waybar::SafeSignal<bool>& suspend_signal() {
  init_power_notification();
  return signal_instance();
}

}  // namespace waybar::win32
