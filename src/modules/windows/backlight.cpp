#include "modules/windows/backlight.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <cmath>

#include "util/command.hpp"
#include "win32/wmi.hpp"

namespace waybar::modules {

Backlight::Backlight(const std::string& id, const Json::Value& config)
    : ALabel(config, "backlight", id, "{percent}%", 5, false, false, true),
      scroll_step_(config_["scroll-step"].isDouble() ? static_cast<int>(config_["scroll-step"].asDouble())
                                                     : 10),
      min_brightness_(config_["min-brightness"].isDouble()
                          ? static_cast<int>(config_["min-brightness"].asDouble())
                          : 0) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

bool Backlight::setBrightness(double value) {
  const double clamped = std::clamp(value, static_cast<double>(min_brightness_), 100.0);
  const auto percent = static_cast<uint8_t>(std::lround(clamped));
  return waybar::win32::wmi_brightness_set(percent);
}

auto Backlight::update() -> void {
  uint32_t percent = 0;
  if (!waybar::win32::wmi_brightness_query(percent)) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  event_box_.show();

  const double ratio = static_cast<double>(percent) / 100.0;
  const auto percent_exp = static_cast<uint8_t>(std::lround(std::pow(ratio, 1.0 / 2.718) * 100.0));

  auto format = format_;
  const auto state = getState(percent);
  if (!state.empty()) {
    const auto state_format = "format-" + state;
    if (config_[state_format].isString()) {
      format = config_[state_format].asString();
    }
  }

  updateLabelAndTooltip(format, format, fmt::arg("percent", percent),
                        fmt::arg("percent_exp", percent_exp),
                        fmt::arg("icon", getIcon(percent)),
                        fmt::arg("icon_exp", getIcon(percent_exp)));
  ALabel::update();
}

bool Backlight::handleScroll(GdkEventScroll* e) {
  const SCROLL_DIR dir = getScrollDir(e);
  if (dir == SCROLL_DIR::NONE) {
    return false;
  }

  auto on_scroll = [&](const char* key) -> std::string {
    return config_[key].isString() ? config_[key].asString() : std::string();
  };
  const std::string command =
      dir == SCROLL_DIR::UP ? on_scroll("on-scroll-up")
                            : (dir == SCROLL_DIR::DOWN ? on_scroll("on-scroll-down") : "");
  if (!command.empty()) {
    util::command::exec(command, "");
    return true;
  }

  uint32_t current = 0;
  if (!waybar::win32::wmi_brightness_query(current)) {
    return false;
  }
  const double target = static_cast<double>(current) +
                        (dir == SCROLL_DIR::UP ? scroll_step_ : -scroll_step_);
  if (!setBrightness(target)) {
    return false;
  }
  update();
  return true;
}

}  // namespace waybar::modules
