#pragma once

// 配置路径解析与 shell 风格展开（替代 POSIX 的 wordexp/glob）。
//
// 支持：
//   ~            -> %USERPROFILE%
//   $VAR ${VAR}  -> 环境变量
//   %VAR%        -> Windows 风格环境变量
//   * ?          -> 文件名通配（可出现在任意路径层级）

#include <string>
#include <vector>

namespace waybar::win32 {

// 展开环境变量与 ~，不做通配。
std::string expand_env(const std::string& input);

// 展开 + 通配；返回存在的路径（按字典序）。无匹配返回空。
std::vector<std::string> expand_glob(const std::string& pattern);

// 相对路径按当前工作目录解析；返回是否存在。
bool file_exists(const std::string& utf8_path);

// 上游 Config::CONFIG_DIRS 的 Windows 等价物（未展开）。
std::vector<std::string> config_dirs();

// 上游 Config::tryExpandPath 的 Windows 等价物。
std::vector<std::string> try_expand_path(const std::string& base, const std::string& filename);

// 常用目录（已展开）
std::string home_dir();
std::string config_home();
std::string local_appdata();
std::string exe_dir();

// 目录部分 / 文件名部分
std::string parent_path(const std::string& path);
std::string filename_part(const std::string& path);

}  // namespace waybar::win32
