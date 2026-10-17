#include "win32/exec.hpp"

#include <fcntl.h>
#include <io.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include <vector>

#include "win32/common.hpp"

namespace waybar::win32 {

namespace {

// 构造 UTF-16 环境块：继承当前环境，覆盖/追加 WAYBAR_OUTPUT_NAME。
std::vector<wchar_t> build_environment(const std::string& output_name) {
  std::vector<wchar_t> block;
  wchar_t* environment = GetEnvironmentStringsW();
  if (environment != nullptr) {
    for (const wchar_t* cursor = environment; *cursor != L'\0';) {
      const size_t len = wcslen(cursor);
      const wchar_t* equal = wcschr(cursor, L'=');
      bool skip = false;
      if (equal != nullptr) {
        const std::wstring name(cursor, equal);
        if (name == L"WAYBAR_OUTPUT_NAME") skip = true;
        // Windows 环境块以 '=' 开头的特殊变量需保留
        if (name.empty()) skip = false;
      }
      if (!skip) {
        block.insert(block.end(), cursor, cursor + len);
        block.push_back(L'\0');
      }
      cursor += len + 1;
    }
    FreeEnvironmentStringsW(environment);
  }

  if (!output_name.empty()) {
    const std::wstring entry = L"WAYBAR_OUTPUT_NAME=" + to_wide(output_name);
    block.insert(block.end(), entry.begin(), entry.end());
    block.push_back(L'\0');
  }

  block.push_back(L'\0');
  if (block.empty()) block.push_back(L'\0');
  return block;
}

struct ProcessHandle {
  HANDLE process = nullptr;
  HANDLE thread = nullptr;
  ~ProcessHandle() {
    if (thread != nullptr) CloseHandle(thread);
    if (process != nullptr) CloseHandle(process);
  }
};

bool spawn(const std::string& command, const std::string& output_name, HANDLE* stdout_write,
           DWORD creation_flags, ProcessHandle& out, DWORD* error_code) {
  std::wstring command_line = build_command_line(command);
  std::vector<wchar_t> environment = build_environment(output_name);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  if (stdout_write != nullptr) {
    startup.dwFlags |= STARTF_USESTDHANDLES;
    startup.hStdOutput = *stdout_write;
    startup.hStdError = *stdout_write;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  }

  PROCESS_INFORMATION info{};
  const BOOL ok = CreateProcessW(nullptr, command_line.data(), nullptr, nullptr,
                                 stdout_write != nullptr ? TRUE : FALSE,
                                 creation_flags | CREATE_UNICODE_ENVIRONMENT, environment.data(),
                                 nullptr, &startup, &info);
  if (!ok) {
    const DWORD code = GetLastError();
    if (error_code != nullptr) *error_code = code;
    spdlog::warn("CreateProcessW failed for '{}': {}", command, last_error(code));
    return false;
  }

  out.process = info.hProcess;
  out.thread = info.hThread;

  if (job_object() != nullptr) {
    AssignProcessToJobObject(job_object(), info.hProcess);
  }
  return true;
}

}  // namespace

std::wstring build_command_line(const std::string& command) {
  // 与 `sh -c` 对应：交给 cmd.exe。用 /d 跳过 AutoRun，/s 保留引号语义。
  std::wstring line = L"cmd.exe /d /s /c \"";
  line += to_wide(command);
  line += L"\"";
  return line;
}

int run_capture(const std::string& command, const std::string& output_name,
                std::string& output) {
  output.clear();
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
    spdlog::warn("CreatePipe failed: {}", last_error());
    return -1;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  ProcessHandle process;
  DWORD spawn_error = 0;
  if (!spawn(command, output_name, &write_pipe, 0, process, &spawn_error)) {
    CloseHandle(read_pipe);
    CloseHandle(write_pipe);
    return -1;
  }
  CloseHandle(write_pipe);

  char buffer[4096];
  DWORD read = 0;
  while (ReadFile(read_pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
    output.append(buffer, read);
  }
  CloseHandle(read_pipe);

  WaitForSingleObject(process.process, INFINITE);
  DWORD exit_code = static_cast<DWORD>(-1);
  GetExitCodeProcess(process.process, &exit_code);

  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
  return static_cast<int>(exit_code);
}

FILE* run_piped(const std::string& command, const std::string& output_name, int32_t& pid_out) {
  pid_out = 0;
  HANDLE read_pipe = nullptr;
  HANDLE write_pipe = nullptr;
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.bInheritHandle = TRUE;
  if (!CreatePipe(&read_pipe, &write_pipe, &attributes, 0)) {
    spdlog::warn("CreatePipe failed: {}", last_error());
    return nullptr;
  }
  SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

  ProcessHandle process;
  if (!spawn(command, output_name, &write_pipe, 0, process, nullptr)) {
    CloseHandle(read_pipe);
    CloseHandle(write_pipe);
    return nullptr;
  }
  CloseHandle(write_pipe);

  pid_out = static_cast<int32_t>(process.process != nullptr ? GetProcessId(process.process) : 0);
  // 句柄交给 runner 管理：FILENO 包装后由 fclose 关闭
  const int file_descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(read_pipe), _O_RDONLY);
  if (file_descriptor == -1) {
    CloseHandle(read_pipe);
    return nullptr;
  }
  FILE* file = _fdopen(file_descriptor, "rb");
  if (file == nullptr) {
    _close(file_descriptor);
    return nullptr;
  }
  // 让进程句柄随 FILE* 生命周期关闭：把 PID 记到 map 中由 wait_exit_code 使用
  // （此处只保留 PID，句柄在进程退出后由系统回收；wait_exit_code 重新 OpenProcess）
  return file;
}

int32_t run_detached(const std::string& command, const std::string& output_name) {
  ProcessHandle process;
  if (!spawn(command, output_name, nullptr, 0, process, nullptr)) return 0;
  return static_cast<int32_t>(GetProcessId(process.process));
}

int wait_exit_code(int32_t pid, int timeout_ms) {
  if (pid <= 0) return -1;
  HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(pid));
  if (process == nullptr) return -1;
  const DWORD wait = WaitForSingleObject(process, timeout_ms < 0 ? INFINITE : timeout_ms);
  int exit_code = -1;
  if (wait == WAIT_OBJECT_0) {
    DWORD code = 0;
    if (GetExitCodeProcess(process, &code)) exit_code = static_cast<int>(code);
  }
  CloseHandle(process);
  return exit_code;
}

void kill_child(int32_t pid) {
  if (pid <= 0) return;
  HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
  if (process == nullptr) return;
  TerminateProcess(process, 1);
  CloseHandle(process);
}

}  // namespace waybar::win32
