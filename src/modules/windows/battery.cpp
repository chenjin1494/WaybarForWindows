#include "modules/windows/battery.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "util/command.hpp"

namespace waybar::modules {

Battery::Battery(const std::string& id, const waybar::Bar& bar, const Json::Value& config)
    : ALabel(config, "battery", id, "{capacity}% {icon}", 60), bar_(bar) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

namespace {

struct PowerState {
  bool present = false;
  bool ac_online = false;
  bool charging = false;
  bool full = false;
  int percent = -1;             // 0..100
  long seconds_remaining = -1;  // BatteryLifeTime，仅在放电时有效
};

PowerState readPowerState() {
  PowerState state;
  SYSTEM_POWER_STATUS status{};
  if (!GetSystemPowerStatus(&status)) {
    return state;
  }

  state.ac_online = status.ACLineStatus == 1;
  const bool no_battery = status.BatteryFlag == 128 || status.BatteryFlag == 255;
  state.present = !no_battery;
  state.charging = (status.BatteryFlag & 8) != 0;
  state.full = status.BatteryLifePercent == 100;
  state.percent = status.BatteryLifePercent <= 100 ? status.BatteryLifePercent : -1;
  if (status.BatteryLifeTime != static_cast<DWORD>(-1)) {
    state.seconds_remaining = static_cast<long>(status.BatteryLifeTime);
  }
  return state;
}

}  // namespace

const std::string Battery::getAdapterStatus(uint8_t capacity) const {
  PowerState state = readPowerState();
  if (!state.present || capacity == 100) {
    return "Full";
  }
  if (state.ac_online && !state.charging) {
    return "Plugged";
  }
  return state.ac_online ? "Charging" : "Discharging";
}

std::tuple<uint8_t, float, std::string, float, uint16_t, float> Battery::getInfos() {
  const PowerState state = readPowerState();
  if (!state.present || state.percent < 0) {
    return {0, 0.0F, "Unknown", 0.0F, 0, 1.0F};
  }

  uint8_t capacity = static_cast<uint8_t>(state.percent);

  // full-at：把 100% 映射到指定容量（与上游一致）
  if (config_["full-at"].isUInt()) {
    const auto full_at = config_["full-at"].asUInt();
    if (full_at > 0 && full_at < 100) {
      capacity = static_cast<uint8_t>(std::min(100.0F, 100.0F * capacity / full_at));
    }
  }

  std::string status;
  if (state.charging) {
    status = "Charging";
  } else if (state.full) {
    status = "Full";
  } else if (state.ac_online) {
    status = "Not charging";
  } else {
    status = "Discharging";
  }

  // 上游约定：正数表示“还有多久耗尽”，负数表示“还有多久充满”
  float hours_remaining = 0.0F;
  if (state.seconds_remaining > 0 && status == "Discharging") {
    hours_remaining = static_cast<float>(state.seconds_remaining) / 3600.0F;
  } else if (state.charging && capacity < 100) {
    const long full_life = static_cast<long>(state.seconds_remaining);
    if (full_life > 0) {
      hours_remaining = -static_cast<float>(full_life) / 3600.0F;
    }
  }

  return {capacity, hours_remaining, status, 0.0F, 0, 1.0F};
}

const std::string Battery::formatTimeRemaining(float hoursRemaining) {
  hoursRemaining = std::fabs(hoursRemaining);
  uint16_t full_hours = static_cast<uint16_t>(hoursRemaining);
  uint16_t minutes = static_cast<uint16_t>(60 * (hoursRemaining - full_hours));
  auto format = std::string("{H} h {M} min");
  if (full_hours == 0 && minutes == 0) {
    return "";
  }
  if (config_["format-time"].isString()) {
    format = config_["format-time"].asString();
  }
  std::string zero_pad_minutes = fmt::format("{:02d}", minutes);
  return fmt::format(fmt::runtime(format), fmt::arg("H", full_hours), fmt::arg("M", minutes),
                     fmt::arg("m", zero_pad_minutes));
}

void Battery::setBarClass(std::string& state) {
  auto classes = bar_.window.get_style_context()->list_classes();
  const std::string prefix = "battery-";

  auto old_class_it = std::find_if(classes.begin(), classes.end(), [&prefix](auto classname) {
    return classname.rfind(prefix, 0) == 0;
  });

  auto new_class = prefix + state;

  if (old_class_it == classes.end()) {
    if (!state.empty()) {
      bar_.window.get_style_context()->add_class(new_class);
    }
    return;
  }

  auto old_class = *old_class_it;

  if (state.empty()) {
    bar_.window.get_style_context()->remove_class(old_class);
    return;
  }

  if (old_class != new_class) {
    bar_.window.get_style_context()->remove_class(old_class);
    bar_.window.get_style_context()->add_class(new_class);
  }
}

void Battery::processEvents(std::string& state, std::string& status, uint8_t capacity) {
  auto events = config_["events"];
  if (!events.isObject() || events.empty()) {
    return;
  }
  auto exec = [](Json::Value const& event) {
    if (!event.isString()) return;
    if (auto command = event.asString(); !command.empty()) {
      util::command::exec(command, "");
    }
  };
  std::string status_name = status == "discharging" ? "on-discharging" : "on-charging";
  std::string event_name = status_name + '-' + (state.empty() ? std::to_string(capacity) : state);
  if (last_event_ != event_name) {
    spdlog::debug("battery: triggering event {}", event_name);
    exec(events[event_name]);
    if (!last_event_.empty() && last_event_[3] != event_name[3]) {
      exec(events[status_name]);
    }
    last_event_ = event_name;
  }
}

auto Battery::update() -> void {
  PowerState power_state = readPowerState();
  last_ac_online_ = power_state.ac_online;

  if (!power_state.present) {
    // 台式机没有电池：隐藏模块（与上游 Linux 无电池时的行为一致）
    event_box_.hide();
    ALabel::update();
    return;
  }

  auto [capacity, time_remaining, status, power, cycles, health] = getInfos();
  if (status == "Unknown") {
    status = getAdapterStatus(capacity);
  }
  auto state = getState(capacity, true);
  if (config_["full-at-plugged"].asBool() && status == "Full" && power_state.ac_online) {
    status = "Plugged";
  }
  auto status_pretty = status;

  std::ranges::transform(status.begin(), status.end(), status.begin(),
                         [](char ch) { return ch == ' ' ? '-' : std::tolower(ch); });
  auto format = format_;
  processEvents(state, status, capacity);
  setBarClass(state);
  auto time_remaining_formatted = formatTimeRemaining(time_remaining);

  if (tooltipEnabled()) {
    std::string tooltip_text_default;
    std::string tooltip_format = "{timeTo}";
    if (time_remaining != 0) {
      if (time_remaining > 0) {
        tooltip_text_default = std::string("Empty in ") + time_remaining_formatted;
      } else {
        tooltip_text_default = std::string("Full in ") + time_remaining_formatted;
      }
    } else {
      tooltip_text_default = status_pretty;
    }
    if (!state.empty() && config_["tooltip-format-" + status + "-" + state].isString()) {
      tooltip_format = config_["tooltip-format-" + status + "-" + state].asString();
    } else if (config_["tooltip-format-" + status].isString()) {
      tooltip_format = config_["tooltip-format-" + status].asString();
    } else if (!state.empty() && config_["tooltip-format-" + state].isString()) {
      tooltip_format = config_["tooltip-format-" + state].asString();
    } else if (config_["tooltip-format"].isString()) {
      tooltip_format = config_["tooltip-format"].asString();
    }
    setTooltipMarkup(
        fmt::format(fmt::runtime(tooltip_format), fmt::arg("timeTo", tooltip_text_default),
                    fmt::arg("power", power), fmt::arg("capacity", capacity),
                    fmt::arg("time", time_remaining_formatted), fmt::arg("cycles", cycles),
                    fmt::arg("health", fmt::format("{:.3}", health))));
  }

  if (!old_status_.empty()) {
    label_.get_style_context()->remove_class(old_status_);
  }
  label_.get_style_context()->add_class(status);
  old_status_ = status;

  if (!state.empty() && config_["format-" + status + "-" + state].isString()) {
    format = config_["format-" + status + "-" + state].asString();
  } else if (config_["format-" + status].isString()) {
    format = config_["format-" + status].asString();
  } else if (!state.empty() && config_["format-" + state].isString()) {
    format = config_["format-" + state].asString();
  }

  if (format.empty()) {
    event_box_.hide();
  } else {
    event_box_.show();
    auto icons = std::vector<std::string>{status + "-" + state, status, state};
    setLabelMarkup(fmt::format(
        fmt::runtime(format), fmt::arg("capacity", capacity), fmt::arg("power", power),
        fmt::arg("icon", getIcon(capacity, icons)), fmt::arg("time", time_remaining_formatted),
        fmt::arg("cycles", cycles), fmt::arg("health", fmt::format("{:.3}", health))));
  }

  ALabel::update();
}

}  // namespace waybar::modules
