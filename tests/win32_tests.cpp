// Windows 平台层单元测试（不依赖 catch2）。
//
// 覆盖：布局计算、UTF-8 路径/环境变量/通配展开、CreateProcess 执行与引号处理、
// 命名管道控制通道。运行：meson test -C build 或 build/win32-tests.exe。

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "util/fs.hpp"
#include "win32/common.hpp"
#include "win32/control.hpp"
#include "win32/exec.hpp"
#include "win32/layout.hpp"
#include "win32/paths.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool condition, const std::string& message) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

void check_eq(long long actual, long long expected, const std::string& message) {
  ++g_checks;
  if (actual != expected) {
    ++g_failures;
    std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual << ")\n";
  }
}

void check_contains(const std::string& haystack, const std::string& needle,
                    const std::string& message) {
  ++g_checks;
  if (haystack.find(needle) == std::string::npos) {
    ++g_failures;
    std::cerr << "FAIL: " << message << " (missing '" << needle << "' in '" << haystack << "')\n";
  }
}

// ---------------------------------------------------------------- layout

void test_layout() {
  using namespace waybar::win32;

  MonitorInfo monitor;
  monitor.geometry = {1920, 0, 1920, 1080};
  monitor.workarea = {1920, 0, 1920, 1032};

  {
    LayoutOptions options;  // top, full width, auto height
    const auto result = compute_layout(monitor, options, 400, 34);
    check_eq(result.x, 1920, "top: x anchored to monitor x");
    check_eq(result.y, 0, "top: y at monitor top");
    check_eq(result.width, 1920, "top: full monitor width");
    check_eq(result.height, 34, "top: height from content");
    check(result.edge == Edge::Top && !result.vertical, "top: edge/vertical");
  }

  {
    LayoutOptions options;
    options.position = "bottom";
    options.height = 40;
    const auto result = compute_layout(monitor, options, 400, 30);
    check_eq(result.y, 1080 - 40, "bottom: y at monitor bottom");
    check_eq(result.height, 40, "bottom: configured height wins");
  }

  {
    LayoutOptions options;
    options.position = "top";
    options.width = 800;
    options.height = 30;
    options.margin_top = 6;
    const auto result = compute_layout(monitor, options, 0, 0);
    check_eq(result.width, 800, "top+width: configured width");
    check_eq(result.x, 1920 + (1920 - 800) / 2, "top+width: horizontally centered");
    check_eq(result.y, 6, "top+width: margin applied");
  }

  {
    LayoutOptions options;
    options.position = "left";
    options.width = 48;
    options.margin_left = 4;
    const auto result = compute_layout(monitor, options, 0, 0);
    check(result.vertical, "left: vertical");
    check_eq(result.x, 1920 + 4, "left: margin applied");
    check_eq(result.width, 48, "left: configured width");
    check_eq(result.height, 1080, "left: full monitor height");
  }

  {
    LayoutOptions options;
    options.position = "right";
    options.width = 48;
    const auto result = compute_layout(monitor, options, 0, 0);
    check_eq(result.x, 1920 + 1920 - 48, "right: right-aligned");
  }
}

// ---------------------------------------------------------------- paths

std::string make_temp_dir() {
  wchar_t buffer[MAX_PATH] = {};
  GetTempPathW(MAX_PATH, buffer);
  std::wstring path = std::wstring(buffer) + L"waybar-win32-tests-";
  path += std::to_wstring(GetCurrentProcessId());
  std::filesystem::create_directories(std::filesystem::path(path));
  return waybar::win32::to_utf8(path);
}

void test_paths() {
  using namespace waybar::win32;

  SetEnvironmentVariableW(L"WAYBAR_TEST_VAR", L"hello-world");
  check_eq(static_cast<long long>(expand_env("%WAYBAR_TEST_VAR%").size()), 11,
           "expand_env: %VAR%");
  check(expand_env("$WAYBAR_TEST_VAR") == "hello-world", "expand_env: $VAR");
  check(expand_env("${WAYBAR_TEST_VAR}/x") == "hello-world\\x" ||
            expand_env("${WAYBAR_TEST_VAR}/x") == "hello-world/x",
        "expand_env: ${VAR}");
  check(expand_env("%WAYBAR_TEST_VAR_UNSET%").find('%') != std::string::npos,
        "expand_env: unknown %VAR% kept literal");

  const std::string home = home_dir();
  check(!home.empty(), "home_dir: non-empty");

  const std::string dir = make_temp_dir();
  check(file_exists(dir), "file_exists: temp dir");

  // 中文文件名（UTF-8 → UTF-16 路径处理）
  const std::string chinese_file = dir + "\\测试文件.txt";
  {
    auto out = waybar::util::open_ofstream(chinese_file, std::ios::binary);
    out << "waybar";
  }
  check(file_exists(chinese_file), "UTF-8 path: create + exists");

  const std::string text_file = dir + "\\sample-1.conf";
  { auto out = waybar::util::open_ofstream(text_file, std::ios::binary); out << "x"; }

  const auto matches = expand_glob(dir + "\\sample-*.conf");
  check_eq(static_cast<long long>(matches.size()), 1, "expand_glob: wildcard match");

  const auto none = expand_glob(dir + "\\nothing-*.conf");
  check(none.empty(), "expand_glob: no match -> empty");

  const std::vector<std::string> dirs = config_dirs();
  check(!dirs.empty(), "config_dirs: non-empty");

  const auto expanded = try_expand_path(dir, "sample-*.conf");
  check_eq(static_cast<long long>(expanded.size()), 1, "try_expand_path: wildcard");

  std::error_code ec;
  std::filesystem::remove_all(waybar::util::to_path(dir), ec);
}

