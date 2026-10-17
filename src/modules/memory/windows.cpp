#include "modules/memory.hpp"

#include <windows.h>

// Windows 实现：GlobalMemoryStatusEx。
//
// meminfo_ 的单位与上游 /proc/meminfo 一致（kiB = 1024 字节），
// common.cpp 里的换算与展示逻辑因此可以原样复用。
//
// 交换分区语义对齐：Windows 的 ullTotalPageFile 是“提交上限”，包含物理内存；
// 这里减去物理内存，得到更接近 Linux swap 的页面文件容量。

void waybar::modules::Memory::parseMeminfo() {
  MEMORYSTATUSEX status{};
  status.dwLength = sizeof(status);
  if (!GlobalMemoryStatusEx(&status)) {
    throw std::runtime_error("GlobalMemoryStatusEx failed");
  }

  const unsigned long kib = 1024;

  meminfo_["MemTotal"] = status.ullTotalPhys / kib;
  meminfo_["MemAvailable"] = status.ullAvailPhys / kib;
  meminfo_["MemFree"] = status.ullAvailPhys / kib;
  meminfo_["Buffers"] = 0;
  meminfo_["Cached"] = 0;
  meminfo_["SReclaimable"] = 0;
  meminfo_["Shmem"] = 0;
  meminfo_["zfs_size"] = 0;

  const unsigned long swap_total =
      status.ullTotalPageFile > status.ullTotalPhys
          ? static_cast<unsigned long>((status.ullTotalPageFile - status.ullTotalPhys) / kib)
          : 0;
  const unsigned long swap_free =
      status.ullAvailPageFile > status.ullAvailPhys
          ? static_cast<unsigned long>((status.ullAvailPageFile - status.ullAvailPhys) / kib)
          : 0;

  meminfo_["SwapTotal"] = swap_total;
  meminfo_["SwapFree"] = swap_free;
}
