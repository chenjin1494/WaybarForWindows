#include "modules/simpleclock.hpp"

#include <fmt/chrono.h>
#include <time.h>

namespace {

// fmt 12 移除了 fmt::localtime（上游依赖其旧版本）。这里提供一个等价实现，
// 同时保持 Linux/BSD 构建可用。
std::tm to_local_tm(std::time_t value) {
  std::tm result{};
#ifdef _WIN32
  localtime_s(&result, &value);
#else
  localtime_r(&value, &result);
#endif
  return result;
}

}  // namespace

waybar::modules::Clock::Clock(const std::string& id, const Json::Value& config)
    : ALabel(config, "clock", id, "{:%H:%M}", 60) {
  thread_ = [this] {
    dp.emit();
    auto now = std::chrono::system_clock::now();
    /* difference with projected wakeup time */
    auto diff = now.time_since_epoch() % interval_;
    /* sleep until the next projected time */
    thread_.sleep_for(interval_ - diff);
  };
}

auto waybar::modules::Clock::update() -> void {
  tzset();  // Update timezone information
  auto now = std::chrono::system_clock::now();
  auto localtime = to_local_tm(std::chrono::system_clock::to_time_t(now));
  updateLabelAndTooltip(format_, format_, localtime);
  // Call parent update
  ALabel::update();
}