// ---------------------------------------------------------------- exec

void test_exec() {
  using namespace waybar::win32;

  {
    std::string out;
    const auto code = run_capture("ver", "", out);
    check_eq(code, 0, "run_capture: ver exit code");
    check_contains(out, "Windows", "run_capture: ver output");
  }

  {
    std::string out;
    const auto code = run_capture("exit 7", "", out);
    check_eq(code, 7, "run_capture: exit code propagated");
  }

  {
    std::string out;
    const auto code = run_capture("echo %%WAYBAR_EXEC_TEST%%", "WAYBAR_EXEC_TEST", out);
    check_eq(code, 0, "run_capture: output name env");
  }

  {
    // 引号与空格：路径含空格和中文
    const std::string dir = make_temp_dir();
    const std::string file = dir + "\\a b 中文.txt";
    {
      auto out = waybar::util::open_ofstream(file, std::ios::binary);
      out << "quoted-ok";
    }
    std::string out;
    const auto code = run_capture("type \"" + file + "\"", "", out);
    check_eq(code, 0, "run_capture: quoted UTF-8 path exit code");
    check_contains(out, "quoted-ok", "run_capture: quoted UTF-8 path output");

    std::error_code ec;
    std::filesystem::remove_all(waybar::util::to_path(dir), ec);
  }

  {
    // 管道读取（custom 模块连续输出用）
    int32_t pid = 0;
    auto* pipe = run_piped("echo piped-line", "", pid);
    check(pipe != nullptr, "run_piped: opened");
    if (pipe != nullptr) {
      char buffer[128] = {};
      fgets(buffer, sizeof(buffer), pipe);
      check_contains(buffer, "piped-line", "run_piped: read line");
      fclose(pipe);
      kill_child(pid);
    }
  }
}

// ---------------------------------------------------------------- control

void test_control() {
  using namespace waybar::win32;

  std::atomic<int> handled{0};
  ControlServer server;
  const bool started = server.start(
      [&handled](const ControlRequest& request) -> std::string {
        ++handled;
        if (request.command == "toggle") return "OK toggle";
        return "ERR unknown";
      },
      "D:\\config.jsonc", {"bar-0"});
  check(started, "control: server started");
  if (!started) return;

  check(!server.pipe_name().empty(), "control: pipe name");

  ControlRequest request;
  request.command = "toggle";
  const int sent = send_control_command(request);
  check_eq(sent, 1, "control: command handled by one instance");

  // 等待服务线程处理
  for (int i = 0; i < 100 && handled == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  check_eq(handled.load(), 1, "control: handler invoked");

  const auto instances = list_instances();
  check(!instances.empty(), "control: instance registered");

  server.stop();
}

}  // namespace

int main(int argc, char** argv) {
  waybar::win32::setup_console();
  const std::string only = argc > 1 ? argv[1] : "";
  const auto want = [&only](const char* name) { return only.empty() || only == name; };

  std::cout << "== waybar win32 tests ==\n";
  std::cout.flush();
  if (want("layout")) { test_layout(); std::cout << "layout done\n"; std::cout.flush(); }
  if (want("paths")) { test_paths(); std::cout << "paths done\n"; std::cout.flush(); }
  if (want("exec")) { test_exec(); std::cout << "exec done\n"; std::cout.flush(); }
  if (want("control")) { test_control(); std::cout << "control done\n"; std::cout.flush(); }

  std::cout << (g_failures == 0 ? "OK" : "FAILED") << ": " << (g_checks - g_failures) << "/"
            << g_checks << " checks passed\n";
  return g_failures == 0 ? 0 : 1;
}
