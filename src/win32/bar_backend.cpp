#include "win32/bar_backend.hpp"

#include <gdk/gdkwin32.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "win32/common.hpp"

namespace waybar::win32 {

namespace {

// HWND -> BarBackend 映射。不使用 GWLP_USERDATA，因为 GTK 自己也在用它。
std::mutex& registry_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<HWND, BarBackend*>& registry() {
  static std::unordered_map<HWND, BarBackend*> map;
  return map;
}

constexpr UINT kAppBarCallback = WM_APP + 0x51;

}  // namespace

BarBackend::BarBackend(Gtk::Window& window, const LayoutOptions& options,
                       const MonitorInfo& monitor)
    : window_(window), options_(options), monitor_(monitor) {
  taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
  // 初始状态：可见、不置顶、不穿透、不预留（由 Bar 通过 setter 覆盖）
}

BarBackend::~BarBackend() {
  unregister_appbar();
  remove_window_hook();
}

void BarBackend::set_monitor(const MonitorInfo& monitor) { monitor_ = monitor; }

void BarBackend::set_layout(const LayoutOptions& options) {
  options_ = options;
  measured_ = false;
}

void BarBackend::on_realize() {
  if (hwnd_ != nullptr) return;
  GdkWindow* gdk_window = window_.get_window() ? window_.get_window()->gobj() : nullptr;
  if (gdk_window == nullptr) {
    spdlog::debug("[win32] bar window not realized yet, deferring");
    return;
  }
  hwnd_ = static_cast<HWND>(gdk_win32_window_get_handle(gdk_window));
  if (hwnd_ == nullptr) {
    spdlog::warn("[win32] failed to obtain HWND");
    return;
  }
  register_backend(hwnd_, this);
  install_window_hook();
  apply_window_styles();
  spdlog::debug("[win32] bar hwnd={} output={}", static_cast<void*>(hwnd_),
                monitor_.identifier.empty() ? monitor_.name : monitor_.identifier);
}

void BarBackend::on_map() {
  on_realize();
  if (hwnd_ == nullptr) return;
  apply_window_styles();
  apply();
}

void BarBackend::on_size_allocate(int width, int height) {
  if (width > 0) content_width_ = width;
  if (height > 0) content_height_ = height;
  measured_ = true;
}

void BarBackend::refresh_content_size() {
  if (measured_) return;
  int min_height = 0;
  int natural_height = 0;
  window_.get_preferred_height(min_height, natural_height);
  int min_width = 0;
  int natural_width = 0;
  window_.get_preferred_width(min_width, natural_width);
  if (natural_height > 0) content_height_ = natural_height;
  if (natural_width > 0) content_width_ = natural_width;
  measured_ = true;
}

void BarBackend::apply() {
  if (hwnd_ == nullptr) {
    on_realize();
    if (hwnd_ == nullptr) return;
  }
  refresh_content_size();
  update_size_and_position();
  apply_window_styles();

  if (exclusive_ && visible_) {
    register_appbar();
    update_appbar_position();
  } else {
    unregister_appbar();
  }
}

void BarBackend::update_size_and_position() {
  LayoutResult layout =
      compute_layout(monitor_, options_, std::max(1, content_width_), std::max(1, content_height_));

  // GTK 会强制窗口不小于内容的最小尺寸；若配置值更小，按内容尺寸放大以免文字被裁切
  // （Bar 自身已按上游行为打印 "Requested height ... is less than the minimum" 警告）。
  if (!layout.vertical && options_.height > 1 && content_height_ > layout.height) {
    layout.height = content_height_;
    if (options_.position == "bottom") {
      layout.y = monitor_.geometry.y + monitor_.geometry.height - layout.height -
                 std::max(0, options_.margin_bottom);
    }
  }
  if (layout.vertical && options_.width > 1 && content_width_ > layout.width) {
    layout.width = content_width_;
    if (options_.position == "right") {
      layout.x = monitor_.geometry.x + monitor_.geometry.width - layout.width -
                 std::max(0, options_.margin_right);
    }
  }

  current_.x = layout.x;
  current_.y = layout.y;
  current_.width = layout.width;
  current_.height = layout.height;

  HWND insert_after = topmost_ ? HWND_TOPMOST : HWND_NOTOPMOST;
  const UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOCOPYBITS |
                     (styles_applied_ ? 0 : SWP_FRAMECHANGED);

  if (!SetWindowPos(hwnd_, insert_after, current_.x, current_.y, current_.width, current_.height,
                    flags)) {
    spdlog::warn("[win32] SetWindowPos failed: {}", last_error());
  }
  spdlog::debug("[win32] bar rect=({},{} {}x{}) topmost={} exclusive={} passthrough={}",
                current_.x, current_.y, current_.width, current_.height, topmost_, exclusive_,
                passthrough_);
}

void BarBackend::apply_window_styles() {
  if (hwnd_ == nullptr) return;

  LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_STYLE);
  style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX |
             WS_OVERLAPPEDWINDOW);
  style |= WS_POPUP;
  SetWindowLongPtrW(hwnd_, GWL_STYLE, style);

  LONG_PTR exstyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  exstyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
  if (passthrough_) {
    exstyle |= WS_EX_TRANSPARENT;
  } else {
    exstyle &= ~WS_EX_TRANSPARENT;
  }
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exstyle);

  // 保证分层窗口：GTK 在使用 RGBA visual 时会设置，但某些驱动/主题下不会
  if ((exstyle & WS_EX_LAYERED) == 0) {
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
  }

  styles_applied_ = true;
}

