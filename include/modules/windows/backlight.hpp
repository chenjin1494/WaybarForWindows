#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <string>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 backlight：WMI WmiMonitorBrightness / WmiMonitorBrightnessMethods。
// 仅内置屏可用；外接显示器通常不支持，此时模块隐藏。
class Backlight : public ALabel {
 public:
  Backlight(const std::string&, const Json::Value&);
  virtual ~Backlight() = default;
  auto update() -> void override;

 private:
  bool handleScroll(GdkEventScroll* e) override;

  bool setBrightness(double value);

  util::SleeperThread thread_;
  int scroll_step_;
  int min_brightness_;
};

}  // namespace waybar::modules
