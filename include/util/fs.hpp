#pragma once

// 跨平台 UTF-8 文件系统适配。
//
// Windows 的 std::filesystem::path(std::string) 会按 ANSI 代码页解释窄字符串，
// 直接传 UTF-8（如 C:\Users\琛瑾喵呜\...）会抛 "Illegal byte sequence"。
// 因此所有路径构造统一走 to_path()：Windows 上先转 UTF-16，POSIX 上原样使用。

#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
#include "win32/common.hpp"
#endif

namespace waybar::util {

inline std::filesystem::path to_path(const std::string& utf8_path) {
#ifdef _WIN32
  return std::filesystem::path(win32::to_wide(utf8_path));
#else
  return std::filesystem::path(utf8_path);
#endif
}

inline std::string path_to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
  return win32::to_utf8(path.wstring());
#else
  return path.string();
#endif
}

inline std::ifstream open_ifstream(const std::string& utf8_path,
                                   std::ios::openmode mode = std::ios::in) {
  return std::ifstream(to_path(utf8_path), mode);
}

inline std::ofstream open_ofstream(const std::string& utf8_path,
                                   std::ios::openmode mode = std::ios::out) {
  return std::ofstream(to_path(utf8_path), mode);
}

inline bool path_exists(const std::string& utf8_path) {
  std::error_code ec;
  return std::filesystem::exists(to_path(utf8_path), ec);
}

inline bool is_symlink_path(const std::string& utf8_path) {
  std::error_code ec;
  return std::filesystem::is_symlink(to_path(utf8_path), ec);
}

inline std::string read_symlink_path(const std::string& utf8_path) {
  std::error_code ec;
  return path_to_utf8(std::filesystem::read_symlink(to_path(utf8_path), ec));
}

inline std::uintmax_t file_size_path(const std::string& utf8_path, std::error_code& ec) {
  return std::filesystem::file_size(to_path(utf8_path), ec);
}

}  // namespace waybar::util