void BarBackend::set_visible(bool visible) {
  if (visible_ == visible) return;
  visible_ = visible;
  // 与上游 "invisible" 模式一致：保持窗口存在（模块继续更新），仅隐藏内容并穿透
  window_.set_opacity(visible ? 1.0 : 0.0);
  apply();
}

void BarBackend::set_passthrough(bool passthrough) {
  if (passthrough_ == passthrough) return;
  passthrough_ = passthrough;
  if (hwnd_ != nullptr) apply_window_styles();
}

void BarBackend::set_exclusive(bool exclusive) {
  if (exclusive_ == exclusive) return;
  exclusive_ = exclusive;
  apply();
}

void BarBackend::set_topmost(bool topmost) {
  if (topmost_ == topmost) return;
  topmost_ = topmost;
  if (hwnd_ != nullptr) update_size_and_position();
}

void BarBackend::set_geometry_changed_callback(std::function<void()> callback) {
  geometry_changed_callback_ = std::move(callback);
}

// ---------------------------------------------------------------- AppBar

void BarBackend::register_appbar() {
  if (appbar_registered_ || hwnd_ == nullptr) return;
  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  abd.hWnd = hwnd_;
  abd.uCallbackMessage = kAppBarCallback;
  if (SHAppBarMessage(ABM_NEW, &abd) == 0) {
    spdlog::warn("[win32] SHAppBarMessage(ABM_NEW) failed: {}", last_error());
    return;
  }
  appbar_registered_ = true;
  spdlog::debug("[win32] AppBar registered for {}", monitor_.identifier);
}

void BarBackend::unregister_appbar() {
  if (!appbar_registered_ || hwnd_ == nullptr) return;
  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  abd.hWnd = hwnd_;
  SHAppBarMessage(ABM_REMOVE, &abd);
  appbar_registered_ = false;
  spdlog::debug("[win32] AppBar removed for {}", monitor_.identifier);
}

