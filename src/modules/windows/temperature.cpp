#include "modules/windows/temperature.hpp"

#include <cmath>

#include "win32/wmi.hpp"

namespace waybar::modules {

Temperature::Temperature(const std::string& id, const Json::Value& config)
    : ALabel(config, "temperature", id, "{temperatureC}°C", 10) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

bool Temperature::isCritical(uint16_t temperature_c) const {
  return config_["critical-threshold"].isInt() &&
         temperature_c >= config_["critical-threshold"].asInt();
}

bool Temperature::isWarning(uint16_t temperature_c) const {
  return config_["warning-threshold"].isInt() &&
         temperature_c >= config_["warning-threshold"].asInt();
}

auto Temperature::update() -> void {
  double temperature = 0.0;
  if (!waybar::win32::wmi_temperature_query(temperature)) {
    // 没有可用的热区（常见于台式机）：隐藏模块
    event_box_.hide();
    ALabel::update();
    return;
  }

  const auto temperature_c = static_cast<uint16_t>(std::lround(temperature));
  const auto temperature_f = static_cast<uint16_t>(std::lround(temperature * 1.8 + 32));
  const auto temperature_k = static_cast<uint16_t>(std::lround(temperature + 273.15));

  const bool critical = isCritical(temperature_c);
  const bool warning = !critical && isWarning(temperature_c);
  auto format = format_;
  if (critical) {
    if (config_["format-critical"].isString()) {
      format = config_["format-critical"].asString();
    }
    label_.get_style_context()->add_class("critical");
    label_.get_style_context()->remove_class("warning");
  } else if (warning) {
    if (config_["format-warning"].isString()) {
      format = config_["format-warning"].asString();
    }
    label_.get_style_context()->add_class("warning");
    label_.get_style_context()->remove_class("critical");
  } else {
    label_.get_style_context()->remove_class("critical");
    label_.get_style_context()->remove_class("warning");
  }

  if (format.empty()) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  event_box_.show();
  const auto max_temp =
      config_["critical-threshold"].isInt() ? config_["critical-threshold"].asInt() : 0;
  updateLabelAndTooltip(format, "{temperatureC}°C", fmt::arg("temperatureC", temperature_c),
                        fmt::arg("temperatureF", temperature_f),
                        fmt::arg("temperatureK", temperature_k),
                        fmt::arg("icon", getIcon(temperature_c, "", max_temp)));
  ALabel::update();
}

}  // namespace waybar::modules
