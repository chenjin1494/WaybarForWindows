#pragma once

// 系统明暗主题检测（替代上游的 XDG desktop portal）。

#include <functional>
#include <memory>

namespace waybar::win32 {

// true = 浅色主题（AppsUseLightTheme != 0）
bool is_light_theme();

// 监听主题变化；回调在主线程（GLib main loop）执行。
class ThemeWatcher {
 public:
  using Callback = std::function<void(bool light)>;

  ThemeWatcher();
  ~ThemeWatcher();

  ThemeWatcher(const ThemeWatcher&) = delete;
  ThemeWatcher& operator=(const ThemeWatcher&) = delete;

  void start(Callback callback);
  void stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace waybar::win32
