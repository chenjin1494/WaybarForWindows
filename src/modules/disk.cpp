#include "modules/disk.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <optional>

#ifndef _WIN32
#include <cerrno>
#else
#include <windows.h>

#include "win32/common.hpp"
#endif

using namespace waybar::util;

#ifdef _WIN32
namespace {

struct WindowsDiskStats {
  unsigned long long blocks;
  unsigned long long bfree;
  unsigned long long bavail;
  unsigned long long frsize;
};

// 把配置里的 POSIX 风格路径转换为 Windows 路径：
//   "/"        -> 系统盘根目录
//   "/mnt/c/…" -> "C:\…"
//   "C:"       -> "C:\"
std::wstring to_windows_path(const std::string& path) {
  std::string p = path;
  for (auto& c : p) {
    if (c == '/') c = '\\';
  }

  if (p.empty() || p == "\\") {
    const std::string system_drive = waybar::win32::get_env("SystemDrive");
    p = (system_drive.empty() ? std::string("C:") : system_drive) + "\\";
  } else if (p.size() == 2 && p[1] == ':') {
    p += "\\";
  } else if (p.rfind("\\mnt\\", 0) == 0 && p.size() >= 6) {
    // WSL 风格挂载点
    std::string drive(1, static_cast<char>(std::toupper(static_cast<unsigned char>(p[5]))));
    p = drive + ":\\" + p.substr(7);
  }
  return waybar::win32::to_wide(p);
}

std::optional<WindowsDiskStats> getDiskStats(const std::string& path) {
  ULARGE_INTEGER free_to_caller{};
  ULARGE_INTEGER total{};
  ULARGE_INTEGER total_free{};
  const std::wstring wide_path = to_windows_path(path);
  if (!GetDiskFreeSpaceExW(wide_path.c_str(), &free_to_caller, &total, &total_free)) {
    return std::nullopt;
  }  if (total.QuadPart == 0) {
    return std::nullopt;
  }
  // 以字节为单位的“块”，frsize 固定为 1，便于复用上游的换算逻辑
  return WindowsDiskStats{total.QuadPart, total_free.QuadPart, free_to_caller.QuadPart, 1};
}

}  // namespace
#endif

waybar::modules::Disk::Disk(const std::string& id, const Json::Value& config)
    : ALabel(config, "disk", id, "{}%", 30), header_(""), paths_(), separator_(" ") {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
  if (config["header"].isString()) {
    header_ = config["header"].asString();
  }
  if (config["path"].isString() && !config["paths"].isArray()) {
    spdlog::warn("Disk: path is deprecated use paths instead!");
    paths_.push_back(config["path"].asString());
  }
  if (config["paths"].isArray()) {
    for (const auto& path : config["paths"]) {
      paths_.push_back(path.asString());
    }
  }
  if (!config["path"].isString() && !config["paths"].isArray()) {
    paths_.emplace_back("/");
  }
  if (config["separator"].isString()) {
    separator_ = config["separator"].asString();
  }
  if (config["unit"].isString()) {
    unit_ = config["unit"].asString();
  }
}

