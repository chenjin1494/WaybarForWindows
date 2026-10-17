#pragma once

#include <fmt/format.h>

#include <string>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules::sway {

// Windows 版 sway/language：由前台窗口线程的键盘布局推导当前输入法语言。
// 配置项：format（{short}/{long}/{layout}）、format-icons、tooltip-format。
class Language : public ALabel {
 public:
  Language(const std::string&, const Json::Value&);
  virtual ~Language() = default;
  auto update() -> void override;

 private:
  struct LayoutInfo {
    std::string short_code;
    std::string long_name;
    std::string klid;
  };

  LayoutInfo query_layout() const;

  util::SleeperThread thread_;
};

}  // namespace waybar::modules::sway
