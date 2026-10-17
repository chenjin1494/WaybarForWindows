#include "modules/cpu_usage.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>
#include <winternl.h>

#include <stdexcept>

// Windows 实现：用 NtQuerySystemInformation(SystemProcessorPerformanceInformation)
// 取得每个逻辑处理器的 idle/kernel/user 累计时间。
// 返回 (idle, total) 序列，索引 0 为总体、其后为各逻辑处理器，
// 与上游 /proc/stat 解析的语义保持一致（total 包含 idle）。

std::vector<std::tuple<size_t, size_t>> waybar::modules::CpuUsage::parseCpuinfo() {
  const ULONG processor_count = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  if (processor_count == 0) {
    throw std::runtime_error("Can't determine processor count");
  }

  std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> info(processor_count);
  ULONG returned = 0;
  const NTSTATUS status =
      NtQuerySystemInformation(SystemProcessorPerformanceInformation, info.data(),
                               processor_count * static_cast<ULONG>(sizeof(info[0])), &returned);
  if (status != 0) {
    throw std::runtime_error("NtQuerySystemInformation failed");
  }

  const size_t actual = returned / sizeof(info[0]);
  std::vector<std::tuple<size_t, size_t>> cpuinfo;
  cpuinfo.reserve(actual + 1);

  size_t total_idle = 0;
  size_t total_all = 0;
  std::vector<std::tuple<size_t, size_t>> per_cpu;
  per_cpu.reserve(actual);

  for (size_t i = 0; i < actual; ++i) {
    const auto idle = static_cast<size_t>(info[i].IdleTime.QuadPart);
    // KernelTime 已包含 IdleTime
    const auto kernel = static_cast<size_t>(info[i].KernelTime.QuadPart);
    const auto user = static_cast<size_t>(info[i].UserTime.QuadPart);
    const auto total = kernel + user;

    per_cpu.emplace_back(idle, total);
    total_idle += idle;
    total_all += total;
  }

  cpuinfo.emplace_back(total_idle, total_all);
  cpuinfo.insert(cpuinfo.end(), per_cpu.begin(), per_cpu.end());
  return cpuinfo;
}
