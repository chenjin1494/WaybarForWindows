#include "win32/theme.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <atomic>
#include <thread>

#include "util/SafeSignal.hpp"
#include "win32/common.hpp"

namespace waybar::win32 {

namespace {

constexpr const wchar_t* kPersonalizeKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr const wchar_t* kAppsUseLightTheme = L"AppsUseLightTheme";

}  // namespace

bool is_light_theme() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
    return true;  // 读不到时按浅色处理（与系统默认一致）
  }
  DWORD value = 1;
  DWORD size = sizeof(value);
  DWORD type = 0;
  const LONG status =
      RegQueryValueExW(key, kAppsUseLightTheme, nullptr, &type, reinterpret_cast<LPBYTE>(&value),
                       &size);
  RegCloseKey(key);
  if (status != ERROR_SUCCESS || type != REG_DWORD) return true;
  return value != 0;
}

struct ThemeWatcher::Impl {
  waybar::SafeSignal<bool> changed;
  std::thread thread;
  std::atomic<bool> running{false};
  HANDLE stop_event = nullptr;
  Callback callback;
};

ThemeWatcher::ThemeWatcher() : impl_(std::make_unique<Impl>()) {}

ThemeWatcher::~ThemeWatcher() { stop(); }

void ThemeWatcher::start(Callback callback) {
  if (impl_->running.exchange(true)) {
    impl_->callback = std::move(callback);
    return;
  }
  impl_->callback = std::move(callback);
  impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (impl_->stop_event == nullptr) {
    spdlog::warn("[theme] CreateEvent failed: {}", last_error());
    impl_->running = false;
    return;
  }

  impl_->changed.connect([this](bool light) {
    if (impl_->callback) impl_->callback(light);
  });

  impl_->thread = std::thread([this] {
    Impl* impl = impl_.get();
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kPersonalizeKey, 0,
                      KEY_NOTIFY | KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
      spdlog::warn("[theme] cannot open personalize key for watching");
      return;
    }
    while (impl->running.load()) {
      HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
      if (event == nullptr) break;
      const LONG status = RegNotifyChangeKeyValue(key, TRUE, REG_NOTIFY_CHANGE_LAST_SET, event,
                                                  TRUE);
      if (status != ERROR_SUCCESS) {
        CloseHandle(event);
        break;
      }
      HANDLE handles[2] = {impl->stop_event, event};
      const DWORD wait = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
      CloseHandle(event);
      if (wait != WAIT_OBJECT_0 + 1) break;  // stop 或被中断
      impl->changed.emit(is_light_theme());
    }
    RegCloseKey(key);
  });
}

void ThemeWatcher::stop() {
  if (!impl_ || !impl_->running.exchange(false)) return;
  if (impl_->stop_event != nullptr) SetEvent(impl_->stop_event);
  if (impl_->thread.joinable()) impl_->thread.join();
  if (impl_->stop_event != nullptr) {
    CloseHandle(impl_->stop_event);
    impl_->stop_event = nullptr;
  }
}

}  // namespace waybar::win32
