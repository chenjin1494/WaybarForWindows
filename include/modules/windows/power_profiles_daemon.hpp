#pragma once

#include <fmt/format.h>

#include <string>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 power-profiles-daemon：映射到 Windows 电源计划
//   balanced      -> 平衡
//   performance   -> 高性能
//   power-saver   -> 节能
// 配置项与上游一致：format / format-icons / tooltip-format / states / on-click 循环切换。
class PowerProfilesDaemon : public ALabel {
 public:
  PowerProfilesDaemon(const std::string&, const Json::Value&);
  virtual ~PowerProfilesDaemon() = default;
  auto update() -> void override;

 private:
  bool handleToggle(GdkEventButton* const& e) override;
  bool handleScroll(GdkEventScroll* e) override;

  void cycleProfile();
  bool applyProfile(const std::string& profile);

  util::SleeperThread thread_;
  std::string current_;
};

}  // namespace waybar::modules
