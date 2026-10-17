#include "modules/windows/power_profiles_daemon.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>
#include <powrprof.h>

#include <array>
#include <cstring>

namespace waybar::modules {

namespace {

// 三个标准电源计划 GUID（避免依赖 MSYS2 头文件里的 GUID 常量）
constexpr GUID kBalanced = {0x381b4222,
                            0xf694,
                            0x41f0,
                            {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
constexpr GUID kHighPerformance = {0x8c5e7fda,
                                   0xe8bf,
                                   0x4a96,
                                   {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
constexpr GUID kPowerSaver = {0xa1841308,
                              0x3541,
                              0x4fab,
                              {0xbc, 0x81, 0xf7, 0x15, 0x56, 0xf2, 0x0b, 0x4a}};

bool guid_equal(const GUID& a, const GUID& b) {
  return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

std::string active_profile() {
  GUID* active = nullptr;
  if (PowerGetActiveScheme(nullptr, &active) != ERROR_SUCCESS || active == nullptr) {
    return "";
  }
  std::string name;
  if (guid_equal(*active, kBalanced)) {
    name = "balanced";
  } else if (guid_equal(*active, kHighPerformance)) {
    name = "performance";
  } else if (guid_equal(*active, kPowerSaver)) {
    name = "power-saver";
  }
  LocalFree(active);
  return name;
}

bool set_profile(const std::string& profile) {
  const GUID* target = nullptr;
  if (profile == "balanced") {
    target = &kBalanced;
  } else if (profile == "performance") {
    target = &kHighPerformance;
  } else if (profile == "power-saver") {
    target = &kPowerSaver;
  } else {
    return false;
  }

  GUID guid = *target;
  return PowerSetActiveScheme(nullptr, &guid) == ERROR_SUCCESS;
}

}  // namespace

PowerProfilesDaemon::PowerProfilesDaemon(const std::string& id, const Json::Value& config)
    : ALabel(config, "power-profiles-daemon", id, "{icon}", 5) {
  current_ = active_profile();
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

bool PowerProfilesDaemon::applyProfile(const std::string& profile) {
  if (!set_profile(profile)) {
    spdlog::warn("power-profiles-daemon: failed to switch to '{}'", profile);
    return false;
  }
  current_ = profile;
  update();
  return true;
}

void PowerProfilesDaemon::cycleProfile() {
  static const std::array<const char*, 3> order = {"balanced", "performance", "power-saver"};
  size_t index = 0;
  for (size_t i = 0; i < order.size(); ++i) {
    if (current_ == order[i]) {
      index = (i + 1) % order.size();
      break;
    }
  }
  applyProfile(order[index]);
}

auto PowerProfilesDaemon::update() -> void {
  current_ = active_profile();

  if (current_.empty()) {
    // 无法读取电源计划（极少数环境）：隐藏
    event_box_.hide();
    ALabel::update();
    return;
  }

  event_box_.show();
  const std::string state = current_;
  auto format = format_;
  if (config_["format-" + state].isString()) {
    format = config_["format-" + state].asString();
  }

  updateLabelAndTooltipForState(state, format, "{profile}", fmt::arg("profile", state),
                                fmt::arg("icon", getIcon(0, {state})),
                                fmt::arg("state", state));

  label_.get_style_context()->remove_class("balanced");
  label_.get_style_context()->remove_class("performance");
  label_.get_style_context()->remove_class("power-saver");
  label_.get_style_context()->add_class(state);

  ALabel::update();
}

bool PowerProfilesDaemon::handleToggle(GdkEventButton* const& e) {
  if (e->button == 1) {
    if (config_["on-click"].isString() && !config_["on-click"].asString().empty()) {
      return ALabel::handleToggle(e);
    }
    cycleProfile();
    return true;
  }
  return ALabel::handleToggle(e);
}

bool PowerProfilesDaemon::handleScroll(GdkEventScroll* e) {
  const SCROLL_DIR dir = getScrollDir(e);
  if (dir == SCROLL_DIR::UP || dir == SCROLL_DIR::DOWN) {
    cycleProfile();
    return true;
  }
  return false;
}

}  // namespace waybar::modules
