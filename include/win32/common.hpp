#pragma once

// Windows 平台层通用工具：UTF 转换、错误串、控制台、DPI、Job Object。
//
// 约定：平台层对外接口一律使用 UTF-8（std::string），仅在调用 Win32 的 *W API 时
// 转成 UTF-16，避免中文用户名/路径（如 C:\Users\琛瑾喵呜）出现问题。

#include <windows.h>

#include <string>

namespace waybar::win32 {

std::wstring to_wide(const std::string& utf8);
std::string to_utf8(const std::wstring& wide);
std::string to_utf8(const wchar_t* wide);

// 根据 GetLastError()（或给定错误码）生成 "message (0x...)" 形式描述。
std::string last_error(DWORD code = 0);

// 控制台输出切到 UTF-8；失败时静默（例如无控制台）。
void setup_console();

// 读取环境变量（UTF-8 值）。Windows 的 std::getenv 返回 ANSI 编码，
// 含中文的 %LOCALAPPDATA% 等会乱码，因此统一走 GetEnvironmentVariableW。
std::string get_env(const std::string& name);

// 进程级 DPI 感知：PER_MONITOR_AWARE_V2（必须在 gtk_init 之前调用）。
void enable_dpi_awareness();

// 创建 Job Object 并放进当前进程；bar 退出时子进程随之结束（替代 killpg）。
void init_job_object();
HANDLE job_object();

}  // namespace waybar::win32
