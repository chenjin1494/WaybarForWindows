#include "modules/windows/backlight_slider.hpp"

#include <algorithm>
#include <cmath>

#include "win32/wmi.hpp"

namespace waybar::modules {

BacklightSlider::BacklightSlider(const std::string& id, const Json::Value& config)
    : ASlider(config, "backlight/slider", id),
      interval_(std::chrono::milliseconds(config_["interval"].isUInt()
                                              ? config_["interval"].asUInt() * 1000
                                              : 5000)) {
  uint32_t percent = 0;
  if (waybar::win32::wmi_brightness_query(percent)) {
    curr_ = static_cast<int>(percent);
  }
  updating_ = true;
  scale_.set_value(curr_);
  updating_ = false;
}

void BacklightSlider::update() {
  const auto now = std::chrono::steady_clock::now();
  static auto last = now - interval_;
  if (now - last < interval_) {
    return;
  }
  last = now;

  uint32_t percent = 0;
  if (!waybar::win32::wmi_brightness_query(percent)) {
    event_box_.hide();
    return;
  }
  event_box_.show();
  if (static_cast<int>(percent) != curr_) {
    curr_ = static_cast<int>(percent);
    updating_ = true;
    scale_.set_value(curr_);
    updating_ = false;
  }
  AModule::update();
}

void BacklightSlider::onValueChanged() {
  if (updating_) {
    return;
  }
  curr_ = static_cast<int>(scale_.get_value());
  const auto percent = static_cast<uint8_t>(std::clamp(curr_, 0, 100));
  waybar::win32::wmi_brightness_set(percent);
}

}  // namespace waybar::modules
