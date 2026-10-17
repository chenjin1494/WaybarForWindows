#include "modules/windows/tray.hpp"

#include <dwmapi.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include <string>
#include <vector>

#include "win32/common.hpp"
#include "win32/exec.hpp"

namespace waybar::modules {

namespace {

struct WindowInfo {
  HWND hwnd;
  std::string title;
};

std::string window_title(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  if (length <= 0) return {};
  std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(hwnd, buffer.data(), static_cast<int>(buffer.size()));
  if (copied <= 0) return {};
  buffer.resize(static_cast<size_t>(copied));
  return waybar::win32::to_utf8(buffer);
}

BOOL CALLBACK enum_windows(HWND hwnd, LPARAM param) {
  auto* windows = reinterpret_cast<std::vector<WindowInfo>*>(param);
  if (!IsWindowVisible(hwnd)) return TRUE;
  if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
  if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) return TRUE;
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
      cloaked != 0) {
    return TRUE;
  }
  wchar_t class_name[128] = {};
  GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name)));
  if (wcscmp(class_name, L"Progman") == 0 || wcscmp(class_name, L"WorkerW") == 0 ||
      wcscmp(class_name, L"Shell_TrayWnd") == 0) {
    return TRUE;
  }
  std::string title = window_title(hwnd);
  if (title.empty()) return TRUE;
  windows->push_back({hwnd, std::move(title)});
  return TRUE;
}

std::vector<WindowInfo> collect_windows() {
  std::vector<WindowInfo> windows;
  EnumWindows(enum_windows, reinterpret_cast<LPARAM>(&windows));
  return windows;
}

}  // namespace

Tray::Tray(const std::string& id, const Json::Value& config)
    : AModule(config, "tray", id, true, false) {
  format_ = config_["format"].isString() ? config_["format"].asString() : "\uF00A";
  label_.set_name("tray");
  label_.get_style_context()->add_class(id);
  label_.get_style_context()->add_class(MODULE_CLASS);
  label_.set_markup(format_);
  label_.show();
  event_box_.add(label_);

  list_.set_orientation(Gtk::ORIENTATION_VERTICAL);
  list_.set_spacing(2);
  list_.set_border_width(4);
  list_.show();
  // 用普通无边框窗口代替 Gtk::Menu/Popover：NOACTIVATE 的 bar 窗口上
  // GTK 的 grab 弹窗无法显示，独立窗口可以正常工作。
  popup_.set_decorated(false);
  popup_.set_skip_taskbar_hint(true);
  popup_.set_skip_pager_hint(true);
  popup_.set_keep_above(true);
  popup_.set_type_hint(Gdk::WINDOW_TYPE_HINT_POPUP_MENU);
  popup_.set_resizable(false);
  popup_.get_style_context()->add_class("tray-popup");
  popup_.add(list_);
  popup_.signal_focus_out_event().connect([this](GdkEventFocus*) {
    popup_.hide();
    return false;
  });
}

void Tray::update() {
  const auto windows = collect_windows();
  label_.set_markup(format_);
  if (tooltipEnabled()) {
    event_box_.set_tooltip_text("后台任务（" + std::to_string(windows.size()) +
                                " 个窗口）\n左键：窗口列表\n右键：任务管理器");
  }
}

void Tray::rebuildList() {
  for (auto* child : list_.get_children()) {
    list_.remove(*child);
    delete child;
  }

  const auto windows = collect_windows();
  for (const auto& window : windows) {
    std::string label = window.title;
    if (label.size() > 48) {
      label = label.substr(0, 45) + "...";
    }
    auto* button = new Gtk::Button(label);
    button->set_relief(Gtk::RELIEF_NONE);
    button->set_halign(Gtk::ALIGN_START);
    button->get_style_context()->add_class("flat");
    const HWND target = window.hwnd;
    button->signal_clicked().connect([this, target] {
      popup_.hide();
      if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
      }
      SetForegroundWindow(target);
    });
    button->show();
    list_.pack_start(*button, false, false, 0);
  }
  if (windows.empty()) {
    auto* label = new Gtk::Label("（没有可切换的窗口）");
    label->show();
    list_.pack_start(*label, false, false, 0);
  }
  list_.show_all();
}

bool Tray::handleToggle(GdkEventButton* const& event) {
  if (event->button == 1) {
    rebuildList();
    popup_.show_all();
    int x = 0;
    int y = 0;
    if (auto window = event_box_.get_window()) {
      window->get_origin(x, y);
      x += event_box_.get_allocated_width() - popup_.get_allocated_width();
      y += event_box_.get_allocated_height() + 2;
    } else {
      auto display = Gdk::Display::get_default();
      auto seat = display->get_default_seat();
      auto pointer = seat->get_pointer();
      pointer->get_position(x, y);
      y += 4;
    }
    popup_.move(x, y);
    popup_.present();
    return true;
  }
  if (event->button == 3) {
    win32::run_detached("cmd /d /s /c start taskmgr.exe", "");
    return true;
  }
  return false;
}

}  // namespace waybar::modules
