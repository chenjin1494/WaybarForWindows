#include "modules/windows/upower.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>

namespace waybar::modules {

UPower::UPower(const std::string& id, const Json::Value& config)
    : ALabel(config, "upower", id, "{percentage}%", 30) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

auto UPower::update() -> void {
  SYSTEM_POWER_STATUS status{};
  const bool ok = GetSystemPowerStatus(&status) != 0;
  const bool present = ok && status.BatteryFlag != 128 && status.BatteryFlag != 255 &&
                       status.BatteryLifePercent <= 100;

  const bool hide_if_empty =
      config_["hide-if-empty"].isBool() ? config_["hide-if-empty"].asBool() : true;
  if (!present) {
    if (hide_if_empty) {
      event_box_.hide();
      ALabel::update();
      return;
    }
    label_.set_markup("");
    ALabel::update();
    return;
  }

  event_box_.show();

  const int percentage = status.BatteryLifePercent;
  const bool charging = (status.BatteryFlag & 8) != 0;
  const bool full = percentage >= 100;
  std::string state;
  if (charging) {
    state = "charging";
  } else if (full) {
    state = "full";
  } else if (status.ACLineStatus == 1) {
    state = "pending-charge";
  } else {
    state = "discharging";
  }

  // 剩余时间（秒，-1 表示未知）
  long seconds = -1;
  if (status.BatteryLifeTime != static_cast<DWORD>(-1)) {
    seconds = static_cast<long>(status.BatteryLifeTime);
  }
  const int hours = seconds >= 0 ? static_cast<int>(seconds / 3600) : 0;
  const int minutes = seconds >= 0 ? static_cast<int>((seconds % 3600) / 60) : 0;

  auto format = format_;
  const std::string state_format = "format-" + state;
  if (config_[state_format].isString()) {
    format = config_[state_format].asString();
  }
  if (format.empty()) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  const auto state_for_tooltip = state;
  updateLabelAndTooltipForState(
      state_for_tooltip, format, "{percentage}% ({state})", fmt::arg("percentage", percentage),
      fmt::arg("state", state), fmt::arg("icon", getIcon(percentage)),
      fmt::arg("time", fmt::format("{}h {}m", hours, minutes)),
      fmt::arg("hours", hours), fmt::arg("minutes", minutes));

  label_.get_style_context()->remove_class("charging");
  label_.get_style_context()->remove_class("discharging");
  label_.get_style_context()->remove_class("full");
  label_.get_style_context()->remove_class("pending-charge");
  label_.get_style_context()->add_class(state);
  ALabel::update();
}

}  // namespace waybar::modules
