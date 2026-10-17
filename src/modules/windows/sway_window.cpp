#include "modules/windows/sway_window.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>
#include <psapi.h>

#include <regex>
#include <string>

#include "win32/common.hpp"

namespace waybar::modules::sway {

namespace {

struct ForegroundInfo {
  bool valid = false;
  std::string title;
  std::string app_id;
  std::string window_class;
};

ForegroundInfo query_foreground(HWND own_window) {
  ForegroundInfo info;
  HWND foreground = GetForegroundWindow();
  if (foreground == nullptr || foreground == own_window) {
    return info;
  }
  if (!IsWindowVisible(foreground)) {
    return info;
  }

  wchar_t buffer[1024] = {};
  const int length = GetWindowTextW(foreground, buffer, static_cast<int>(std::size(buffer)));
  if (length <= 0) {
    return info;
  }
  info.title = waybar::win32::to_utf8(std::wstring(buffer, length));
  if (info.title.empty()) {
    return info;
  }

  wchar_t class_name[256] = {};
  if (GetClassNameW(foreground, class_name, static_cast<int>(std::size(class_name))) > 0) {
    info.window_class = waybar::win32::to_utf8(class_name);
  }

  DWORD pid = 0;
  GetWindowThreadProcessId(foreground, &pid);
  if (pid != 0) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process != nullptr) {
      wchar_t path[MAX_PATH] = {};
      DWORD size = static_cast<DWORD>(std::size(path));
      if (QueryFullProcessImageNameW(process, 0, path, &size)) {
        std::string exe = waybar::win32::to_utf8(std::wstring(path, size));
        const auto pos = exe.find_last_of("\\/");
        info.app_id = pos == std::string::npos ? exe : exe.substr(pos + 1);
        const auto dot = info.app_id.find_last_of('.');
        if (dot != std::string::npos) info.app_id = info.app_id.substr(0, dot);
      }
      CloseHandle(process);
    }
  }

  info.valid = true;
  return info;
}

std::string truncate_utf8(const std::string& text, size_t max_length) {
  if (max_length == 0 || text.size() <= max_length) return text;
  Glib::ustring value(text);
  if (value.size() <= max_length) return text;
  return value.substr(0, max_length) + "…";
}

}  // namespace

Window::Window(const std::string& id, const waybar::Bar& bar, const Json::Value& config)
    : ALabel(config, "window", id, "{title}", 1), bar_(bar) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

std::string Window::rewriteTitle(const std::string& title, const std::string& app_id) const {
  if (!config_["rewrite"].isObject()) return title;

  std::string result = title;
  for (const auto& key : config_["rewrite"].getMemberNames()) {
    if (!config_["rewrite"][key].isString()) continue;
    const std::string replacement = config_["rewrite"][key].asString();
    try {
      const std::regex pattern(key);
      result = std::regex_replace(result, pattern, replacement);
      if (result != title) return result;
    } catch (const std::regex_error& e) {
      spdlog::warn("window: invalid rewrite regex '{}': {}", key, e.what());
    }
  }
  (void)app_id;
  return result;
}

auto Window::update() -> void {
  HWND own = static_cast<HWND>(bar_.backend().hwnd());
  const ForegroundInfo info = query_foreground(own);

  if (!info.valid) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  event_box_.show();

  std::string title = rewriteTitle(info.title, info.app_id);
  if (config_["max-length"].isInt() && config_["max-length"].asInt() > 0) {
    title = truncate_utf8(title, static_cast<size_t>(config_["max-length"].asInt()));
  }

  auto format = format_;
  if (format.empty()) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  setLabelMarkup(fmt::format(fmt::runtime(format), fmt::arg("title", title),
                             fmt::arg("app_id", info.app_id),
                             fmt::arg("class", info.window_class), fmt::arg("shell", "windows")));

  if (tooltipEnabled()) {
    const auto tooltip_format = config_["tooltip-format"].isString()
                                    ? config_["tooltip-format"].asString()
                                    : std::string("{title}\n{app_id}");
    setTooltipMarkup(fmt::format(fmt::runtime(tooltip_format), fmt::arg("title", title),
                                 fmt::arg("app_id", info.app_id),
                                 fmt::arg("class", info.window_class)));
  }

  label_.get_style_context()->remove_class("empty");
  ALabel::update();
}

}  // namespace waybar::modules::sway
