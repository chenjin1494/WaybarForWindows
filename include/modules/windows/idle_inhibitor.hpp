#pragma once

#include <fmt/format.h>

#include <ctime>
#include <list>

#include "ALabel.hpp"
#include "bar.hpp"

namespace waybar::modules {

// Windows 版 idle_inhibitor：通过 SetThreadExecutionState(ES_CONTINUOUS |
// ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED) 阻止系统休眠/关屏。
// 配置项与上游保持一致：format / format-icons / timeout / timeout-step /
// start-activated / dynamic-timeouts / wait-for-activity（Windows 上忽略）。
class IdleInhibitor : public ALabel {
 public:
  IdleInhibitor(const std::string&, const waybar::Bar&, const Json::Value&);
  virtual ~IdleInhibitor();
  auto update() -> void override;
  auto refresh(int) -> void override;
  static std::list<waybar::AModule*> modules;
  static bool status;
  static long deactivationTime;

 private:
  bool handleToggle(GdkEventButton* const& e) override;
  bool handleScroll(GdkEventScroll* e) override;

  void toggleStatus(int force_status = -1);
  void applyExecutionState();

  sigc::connection timeout_;
  const Bar& bar_;

  bool dynamicTimeout = false;
  double timeout;
  double timeout_step;
  bool wait_for_activity_;
};

}  // namespace waybar::modules
