#include "win32/paths.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

#include "util/fs.hpp"
#include "win32/common.hpp"

namespace fs = std::filesystem;

namespace waybar::win32 {

namespace {

std::string getenv_string(const char* name) { return win32::get_env(name); }

bool has_wildcard(const std::string& s) {
  return s.find('*') != std::string::npos || s.find('?') != std::string::npos;
}

std::string trim_trailing_separators(std::string path) {
  while (path.size() > 1 && (path.back() == '\\' || path.back() == '/')) {
    // 保留 "C:\" 这类根
    if (path.size() == 3 && path[1] == ':') break;
    path.pop_back();
  }
  return path;
}

// 目录枚举：pattern 的最后一段含通配，base 为已展开的目录
std::vector<std::string> enumerate_matching(const std::string& base, const std::string& pattern) {
  std::vector<std::string> results;
  const std::wstring wide_pattern =
      (waybar::util::to_path(base) / waybar::util::to_path(pattern)).wstring();
  WIN32_FIND_DATAW data{};
  HANDLE handle = FindFirstFileW(wide_pattern.c_str(), &data);
  if (handle == INVALID_HANDLE_VALUE) return results;
  do {
    const std::wstring name = data.cFileName;
    if (name == L"." || name == L"..") continue;
    results.push_back(waybar::util::path_to_utf8(
        waybar::util::to_path(base) / std::filesystem::path(name)));
  } while (FindNextFileW(handle, &data));
  FindClose(handle);
  return results;
}

// 极简通配匹配（* 与 ?），用于过滤 FindFirstFile 的短名结果
bool wildcard_match(const std::string& pattern, const std::string& text) {
  size_t p = 0, t = 0;
  size_t star = std::string::npos, mark = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      mark = t;
    } else if (star != std::string::npos) {
      p = star + 1;
      t = ++mark;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') ++p;
  return p == pattern.size();
}

}  // namespace

std::string home_dir() {
  std::string home = getenv_string("USERPROFILE");
  if (home.empty()) {
    std::string drive = getenv_string("HOMEDRIVE");
    std::string path = getenv_string("HOMEPATH");
    home = drive + path;
  }
  if (home.empty()) home = getenv_string("HOME");
  return trim_trailing_separators(home);
}

std::string config_home() {
  std::string xdg = getenv_string("XDG_CONFIG_HOME");
  if (!xdg.empty()) return trim_trailing_separators(xdg);
  std::string appdata = getenv_string("APPDATA");
  if (!appdata.empty()) return trim_trailing_separators(appdata);
  return home_dir() + "\\AppData\\Roaming";
}

std::string local_appdata() {
  std::string value = getenv_string("LOCALAPPDATA");
  if (!value.empty()) return trim_trailing_separators(value);
  return home_dir() + "\\AppData\\Local";
}

std::string exe_dir() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const DWORD len = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (len == 0) return {};
    if (len < buffer.size()) {
      buffer.resize(len);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  return trim_trailing_separators(to_utf8(buffer.substr(0, buffer.find_last_of(L"\\/"))));
}

std::string expand_env(const std::string& input) {
  std::string out;
  out.reserve(input.size());

  for (size_t i = 0; i < input.size();) {
    const char c = input[i];

    // ~ 仅作为路径开头时展开
    if (c == '~' && i == 0 && (input.size() == 1 || input[1] == '/' || input[1] == '\\')) {
      out += home_dir();
      ++i;
      continue;
    }

    // %VAR%
    if (c == '%') {
      const size_t end = input.find('%', i + 1);
      if (end != std::string::npos) {
        const std::string name = input.substr(i + 1, end - i - 1);
        const std::string value = getenv_string(name.c_str());
        if (!value.empty()) {
          out += value;
          i = end + 1;
          continue;
        }
      }
      out.push_back(c);
      ++i;
      continue;
    }

    // $VAR / ${VAR}
    if (c == '$') {
      size_t begin = i + 1;
      size_t end = begin;
      bool brace = false;
      if (begin < input.size() && input[begin] == '{') {
        brace = true;
        ++begin;
        end = input.find('}', begin);
      } else {
        while (end < input.size() &&
               (std::isalnum(static_cast<unsigned char>(input[end])) || input[end] == '_')) {
          ++end;
        }
      }
      if (end != std::string::npos && end > begin) {
        const std::string name = input.substr(begin, end - begin);
        const std::string value = getenv_string(name.c_str());
        if (!value.empty()) {
          out += value;
          i = brace ? end + 1 : end;
          continue;
        }
      }
      out.push_back(c);
      ++i;
      continue;
    }

    out.push_back(c);
    ++i;
  }

  return out;
}

std::vector<std::string> expand_glob(const std::string& raw_pattern) {
  const std::string pattern = expand_env(raw_pattern);
  if (pattern.empty()) return {};

  std::vector<std::string> current;
  std::string rest = pattern;

  // 拆分根与前缀
  std::string prefix;
  if (rest.size() >= 2 && std::isalpha(static_cast<unsigned char>(rest[0])) && rest[1] == ':') {
    prefix = rest.substr(0, 2);
    rest = rest.substr(2);
  }
  if (!rest.empty() && (rest[0] == '\\' || rest[0] == '/')) {
    prefix += "\\";
    rest.erase(0, 1);
  }

  current.push_back(prefix.empty() ? std::string(".") : prefix);

  size_t pos = 0;
  while (pos <= rest.size()) {
    size_t sep = rest.find_first_of("\\/", pos);
    const std::string part =
        sep == std::string::npos ? rest.substr(pos) : rest.substr(pos, sep - pos);
    const bool last = (sep == std::string::npos);

    if (!part.empty() && part != ".") {
      std::vector<std::string> next;
      for (const auto& base : current) {
        if (has_wildcard(part)) {
          for (const auto& candidate : enumerate_matching(base, part)) {
            if (wildcard_match(part, waybar::util::path_to_utf8(
                                         waybar::util::to_path(candidate).filename()))) {
              next.push_back(candidate);
            }
          }
        } else {
          next.push_back(
              waybar::util::path_to_utf8(waybar::util::to_path(base) / waybar::util::to_path(part)));
        }
      }
      current.swap(next);
      if (current.empty()) return {};
    }

    if (last) break;
    pos = sep + 1;
  }

  std::vector<std::string> results;
  for (auto& item : current) {
    if (file_exists(item)) results.push_back(item);
  }
  std::sort(results.begin(), results.end());
  results.erase(std::unique(results.begin(), results.end()), results.end());
  return results;
}

bool file_exists(const std::string& utf8_path) { return waybar::util::path_exists(utf8_path); }

std::string parent_path(const std::string& path) {
  return waybar::util::path_to_utf8(waybar::util::to_path(path).parent_path());
}

std::string filename_part(const std::string& path) {
  return waybar::util::path_to_utf8(waybar::util::to_path(path).filename());
}

std::vector<std::string> config_dirs() {
  std::vector<std::string> dirs;
  const std::string home = home_dir();
  const std::string xdg = getenv_string("XDG_CONFIG_HOME");

  // 与上游 CONFIG_DIRS 对齐，并补充 Windows 惯例目录
  if (!xdg.empty()) dirs.push_back(trim_trailing_separators(xdg) + "\\waybar");
  if (!home.empty()) {
    dirs.push_back(home + "\\.config\\waybar");
    dirs.push_back(home + "\\waybar");
  }
  const std::string roaming = getenv_string("APPDATA");
  if (!roaming.empty()) dirs.push_back(trim_trailing_separators(roaming) + "\\waybar");

  const std::string exe = exe_dir();
  if (!exe.empty()) {
    dirs.push_back(exe + "\\resources");
    dirs.push_back(exe + "\\..\\etc\\xdg\\waybar");
  }
  return dirs;
}

std::vector<std::string> try_expand_path(const std::string& base, const std::string& filename) {
  std::string joined;
  if (filename.empty()) {
    joined = base;
  } else if (base.empty()) {
    joined = filename;
  } else {
    joined = trim_trailing_separators(base) + "\\" + filename;
  }
  return expand_glob(joined);
}

}  // namespace waybar::win32
