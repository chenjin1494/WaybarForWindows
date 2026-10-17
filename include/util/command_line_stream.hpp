#pragma once

#include <glibmm/main.h>
#include <glibmm/spawn.h>

#include <cstdio>
#include <functional>
#include <string>

#ifdef _WIN32
#include <atomic>
#include <cstdint>
#include <thread>
#endif

namespace waybar::util::command {

class LineStream {
 public:
  using OutputCallback = std::function<void(const std::string&)>;
  using ExitCallback = std::function<void(int)>;

  LineStream(std::string output_name, OutputCallback on_output, ExitCallback on_exit);
  ~LineStream();

  void start(const std::string& cmd);
  void stop();
  bool running() const;

 private:
#ifdef _WIN32
  void readerLoop();
  std::atomic<bool> running_{false};
  std::FILE* file_ = nullptr;
  int32_t pid_ = 0;
  std::thread reader_;
#else
  bool handleStdout(Glib::IOCondition condition);
  void handleExit(Glib::Pid pid, int status);
  void closeStdout();
  void drainStdout(bool flush_trailing_line);
  static int statusToExitCode(int status);

  Glib::Pid pid_;
  int stdout_fd_;
  sigc::connection stdout_connection_;
  sigc::connection child_connection_;
#endif

  std::string output_name_;
  OutputCallback on_output_;
  ExitCallback on_exit_;
  std::string buffer_;
};

}  // namespace waybar::util::command
