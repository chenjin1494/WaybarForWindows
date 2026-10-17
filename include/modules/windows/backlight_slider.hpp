#pragma once

#include <chrono>
#include <string>

#include "ASlider.hpp"

namespace waybar::modules {

// Windows 版 backlight/slider：滑块直接写入 WMI 亮度。
class BacklightSlider : public ASlider {
 public:
  BacklightSlider(const std::string&, const Json::Value&);
  virtual ~BacklightSlider() = default;

  void update() override;
  void onValueChanged() override;

 private:
  std::chrono::milliseconds interval_;
  bool updating_ = false;
};

}  // namespace waybar::modules