void BarBackend::update_appbar_position() {
  if (!appbar_registered_ || hwnd_ == nullptr) return;

  const Rect& geo = monitor_.geometry;
  APPBARDATA abd{};
  abd.cbSize = sizeof(abd);
  abd.hWnd = hwnd_;

  const LayoutResult layout = compute_layout(monitor_, options_, std::max(1, content_width_),
                                             std::max(1, content_height_));
  // 厚度以实际窗口尺寸为准（update_size_and_position 可能因内容最小尺寸而放大）
  const int thickness = layout.vertical ? std::max(current_.width, layout.width)
                                        : std::max(current_.height, layout.height);

  abd.rc.left = geo.x;
  abd.rc.right = geo.x + geo.width;
  abd.rc.top = geo.y;
  abd.rc.bottom = geo.y + geo.height;

  switch (layout.edge) {
    case Edge::Bottom:
      abd.uEdge = ABE_BOTTOM;
      abd.rc.top = geo.y + geo.height - thickness - std::max(0, options_.margin_bottom);
      abd.rc.bottom = geo.height + geo.y - std::max(0, options_.margin_bottom);
      break;
    case Edge::Left:
      abd.uEdge = ABE_LEFT;
      abd.rc.right = geo.x + thickness + std::max(0, options_.margin_left);
      abd.rc.left = geo.x + std::max(0, options_.margin_left);
      break;
    case Edge::Right:
      abd.uEdge = ABE_RIGHT;
      abd.rc.left = geo.x + geo.width - thickness - std::max(0, options_.margin_right);
      abd.rc.right = geo.x + geo.width - std::max(0, options_.margin_right);
      break;
    case Edge::Top:
    default:
      abd.uEdge = ABE_TOP;
      abd.rc.top = geo.y + std::max(0, options_.margin_top);
      abd.rc.bottom = geo.y + thickness + std::max(0, options_.margin_top);
      break;
  }

  SHAppBarMessage(ABM_QUERYPOS, &abd);

  // 依据查询结果重新贴边，保证厚度不被相邻 appbar 撑开
  switch (layout.edge) {
    case Edge::Bottom:
      abd.rc.top = abd.rc.bottom - thickness;
      break;
    case Edge::Top:
      abd.rc.bottom = abd.rc.top + thickness;
      break;
    case Edge::Left:
      abd.rc.right = abd.rc.left + thickness;
      break;
    case Edge::Right:
      abd.rc.left = abd.rc.right - thickness;
      break;
  }

  if (SHAppBarMessage(ABM_SETPOS, &abd) == 0) {
    spdlog::warn("[win32] SHAppBarMessage(ABM_SETPOS) failed: {}", last_error());
  }
}

// ---------------------------------------------------------------- 消息钩子

void BarBackend::register_backend(HWND hwnd, BarBackend* backend) {
  std::lock_guard<std::mutex> lock(registry_mutex());
  registry()[hwnd] = backend;
}

void BarBackend::unregister_backend(HWND hwnd) {
  std::lock_guard<std::mutex> lock(registry_mutex());
  registry().erase(hwnd);
}

void BarBackend::install_window_hook() {
  if (hwnd_ == nullptr || original_proc_ != nullptr) return;
  original_proc_ = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BarBackend::window_proc)));
  if (original_proc_ == nullptr) {
    spdlog::warn("[win32] failed to subclass bar window: {}", last_error());
  }
}

void BarBackend::remove_window_hook() {
  if (hwnd_ == nullptr || original_proc_ == nullptr) return;
  SetWindowLongPtrW(hwnd_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_proc_));
  original_proc_ = nullptr;
}

LRESULT CALLBACK BarBackend::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  BarBackend* backend = nullptr;
  {
    std::lock_guard<std::mutex> lock(registry_mutex());
    auto it = registry().find(hwnd);
    if (it != registry().end()) backend = it->second;
  }
  if (backend != nullptr) {
    return backend->handle_message(hwnd, message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT BarBackend::handle_message(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  auto call_original = [&]() -> LRESULT {
    if (original_proc_ != nullptr) {
      return CallWindowProcW(original_proc_, hwnd, message, wparam, lparam);
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
  };

  // Explorer 重启后 AppBar 注册会丢失，需要重新注册
  if (taskbar_created_message_ != 0 && message == taskbar_created_message_) {
    spdlog::info("[win32] Explorer restarted, re-registering AppBar");
    appbar_registered_ = false;
    apply();
    return call_original();
  }

  if (message == kAppBarCallback) {
    if (wparam == ABN_POSCHANGED) {
      // 其他 appbar 变化：重新查询位置，避免覆盖
      update_appbar_position();
    }
    return call_original();
  }

  switch (message) {
    case WM_DISPLAYCHANGE:
      spdlog::debug("[win32] WM_DISPLAYCHANGE");
      if (geometry_changed_callback_) geometry_changed_callback_();
      measured_ = false;
      apply();
      break;
    case WM_DPICHANGED:
      spdlog::debug("[win32] WM_DPICHANGED");
      if (geometry_changed_callback_) geometry_changed_callback_();
      apply();
      break;
    default:
      break;
  }

  return call_original();
}

}  // namespace waybar::win32
