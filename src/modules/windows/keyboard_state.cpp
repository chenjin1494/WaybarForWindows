#include "modules/windows/keyboard_state.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <array>

namespace waybar::modules {

bool KeyboardState::isCommonFormatIcons(const Json::Value& config) {
  return config["format-icons"].isObject() && (config["format-icons"]["locked"].isString() ||
                                               config["format-icons"]["unlocked"].isString());
}

KeyboardState::KeyboardState(const std::string& id, const waybar::Bar& bar,
                             const Json::Value& config)
    : AModule(config, "keyboard-state", id, false, !config["disable-scroll"].asBool()),
      numlock_format_(config_["format"].isString() ? config_["format"].asString() : "{name} {icon}"),
      capslock_format_(config_["format"].isString() ? config_["format"].asString()
                                                    : "{name} {icon}"),
      scrolllock_format_(config_["format"].isString() ? config_["format"].asString()
                                                      : "{name} {icon}"),
      interval_(std::chrono::seconds(config_["interval"].isUInt() ? config_["interval"].asUInt()
                                                                  : 1)) {
  if (config_["format"].isObject()) {
    if (config_["format"]["numlock"].isString()) {
      numlock_format_ = config_["format"]["numlock"].asString();
    }
    if (config_["format"]["capslock"].isString()) {
      capslock_format_ = config_["format"]["capslock"].asString();
    }
    if (config_["format"]["scrolllock"].isString()) {
      scrolllock_format_ = config_["format"]["scrolllock"].asString();
    }
  }

  box_.set_name("keyboard-state");
  box_.get_style_context()->add_class("module");
  box_.get_style_context()->add_class("keyboard-state");

  const auto& format_icons = config["format-icons"];
  if (format_icons.isObject()) {
    key_icon_states_["Lock"] = {format_icons["unlocked"].isString()
                                    ? format_icons["unlocked"].asString()
                                    : std::string(),
                                format_icons["locked"].isString() ? format_icons["locked"].asString()
                                                                  : std::string()};
    for (const auto* name : {"numlock", "capslock", "scrolllock"}) {
      if (format_icons[name].isObject()) {
        key_icon_states_[name] = {
            format_icons[name]["unlocked"].isString() ? format_icons[name]["unlocked"].asString()
                                                      : std::string(),
            format_icons[name]["locked"].isString() ? format_icons[name]["locked"].asString()
                                                    : std::string()};
      }
    }
  }

  if (!config_["numlock"].asBool()) {
    numlock_format_.clear();
  }
  if (!config_["capslock"].asBool()) {
    capslock_format_.clear();
  }
  if (!config_["scrolllock"].asBool()) {
    scrolllock_format_.clear();
  }

  if (numlock_format_.empty() && capslock_format_.empty() && scrolllock_format_.empty()) {
    spdlog::warn("keyboard-state: no lock keys enabled, hiding module");
  }

  box_.pack_start(numlock_label_, false, false, 0);
  box_.pack_start(capslock_label_, false, false, 0);
  box_.pack_start(scrolllock_label_, false, false, 0);
  box_.show_all();

  event_box_.add(box_);

  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

auto KeyboardState::update() -> void {
  int numl = 0;
  int capsl = 0;
  int scrolll = 0;

  // GetKeyState 反映的是“调用线程”的键盘状态；先 attach 到前台窗口线程，
  // 才能读到真实的按键锁定状态（NumLock/CapsLock/ScrollLock）。
  const HWND foreground = GetForegroundWindow();
  const DWORD foreground_thread =
      foreground != nullptr ? GetWindowThreadProcessId(foreground, nullptr) : 0;
  const DWORD current_thread = GetCurrentThreadId();
  const bool attached = foreground_thread != 0 && foreground_thread != current_thread &&
                        AttachThreadInput(current_thread, foreground_thread, TRUE);

  numl = (GetKeyState(VK_NUMLOCK) & 1) != 0;
  capsl = (GetKeyState(VK_CAPITAL) & 1) != 0;
  scrolll = (GetKeyState(VK_SCROLL) & 1) != 0;

  if (attached) {
    AttachThreadInput(current_thread, foreground_thread, FALSE);
  }

  struct {
    bool state;
    Gtk::Label& label;
    const std::string& format;
    const std::string name;
  } label_states[] = {
      {numl != 0, numlock_label_, numlock_format_, "Num"},
      {capsl != 0, capslock_label_, capslock_format_, "Caps"},
      {scrolll != 0, scrolllock_label_, scrolllock_format_, "Scroll"},
  };

  for (auto& label_state : label_states) {
    if (label_state.format.empty()) {
      continue;
    }
    std::string text;
    std::string map_key = isCommonFormatIcons(config_) ? "Lock" : label_state.name;

    if (key_icon_states_.find(map_key) == key_icon_states_.end()) {
      spdlog::warn("keyboard-state: Missing icon configuration for '{}'", map_key);
      continue;
    }

    auto& icons = key_icon_states_[map_key];
    if (icons.size() < 2) {
      spdlog::warn("keyboard-state: Invalid icon vector size for '{}'", map_key);
      continue;
    }

    text = fmt::format(fmt::runtime(label_state.format),
                       fmt::arg("icon", label_state.state ? icons[1] : icons[0]),
                       fmt::arg("name", label_state.name));
    label_state.label.set_markup(text);
    if (label_state.state) {
      label_state.label.get_style_context()->add_class("locked");
    } else {
      label_state.label.get_style_context()->remove_class("locked");
    }
  }

  AModule::update();
}

}  // namespace waybar::modules
