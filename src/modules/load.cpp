#include "modules/load.hpp"

#include <cmath>

#ifndef _WIN32
#include <cstdlib>
#else
#include <windows.h>
#include <pdh.h>

#include <mutex>
#endif

// In the 80000 version of fmt library authors decided to optimize imports
// and moved declarations required for fmt::dynamic_format_arg_store in new
// header fmt/args.h
#if (FMT_VERSION >= 80000)
#include <fmt/args.h>
#else
#include <fmt/core.h>
#endif

waybar::modules::Load::Load(const std::string& id, const Json::Value& config)
    : ALabel(config, "load", id, "{load1}", 10) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

auto waybar::modules::Load::update() -> void {
  // TODO: as creating dynamic fmt::arg arrays is buggy we have to calc both
  auto [load1, load5, load15] = Load::getLoad();
  auto default_tooltip = fmt::format("Load 1: {}\nLoad 5: {}\nLoad 15: {}", load1, load5, load15);
  auto format = format_;
  auto state = getState(load1);
  if (!state.empty() && config_["format-" + state].isString()) {
    format = config_["format-" + state].asString();
  }

  if (format.empty()) {
    event_box_.hide();
  } else {
    event_box_.show();
    auto icons = std::vector<std::string>{state};
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    store.push_back(fmt::arg("load1", load1));
    store.push_back(fmt::arg("load5", load5));
    store.push_back(fmt::arg("load15", load15));
    store.push_back(fmt::arg("icon1", getIcon(load1, icons)));
    store.push_back(fmt::arg("icon5", getIcon(load5, icons)));
    store.push_back(fmt::arg("icon15", getIcon(load15, icons)));
    label_.set_markup(fmt::vformat(format, store));
    if (tooltipEnabled()) {
      setTooltipMarkup(fmt::vformat(resolveTooltipFormat(default_tooltip, state), store));
    }
  }

  // Call parent update
  ALabel::update();
}

std::tuple<double, double, double> waybar::modules::Load::getLoad() {
#ifdef _WIN32
  // Windows 没有 POSIX load average。这里用 PDH 的 Processor Queue Length
  // （可运行但等待 CPU 的线程数）+ 1（近似当前在跑的线程）作为瞬时负载，
  // 再用 1/5/15 分钟时间常数做指数滑动平均，得到接近 uptime 语义的三个值。
  static std::mutex mutex;
  static PDH_HQUERY query = nullptr;
  static PDH_HCOUNTER counter = nullptr;
  static bool initialized = false;
  static double ema1 = 0.0;
  static double ema5 = 0.0;
  static double ema15 = 0.0;
  static ULONGLONG last_tick = 0;

  std::lock_guard<std::mutex> lock(mutex);

  if (!initialized) {
    initialized = true;
    if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
      if (PdhAddEnglishCounterW(query, L"\\System\\Processor Queue Length", 0, &counter) !=
          ERROR_SUCCESS) {
        if (counter != nullptr) {
          PdhRemoveCounter(counter);
          counter = nullptr;
        }
        PdhCloseQuery(query);
        query = nullptr;
      }
    }
  }

  double runnable = 1.0;
  if (query != nullptr && counter != nullptr) {
    if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
      PDH_FMT_COUNTERVALUE value{};
      if (PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, nullptr, &value) == ERROR_SUCCESS) {
        runnable += value.doubleValue;
      }
    }
  }

  const ULONGLONG now = GetTickCount64();
  double dt = last_tick == 0 ? 0.0 : static_cast<double>(now - last_tick) / 1000.0;
  last_tick = now;
  if (dt <= 0.0) dt = 10.0;  // 首次采样按默认 interval 处理

  const auto ema = [dt](double current, double sample, double tau_seconds) {
    const double alpha = 1.0 - std::exp(-dt / tau_seconds);
    return current + alpha * (sample - current);
  };

  if (ema1 == 0.0) ema1 = runnable;
  if (ema5 == 0.0) ema5 = runnable;
  if (ema15 == 0.0) ema15 = runnable;

  ema1 = ema(ema1, runnable, 60.0);
  ema5 = ema(ema5, runnable, 300.0);
  ema15 = ema(ema15, runnable, 900.0);

  const auto round2 = [](double value) { return std::ceil(value * 100.0) / 100.0; };
  return {round2(ema1), round2(ema5), round2(ema15)};
#else
  double load[3];
  if (getloadavg(load, 3) != -1) {
    double load1 = std::ceil(load[0] * 100.0) / 100.0;
    double load5 = std::ceil(load[1] * 100.0) / 100.0;
    double load15 = std::ceil(load[2] * 100.0) / 100.0;
    return {load1, load5, load15};
  }
  throw std::runtime_error("Can't get system load");
#endif
}
