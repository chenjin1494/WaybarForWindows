#pragma once

// 子进程执行（替代 POSIX 的 fork/exec + /bin/sh -c）。
//
// 约定：
//   - 命令字符串是 UTF-8，最终以 UTF-16 传给 CreateProcessW；
//   - 通过 cmd.exe /d /s /c 执行（与上游的 `sh -c` 语义对应）；
//   - 所有子进程挂在 Job Object 上（见 common.hpp init_job_object），bar 退出即回收；
//   - WAYBAR_OUTPUT_NAME 通过环境块注入。

#include <cstdio>
#include <cstdint>
#include <string>

namespace waybar::win32 {

// 同步执行并捕获 stdout；返回退出码（失败为负）。
int run_capture(const std::string& command, const std::string& output_name, std::string& output);

// 异步执行并从 stdout 读取；返回 FILE*（失败为 nullptr），pid_out 返回 Windows PID。
FILE* run_piped(const std::string& command, const std::string& output_name, int32_t& pid_out);

// 异步执行、不关心输出；返回 Windows PID（失败为 0）。
int32_t run_detached(const std::string& command, const std::string& output_name);

// 等待退出；timeout_ms < 0 表示无限等待。返回退出码，失败为 -1。
int wait_exit_code(int32_t pid, int timeout_ms = -1);

// 结束子进程（等价 killpg 的粗略替代）。
void kill_child(int32_t pid);

// 构造 "cmd.exe /d /s /c <command>" 的 UTF-16 命令行。
std::wstring build_command_line(const std::string& command);

}  // namespace waybar::win32
