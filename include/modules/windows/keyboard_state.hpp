#pragma once

#include <fmt/chrono.h>
#include <gtkmm/label.h>

#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

#include "AModule.hpp"
#include "bar.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 keyboard-state：用 GetKeyState（配合 AttachThreadInput 到前台窗口线程，
// 以便拿到真实的锁定状态）显示 NumLock / CapsLock / ScrollLock。
// 配置项与上游一致：numlock/capslock/scrolllock、format、format-icons、interval。
class KeyboardState : public AModule {
 public:
  KeyboardState(const std::string&, const waybar::Bar&, const Json::Value&);
  ~KeyboardState() override = default;
  auto update() -> void override;

 private:
  static bool isCommonFormatIcons(const Json::Value& config);

  Gtk::Box box_;
  Gtk::Label numlock_label_;
  Gtk::Label capslock_label_;
  Gtk::Label scrolllock_label_;

  std::string numlock_format_;
  std::string capslock_format_;
  std::string scrolllock_format_;
  const std::chrono::seconds interval_;
  std::unordered_map<std::string, std::vector<std::string>> key_icon_states_;

  util::SleeperThread thread_;
};

}  // namespace waybar::modules
