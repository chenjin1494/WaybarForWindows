#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <tuple>

#include "ALabel.hpp"
#include "bar.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版电池模块：数据来源为 GetSystemPowerStatus（台式机无电池时自动隐藏）。
// 配置项与上游 battery 保持一致（format/format-{status}/format-{state}/format-time/
// tooltip-format*/full-at/full-at-plugged/states/format-icons/interval/events）。
class Battery : public ALabel {
 public:
  Battery(const std::string&, const waybar::Bar&, const Json::Value&);
  virtual ~Battery() = default;
  auto update() -> void override;

 private:
  const std::string getAdapterStatus(uint8_t capacity) const;
  std::tuple<uint8_t, float, std::string, float, uint16_t, float> getInfos();
  const std::string formatTimeRemaining(float hoursRemaining);
  void setBarClass(std::string&);
  void processEvents(std::string& state, std::string& status, uint8_t capacity);

  const Bar& bar_;
  util::SleeperThread thread_;

  bool last_ac_online_ = false;
  std::string old_status_;
  std::string last_event_;
};

}  // namespace waybar::modules
