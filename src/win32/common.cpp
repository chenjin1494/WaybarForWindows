#include "win32/common.hpp"

#include <shellscalingapi.h>
#include <spdlog/spdlog.h>

#include <vector>

namespace waybar::win32 {

namespace {
HANDLE g_job = nullptr;
}  // namespace

std::wstring to_wide(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
  if (len <= 0) {
    // 非法 UTF-8：退回宽松转换，避免直接丢弃路径
    const int fallback_len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                                 static_cast<int>(utf8.size()), nullptr, 0);
    if (fallback_len <= 0) return {};
    std::wstring out(static_cast<size_t>(fallback_len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(),
                        fallback_len);
    return out;
  }
  std::wstring out(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()),
                      out.data(), len);
  return out;
}

std::string to_utf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  const int len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
  if (len <= 0) return {};
  std::string out(static_cast<size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), len,
                      nullptr, nullptr);
  return out;
}

std::string to_utf8(const wchar_t* wide) {
  if (wide == nullptr) return {};
  return to_utf8(std::wstring(wide));
}

std::string last_error(DWORD code) {
  if (code == 0) code = GetLastError();
  wchar_t* buffer = nullptr;
  const DWORD len = FormatMessageW(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
  std::string message;
  if (len != 0 && buffer != nullptr) {
    message = to_utf8(std::wstring(buffer, len));
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
      message.pop_back();
    }
  }
  if (buffer != nullptr) LocalFree(buffer);
  char suffix[24];
  snprintf(suffix, sizeof(suffix), " (0x%08lx)", static_cast<unsigned long>(code));
  return message + suffix;
}

void setup_console() {
  // 控制台可能不存在（GUI 启动），全部失败均可忽略
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
}

std::string get_env(const std::string& name) {
  const std::wstring wide_name = to_wide(name);
  DWORD size = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
  if (size == 0) return {};
  std::wstring buffer(size, L'\0');
  const DWORD written = GetEnvironmentVariableW(wide_name.c_str(), buffer.data(), size);
  if (written == 0) return {};
  buffer.resize(written);
  return to_utf8(buffer);
}

void enable_dpi_awareness() {
  // 优先 PER_MONITOR_AWARE_V2；旧系统退化为 per-monitor / system aware。
  if (SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
  if (SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE) == S_OK) return;
  SetProcessDPIAware();
}

void init_job_object() {
  if (g_job != nullptr) return;
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job == nullptr) {
    spdlog::warn("CreateJobObject failed: {}", last_error());
    return;
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
    spdlog::warn("SetInformationJobObject failed: {}", last_error());
    CloseHandle(job);
    return;
  }
  if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
    // 进程可能已在别的 job 中（例如由某些启动器托管）；此时仍保留 job 供子进程使用。
    spdlog::debug("AssignProcessToJobObject failed: {}", last_error());
  }
  g_job = job;
}

HANDLE job_object() { return g_job; }

}  // namespace waybar::win32
