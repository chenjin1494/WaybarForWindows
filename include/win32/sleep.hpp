#pragma once

// 休眠/唤醒通知（替代上游的 logind inhibitor）。

#include "util/SafeSignal.hpp"

namespace waybar::win32 {

// true = 进入睡眠，false = 唤醒；首次调用时注册系统通知。
waybar::SafeSignal<bool>& suspend_signal();

// 显式初始化（可重复调用，幂等）。
void init_power_notification();

}  // namespace waybar::win32
