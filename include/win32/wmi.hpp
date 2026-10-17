#pragma once

// 最小 WMI 封装（ROOT\WMI），用于 Windows 上没有直接 API 的硬件数据：
//   - 屏幕亮度：WmiMonitorBrightness / WmiMonitorBrightnessMethods
//   - 热区温度：MSAcpi_ThermalZoneTemperature（单位 0.1K）
//
// 所有函数都是“尽力而为”：查询失败返回 false，调用方按不可用处理（隐藏模块），
// 不会抛异常。WMI 查询可能耗时数十毫秒，应在模块的工作线程中调用。

#include <cstdint>
#include <string>

namespace waybar::win32 {

bool wmi_query_u32(const std::wstring& query, const wchar_t* property, uint32_t& out);
bool wmi_query_string(const std::wstring& query, const wchar_t* property, std::string& out);

// 屏幕亮度（0-100）。外接显示器通常没有该 WMI 类，返回 false。
bool wmi_brightness_query(uint32_t& percent);
bool wmi_brightness_set(uint8_t percent);

// 热区温度（摄氏度）。取所有热区中的最大值。
bool wmi_temperature_query(double& celsius);

}  // namespace waybar::win32