auto waybar::modules::Disk::update() -> void {
  std::string tooltip_label;
  std::string label = header_;

  bool had_valid_disk = false;

  for (size_t i = 0; i < paths_.size(); ++i) {
    const auto& path = paths_[i];

#ifdef _WIN32
    auto stats_opt = getDiskStats(path);
    if (!stats_opt) {
      spdlog::warn("Disk: cannot read usage for path '{}'", path);
      continue;
    }
    const unsigned long long f_blocks = stats_opt->blocks;
    const unsigned long long f_bfree = stats_opt->bfree;
    const unsigned long long f_bavail = stats_opt->bavail;
    const unsigned long long f_frsize = stats_opt->frsize;
#else
    struct statvfs /* {
        unsigned long  f_bsize;    // filesystem block size
        unsigned long  f_frsize;   // fragment size
        fsblkcnt_t     f_blocks;   // size of fs in f_frsize units
        fsblkcnt_t     f_bfree;    // # free blocks
        fsblkcnt_t     f_bavail;   // # free blocks for unprivileged users
        fsfilcnt_t     f_files;    // # inodes
        fsfilcnt_t     f_ffree;    // # free inodes
        fsfilcnt_t     f_favail;   // # free inodes for unprivileged users
        unsigned long  f_fsid;     // filesystem ID
        unsigned long  f_flag;     // mount flags
        unsigned long  f_namemax;  // maximum filename length
    }; */
        stats;

    int err = statvfs(path.c_str(), &stats);

    /* Conky options
      fs_bar - Bar that shows how much space is used
      fs_free - Free space on a file system
      fs_free_perc - Free percentage of space
      fs_size - File system size
      fs_used - File system used space
    */

    if (err != 0 || stats.f_blocks == 0) {
      spdlog::warn("Disk: statvfs failed for path '{}' (errno={})", path, errno);
      continue;
    }

    const unsigned long long f_blocks = stats.f_blocks;
    const unsigned long long f_bfree = stats.f_bfree;
    const unsigned long long f_bavail = stats.f_bavail;
    const unsigned long long f_frsize = stats.f_frsize;
#endif

    float specific_free, specific_used, specific_total, divisor;

    divisor = calc_specific_divisor(unit_);
    specific_free = (f_bavail * f_frsize) / divisor;
    specific_used = ((f_blocks - f_bfree) * f_frsize) / divisor;
    specific_total = (f_blocks * f_frsize) / divisor;

    auto free = pow_format(f_bavail * f_frsize, "B", true);
    auto used = pow_format((f_blocks - f_bfree) * f_frsize, "B", true);
    auto total = pow_format(f_blocks * f_frsize, "B", true);
    auto percentage_used = (f_blocks - f_bfree) * 100 / f_blocks;

    std::string disk_format = format_;
    auto state = getState(percentage_used);
    if (!state.empty() && config_["format-" + state].isString()) {
      disk_format = config_["format-" + state].asString();
    }

    if (!disk_format.empty()) {
      try {
        if (had_valid_disk) {
          label += separator_;
        }

        label += fmt::format(fmt::runtime(disk_format), f_bavail * 100 / f_blocks,
                             fmt::arg("free", free),
                             fmt::arg("percentage_free", f_bavail * 100 / f_blocks),
                             fmt::arg("used", used), fmt::arg("percentage_used", percentage_used),
                             fmt::arg("total", total), fmt::arg("path", path),
                             fmt::arg("specific_free", specific_free),
                             fmt::arg("specific_used", specific_used),
                             fmt::arg("specific_total", specific_total));

        had_valid_disk = true;
      } catch (const fmt::format_error& e) {
        spdlog::warn("Disk: {} ({})", e.what(), path);
      }
    }

    std::string tooltip_format = "{used} used out of {total} on {path} ({percentage_used}%)";
    if (config_["tooltip-format"].isString()) {
      tooltip_format = config_["tooltip-format"].asString();
    }

    if (!tooltip_format.empty()) {
      try {
        if (had_valid_disk && i > 0) {
          tooltip_label += "\n";
        }

        tooltip_label += fmt::format(
            fmt::runtime(tooltip_format), f_bavail * 100 / f_blocks,
            fmt::arg("free", free),
            fmt::arg("percentage_free", f_bavail * 100 / f_blocks),
            fmt::arg("used", used), fmt::arg("percentage_used", percentage_used),
            fmt::arg("total", total), fmt::arg("path", path),
            fmt::arg("specific_free", specific_free), fmt::arg("specific_used", specific_used),
            fmt::arg("specific_total", specific_total));

        had_valid_disk = true;
      } catch (const fmt::format_error& e) {
        spdlog::warn("Disk tooltip: {} ({})", e.what(), path);
      }
    }
  }
  if (had_valid_disk) {
    event_box_.show();
  } else {
    event_box_.hide();
  }

  setLabelMarkup(label);

  if (tooltipEnabled() && !tooltip_label.empty()) {
    setTooltipMarkup(tooltip_label);
  }
  // Call parent update
  ALabel::update();
}

float waybar::modules::Disk::calc_specific_divisor(const std::string& divisor) {
  if (divisor == "kB") {
    return 1000.0;
  } else if (divisor == "kiB") {
    return 1024.0;
  } else if (divisor == "MB") {
    return 1000.0 * 1000.0;
  } else if (divisor == "MiB") {
    return 1024.0 * 1024.0;
  } else if (divisor == "GB") {
    return 1000.0 * 1000.0 * 1000.0;
  } else if (divisor == "GiB") {
    return 1024.0 * 1024.0 * 1024.0;
  } else if (divisor == "TB") {
    return 1000.0 * 1000.0 * 1000.0 * 1000.0;
  } else if (divisor == "TiB") {
    return 1024.0 * 1024.0 * 1024.0 * 1024.0;
  } else {  // default to Bytes if it is anything that we don't recognise
    return 1.0;
  }
}
