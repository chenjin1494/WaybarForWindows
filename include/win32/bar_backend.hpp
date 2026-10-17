#pragma once

// bar 窗口的 Win32 后端：定位、置顶、工作区预留（AppBar）、鼠标穿透、可见性。
//
// 为什么需要它：gtk-layer-shell 只在 Wayland 可用；Windows 上 GTK 的 gtk_window_move
// 对无边框窗口不可靠，且没有「预留工作区」的概念，必须用 Win32 API 实现。

#include <gtkmm/window.h>
#include <windows.h>

#include <functional>
#include <string>

#include "win32/layout.hpp"

namespace waybar::win32 {

class BarBackend {
 public:
  BarBackend(Gtk::Window& window, const LayoutOptions& options, const MonitorInfo& monitor);
  ~BarBackend();

  BarBackend(const BarBackend&) = delete;
  BarBackend& operator=(const BarBackend&) = delete;

  // 由 Bar 在显示器/配置变化时更新
  void set_monitor(const MonitorInfo& monitor);
  void set_layout(const LayoutOptions& options);
  const LayoutOptions& layout() const { return options_; }

  // 生命周期回调（由 Bar 转发 GTK 信号）
  void on_realize();
  void on_map();
  void on_size_allocate(int width, int height);

  // 重新计算几何并应用（SetWindowPos / AppBar）
  void apply();

  void set_visible(bool visible);
  void set_passthrough(bool passthrough);
  void set_exclusive(bool exclusive);
  void set_topmost(bool topmost);

  bool visible() const { return visible_; }
  bool passthrough() const { return passthrough_; }
  bool exclusive() const { return exclusive_; }
  bool topmost() const { return topmost_; }

  HWND hwnd() const { return hwnd_; }
  Rect current_rect() const { return current_; }
  int content_width() const { return content_width_; }
  int content_height() const { return content_height_; }

  // 显示器/分辨率变化时由 Bar 设置的回调（例如重新读取 GdkMonitor 几何）
  void set_geometry_changed_callback(std::function<void()> callback);

 private:
  void install_window_hook();
  void remove_window_hook();
  void apply_window_styles();
  void update_size_and_position();
  void register_appbar();
  void unregister_appbar();
  void update_appbar_position();
  void refresh_content_size();

  LRESULT handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

  static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  static void register_backend(HWND hwnd, BarBackend* backend);
  static void unregister_backend(HWND hwnd);

  Gtk::Window& window_;
  LayoutOptions options_;
  MonitorInfo monitor_;

  HWND hwnd_ = nullptr;
  WNDPROC original_proc_ = nullptr;
  UINT taskbar_created_message_ = 0;

  bool appbar_registered_ = false;
  bool visible_ = true;
  bool passthrough_ = false;
  bool exclusive_ = false;
  bool topmost_ = false;
  bool styles_applied_ = false;

  Rect current_;
  int content_width_ = 0;
  int content_height_ = 0;
  bool measured_ = false;

  std::function<void()> geometry_changed_callback_;
};

}  // namespace waybar::win32
