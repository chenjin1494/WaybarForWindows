#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <string>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 temperature：数据来自 WMI MSAcpi_ThermalZoneTemperature（取所有热区最大值）。
// 台式机/部分设备没有该 WMI 类，此时模块自动隐藏（与上游找不到传感器的行为一致）。
class Temperature : public ALabel {
 public:
  Temperature(const std::string&, const Json::Value&);
  virtual ~Temperature() = default;
  auto update() -> void override;

 private:
  bool isCritical(uint16_t temperature_c) const;
  bool isWarning(uint16_t temperature_c) const;

  util::SleeperThread thread_;
};

}  // namespace waybar::modules
