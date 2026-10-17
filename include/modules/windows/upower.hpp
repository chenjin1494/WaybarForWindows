#pragma once

#include <fmt/format.h>

#include <string>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 upower：用 GetSystemPowerStatus 提供与上游 upower 一致的
// percentage/state/icon 占位符（单一系统电池）。
class UPower : public ALabel {
 public:
  UPower(const std::string&, const Json::Value&);
  virtual ~UPower() = default;
  auto update() -> void override;

 private:
  util::SleeperThread thread_;
};

}  // namespace waybar::modules
