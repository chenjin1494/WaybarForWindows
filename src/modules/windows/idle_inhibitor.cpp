#include "modules/windows/idle_inhibitor.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include "util/command.hpp"

namespace waybar::modules {

std::list<waybar::AModule*> IdleInhibitor::modules;
bool IdleInhibitor::status = false;
long IdleInhibitor::deactivationTime = 0;

IdleInhibitor::IdleInhibitor(const std::string& id, const waybar::Bar& bar,
                             const Json::Value& config)
    : ALabel(config, "idle_inhibitor", id, "{icon}", 1, false, true),
      bar_(bar),
      timeout(config_["timeout"].isDouble() ? config_["timeout"].asDouble() : 0.0),
      timeout_step(config_["timeout-step"].isDouble() ? config_["timeout-step"].asDouble() : 10.0),
      wait_for_activity_(config_["wait-for-activity"].asBool()) {
  if (config_["dynamic-timeouts"].asBool() || config_["dynamic-timeout"].asBool()) {
    dynamicTimeout = true;
  }

  modules.push_back(this);

  if (modules.size() == 1) {
    status = false;
    deactivationTime = 0;
    if (config["start-activated"].isBool()) {
      status = config["start-activated"].asBool();
    }
    if (status) {
      toggleStatus(1);
    }
  }

  if (wait_for_activity_) {
    spdlog::warn("idle_inhibitor: wait-for-activity is not supported on Windows, ignoring");
  }

  event_box_.show();
  label_.show();
  // 点击切换（上游默认行为）
  event_box_.signal_button_press_event().connect(sigc::mem_fun(*this, &IdleInhibitor::handleToggle));
}

IdleInhibitor::~IdleInhibitor() {
  if (timeout_.connected()) {
    timeout_.disconnect();
  }

  modules.remove(this);

  if (modules.empty()) {
    status = false;
    deactivationTime = 0;
    SetThreadExecutionState(ES_CONTINUOUS);  // 释放抑制
  }
}

void IdleInhibitor::applyExecutionState() {
  // SetThreadExecutionState 是“按线程”的状态：这里始终在 GTK 主线程调用，
  // 因此只要主线程活着，ES_CONTINUOUS 就一直有效。
  if (status) {
    const EXECUTION_STATE result =
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    if (result == 0) {
      spdlog::warn("idle_inhibitor: SetThreadExecutionState failed: {}", GetLastError());
    }
  } else {
    SetThreadExecutionState(ES_CONTINUOUS);
  }
}

auto IdleInhibitor::update() -> void {
  applyExecutionState();

  std::string status_text = status ? "activated" : "deactivated";
  int timeleft = (deactivationTime - static_cast<long>(time(nullptr))) / 60;
  updateLabelAndTooltipForState(status_text, format_, "{status}", fmt::arg("status", status_text),
                                fmt::arg("timeout", timeout), fmt::arg("timeleft", timeleft),
                                fmt::arg("icon", getIcon(0, status_text)));
  label_.get_style_context()->add_class(status_text);

  ALabel::update();
}

auto IdleInhibitor::refresh(int sig) -> void {
  // Windows 没有实时信号；保留接口，供控制通道/无信号场景使用。
  (void)sig;
}

void IdleInhibitor::toggleStatus(int force_status) {
  status = !status;
  if (force_status != -1) {
    status = force_status;
  }

  if (timeout_.connected()) {
    timeout_.disconnect();
  }

  if (status && timeout > 0) {
    deactivationTime = static_cast<long>(time(nullptr)) + static_cast<long>(timeout * 60);
    timeout_ = Glib::signal_timeout().connect_seconds(
        []() {
          bool continueRunning = true;
          const int timeleft =
              (IdleInhibitor::deactivationTime - static_cast<long>(time(nullptr))) / 60;
          if (timeleft <= 0) {
            spdlog::info("deactivating idle_inhibitor by timeout");
            IdleInhibitor::status = false;
            continueRunning = false;
          }
          for (auto const& module : IdleInhibitor::modules) {
            module->update();
          }
          return continueRunning;
        },
        60);
  } else {
    deactivationTime = 0;
  }

  applyExecutionState();
}

bool IdleInhibitor::handleToggle(GdkEventButton* const& e) {
  if (e->button != 1) {
    return false;
  }
  if (dynamicTimeout) {
    toggleStatus(1);
  } else {
    toggleStatus();
  }
  for (auto const& module : modules) {
    module->update();
  }
  return true;
}

bool IdleInhibitor::handleScroll(GdkEventScroll* e) {
  const SCROLL_DIR dir = getScrollDir(e);
  if (dir == SCROLL_DIR::NONE) {
    return false;
  }

  // 上游：滚动命令优先
  auto on_scroll = [this, dir]() -> std::string {
    switch (dir) {
      case SCROLL_DIR::UP:
        return config_["on-scroll-up"].isString() ? config_["on-scroll-up"].asString() : "";
      case SCROLL_DIR::DOWN:
        return config_["on-scroll-down"].isString() ? config_["on-scroll-down"].asString() : "";
      case SCROLL_DIR::LEFT:
        return config_["on-scroll-left"].isString() ? config_["on-scroll-left"].asString() : "";
      case SCROLL_DIR::RIGHT:
        return config_["on-scroll-right"].isString() ? config_["on-scroll-right"].asString() : "";
      default:
        return "";
    }
  }();

  if (!on_scroll.empty()) {
    util::command::exec(on_scroll, "");
    return true;
  }

  if (!dynamicTimeout) {
    return false;
  }

  switch (dir) {
    case SCROLL_DIR::UP:
      if (timeout > 0) {
        timeout += timeout_step;
      }
      break;
    case SCROLL_DIR::DOWN:
      if (timeout > timeout_step) {
        timeout -= timeout_step;
      }
      break;
    default:
      return false;
  }

  if (status) {
    toggleStatus(1);
  }
  for (auto const& module : modules) {
    module->update();
  }
  return true;
}


}  // namespace waybar::modules
