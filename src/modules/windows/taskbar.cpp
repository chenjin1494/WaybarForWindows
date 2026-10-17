#include "modules/windows/taskbar.hpp"

#include <spdlog/spdlog.h>
#include <dwmapi.h>
#include <windows.h>

#include <algorithm>
#include <string>

#include "win32/common.hpp"

namespace waybar::modules::wlr {

namespace {

struct EnumContext {
  HWND own = nullptr;
  std::vector<Taskbar::WindowEntry>* windows = nullptr;
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

std::string window_class(HWND hwnd) {
  wchar_t buffer[256] = {};
  if (GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer))) <= 0) return {};
  return waybar::win32::to_utf8(buffer);
}

std::string process_name(HWND hwnd) {
  DWORD pid = 0;
  GetWindowThreadProcessId(hwnd, &pid);
  if (pid == 0) return {};
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) return {};
  wchar_t path[MAX_PATH] = {};
  DWORD size = static_cast<DWORD>(std::size(path));
  std::string result;
  if (QueryFullProcessImageNameW(process, 0, path, &size)) {
    result = waybar::win32::to_utf8(std::wstring(path, size));
    const auto pos = result.find_last_of("\\/");
    if (pos != std::string::npos) result = result.substr(pos + 1);
    const auto dot = result.find_last_of('.');
    if (dot != std::string::npos) result = result.substr(0, dot);
  }
  CloseHandle(process);
  return result;
}

BOOL CALLBACK enum_windows(HWND hwnd, LPARAM param) {
  auto* context = reinterpret_cast<EnumContext*>(param);
  if (hwnd == context->own) return TRUE;
  if (!IsWindowVisible(hwnd)) return TRUE;
  if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

  const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
  if ((ex_style & WS_EX_TOOLWINDOW) != 0) return TRUE;

  // 跳过被 DWM 隐藏（cloaked）的窗口，例如 UWP 的后台窗口
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
      cloaked != 0) {
    return TRUE;
  }

  std::string title = window_title(hwnd);
  if (title.empty()) return TRUE;

  Taskbar::WindowEntry entry;
  entry.hwnd = hwnd;
  entry.title = std::move(title);
  entry.window_class = window_class(hwnd);
  entry.app_id = process_name(hwnd);
  context->windows->push_back(std::move(entry));
  return TRUE;
}

std::vector<Taskbar::WindowEntry> enumerate_windows(HWND own) {
  std::vector<Taskbar::WindowEntry> windows;
  EnumContext context{own, &windows};
  EnumWindows(enum_windows, reinterpret_cast<LPARAM>(&context));
  std::sort(windows.begin(), windows.end(),
            [](const auto& a, const auto& b) { return a.hwnd < b.hwnd; });
  return windows;
}

}  // namespace

Taskbar::Taskbar(const std::string& id, const waybar::Bar& bar, const Json::Value& config)
    : AModule(config, "taskbar", id, false, false), bar_(bar) {
  if (config_["format"].isString()) {
    format_ = config_["format"].asString();
  }
  if (config_["max-length"].isInt()) {
    max_length_ = config_["max-length"].asInt();
  }
  if (config_["interval"].isUInt()) {
    interval_ = std::chrono::seconds(std::max<uint32_t>(1, config_["interval"].asUInt()));
  }
  box_.set_name("taskbar");
  box_.get_style_context()->add_class("module");
  box_.get_style_context()->add_class("taskbar");
  event_box_.add(box_);

  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

Taskbar::~Taskbar() { clear(); }

void Taskbar::clear() {
  for (auto* button : buttons_) {
    box_.remove(*button);
    delete button;
  }
  buttons_.clear();
}

void Taskbar::rebuild(const std::vector<WindowEntry>& windows) {
  clear();

  const auto format = format_.empty() ? std::string("{title}") : format_;
  const auto display_title = [this](const std::string& title) -> std::string {
    if (max_length_ <= 0) return title;
    Glib::ustring value(title);
    if (value.size() <= static_cast<size_t>(max_length_)) return title;
    return std::string(value.substr(0, static_cast<size_t>(max_length_))) + "…";
  };
  for (const auto& window : windows) {
    auto* button = new Gtk::Button();
    auto* label = new Gtk::Label();
    try {
      label->set_markup(fmt::format(fmt::runtime(format), fmt::arg("title", display_title(window.title)),
                                    fmt::arg("app_id", window.app_id),
                                    fmt::arg("class", window.window_class)));
    } catch (const fmt::format_error& e) {
      delete label;
      delete button;
      spdlog::warn("taskbar: format error: {}", e.what());
      continue;
    }
    button->add(*label);
    label->show();
    button->set_relief(Gtk::RELIEF_NONE);
    if (tooltipEnabled()) {
      const auto tooltip_format = config_["tooltip-format"].isString()
                                      ? config_["tooltip-format"].asString()
                                      : std::string("{title}\n{app_id}");
      try {
        button->set_tooltip_markup(fmt::format(fmt::runtime(tooltip_format),
                                               fmt::arg("title", window.title),
                                               fmt::arg("app_id", window.app_id),
                                               fmt::arg("class", window.window_class)));
      } catch (const fmt::format_error&) {
      }
    }
    const HWND target = window.hwnd;
    button->signal_clicked().connect([target]() {
      if (IsIconic(target)) {
        ShowWindow(target, SW_RESTORE);
      }
      SetForegroundWindow(target);
    });
    box_.pack_start(*button, false, false, 0);
    button->show();
    buttons_.push_back(button);
  }
  box_.show();
}

auto Taskbar::update() -> void {
  HWND own = bar_.backend().hwnd();
  auto windows = enumerate_windows(own);

  bool changed = windows.size() != last_windows_.size();
  if (!changed) {
    for (size_t i = 0; i < windows.size(); ++i) {
      if (windows[i].hwnd != last_windows_[i].hwnd ||
          windows[i].title != last_windows_[i].title) {
        changed = true;
        break;
      }
    }
  }

  if (changed) {
    rebuild(windows);
    last_windows_ = std::move(windows);
  }

  event_box_.set_visible(!last_windows_.empty());
  AModule::update();
}

}  // namespace waybar::modules::wlr
