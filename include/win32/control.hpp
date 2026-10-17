#pragma once

// 控制通道：替代 POSIX 信号（SIGUSR1/SIGUSR2/SIGINT）。
//
// 每个 waybar 实例创建一个命名管道 `\\.\pipe\waybar-control-<pid>`，并在
// %LOCALAPPDATA%\waybar\instances 下登记一个实例文件；`waybar --toggle` 等命令
// 通过枚举实例文件找到存活实例并投递请求。

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace waybar::win32 {

struct ControlRequest {
  std::string command;  // toggle | show | hide | reload | refresh | quit | list-outputs
  std::string bar_id;   // 可选：只作用于指定 bar
  std::string style;    // reload 时可选
  std::string config;   // reload 时可选
};

class ControlServer {
 public:
  // handler 在服务线程被调用；返回应答文本（"OK" 或 "ERR ..."）。
  using Handler = std::function<std::string(const ControlRequest&)>;

  ControlServer();
  ~ControlServer();

  ControlServer(const ControlServer&) = delete;
  ControlServer& operator=(const ControlServer&) = delete;

  bool start(Handler handler, const std::string& config_path, const std::vector<std::string>& bars);
  void stop();

  int process_id() const;
  std::string pipe_name() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 客户端：向所有存活实例投递命令。返回成功处理的实例数。
int send_control_command(const ControlRequest& request);

// 列出当前登记的实例（每行一个可读描述），用于 --list-instances。
std::vector<std::string> list_instances();

}  // namespace waybar::win32
