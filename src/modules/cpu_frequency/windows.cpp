#include "modules/cpu_frequency.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <cmath>

// Windows 实现：从注册表 HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\<n>\~MHz
// 读取各逻辑处理器的标称主频（MHz）。
//
// 限制：这是“标称/基础”频率而非实时频率（实时值需要 CallNtPowerInformation，
// MSYS2 头文件缺少 PROCESSOR_POWER_INFORMATION，见 docs/WINDOWS-DIFFERENCES.md）。
// 因此 {avg_frequency}/{min_frequency}/{max_frequency} 在 Windows 上表示标称频率。

namespace {

std::vector<float> readRegistryFrequencies() {
  std::vector<float> frequencies;

  HKEY root = nullptr;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor", 0,
                    KEY_READ, &root) != ERROR_SUCCESS) {
    return frequencies;
  }

  for (DWORD index = 0;; ++index) {
    wchar_t subkey_name[64];
    const DWORD name_length = static_cast<DWORD>(std::size(subkey_name));
    if (RegEnumKeyExW(root, index, subkey_name, const_cast<LPDWORD>(&name_length), nullptr, nullptr,
                      nullptr, nullptr) != ERROR_SUCCESS) {
      break;
    }

    HKEY subkey = nullptr;
    if (RegOpenKeyExW(root, subkey_name, 0, KEY_READ, &subkey) != ERROR_SUCCESS) {
      continue;
    }

    DWORD mhz = 0;
    DWORD size = sizeof(mhz);
    DWORD type = 0;
    if (RegQueryValueExW(subkey, L"~MHz", nullptr, &type, reinterpret_cast<LPBYTE>(&mhz), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD && mhz > 0) {
      frequencies.push_back(static_cast<float>(mhz));
    }
    RegCloseKey(subkey);
  }

  RegCloseKey(root);
  return frequencies;
}

}  // namespace

std::vector<float> waybar::modules::CpuFrequency::parseCpuFrequencies() {
  auto frequencies = readRegistryFrequencies();
  if (frequencies.empty()) {
    const auto cores = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    frequencies.assign(cores, 0.0F);
  }
  return frequencies;
}
