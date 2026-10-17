#pragma once

#include <spdlog/spdlog.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>

#ifdef _WIN32
// Windows 侧实现见 src/win32/exec.cpp
#include "win32/exec.hpp"

// 上游用 WEXITSTATUS(command::close(...)) 取退出码；Windows 上 close 直接返回退出码。
#define WB_EXIT_CODE(status) (status)
#else
#include <fcntl.h>
#include <giomm.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifdef __FreeBSD__
#include <sys/procctl.h>
#endif

#include <list>

#define WB_EXIT_CODE(status) WEXITSTATUS(status)
#endif

namespace waybar::util::command {

constexpr int kExecFailureExitCode = 127;

struct res {
  int exit_code = -1;
  std::string out;
};

inline std::string read(FILE* fp) {
  std::array<char, 128> buffer = {0};
  std::string output;
  while (fgets(buffer.data(), buffer.size(), fp) != nullptr) {
    output += buffer.data();
  }
  if (ferror(fp) != 0) {
    spdlog::error("Error reading command output: {}", strerror(errno));
  }

  // Remove last newline
  if (!output.empty() && output[output.length() - 1] == '\n') {
    output.erase(output.length() - 1);
  }
  return output;
}

// 跨平台按行读取（替代 POSIX getline）：成功返回 true，EOF 返回 false。
inline bool read_line(FILE* fp, std::string& line) {
  std::array<char, 4096> buffer = {0};
  line.clear();
  bool any = false;
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), fp) != nullptr) {
    any = true;
    line += buffer.data();
    if (!line.empty() && line.back() == '\n') break;
  }
  return any;
}

#ifdef _WIN32

// 等待子进程退出并返回退出码（由 close 使用）。
inline int close(FILE* fp, int32_t pid) {
  if (fp != nullptr) fclose(fp);
  const int code = waybar::win32::wait_exit_code(pid, -1);
  spdlog::debug("Cmd exited with code {}", code);
  return code;
}

// 打开一个子进程并按行读取 stdout。
inline FILE* open(const std::string& cmd, int32_t& pid, const std::string& output_name) {
  if (cmd.empty()) return nullptr;
  FILE* file = waybar::win32::run_piped(cmd, output_name, pid);
  if (file == nullptr) {
    spdlog::error("Unable to exec cmd {}", cmd);
    return nullptr;
  }
  return file;
}

inline struct res exec(const std::string& cmd, const std::string& output_name) {
  struct res result;
  result.exit_code = waybar::win32::run_capture(cmd, output_name, result.out);
  return result;
}

inline struct res execNoRead(const std::string& cmd) {
  struct res result;
  result.exit_code = waybar::win32::run_capture(cmd, "", result.out);
  return result;
}

inline int32_t forkExec(const std::string& cmd, const std::string& output_name) {
  if (cmd.empty()) return -1;
  const int32_t pid = waybar::win32::run_detached(cmd, output_name);
  if (pid == 0) {
    spdlog::error("Unable to exec cmd {}", cmd);
    return -1;
  }
  spdlog::debug("Spawned child with PID: {}", pid);
  return pid;
}

inline int32_t forkExec(const std::string& cmd) { return forkExec(cmd, ""); }

// 非阻塞检查子进程是否退出；已退出（或已不存在）返回 true 并写入退出码。
inline bool reap_child(int32_t pid, int& status) {
  if (pid <= 0) return true;
  const int code = waybar::win32::wait_exit_code(pid, 0);
  status = code < 0 ? 0 : code;
  return true;
}

inline void killChild(int32_t pid) { waybar::win32::kill_child(pid); }

#else  // !_WIN32

extern std::mutex reap_mtx;
extern std::list<pid_t> reap;

inline int close(FILE* fp, pid_t pid) {
  int stat = -1;
  pid_t ret;

  fclose(fp);
  do {
    ret = waitpid(pid, &stat, WCONTINUED | WUNTRACED);

    if (WIFEXITED(stat)) {
      spdlog::debug("Cmd exited with code {}", WEXITSTATUS(stat));
    } else if (WIFSIGNALED(stat)) {
      spdlog::debug("Cmd killed by {}", WTERMSIG(stat));
    } else if (WIFSTOPPED(stat)) {
      spdlog::debug("Cmd stopped by {}", WSTOPSIG(stat));
    } else if (WIFCONTINUED(stat)) {
      spdlog::debug("Cmd continued");
    } else if (ret == -1) {
      spdlog::debug("waitpid failed: {}", strerror(errno));
      break;
    } else {
      break;
    }
  } while (!WIFEXITED(stat) && !WIFSIGNALED(stat));
  return stat;
}

inline FILE* open(const std::string& cmd, int& pid, const std::string& output_name) {
  if (cmd == "") return nullptr;
  int fd[2];
  // Open the pipe with the close-on-exec flag set, so it will not be inherited
  // by any other subprocesses launched by other threads (which could result in
  // the pipe staying open after this child dies, causing us to hang when trying
  // to read from it)
  if (pipe2(fd, O_CLOEXEC) != 0) {
    spdlog::error("Unable to pipe fd");
    return nullptr;
  }

  pid_t child_pid = fork();

  if (child_pid < 0) {
    spdlog::error("Unable to exec cmd {}, error {}", cmd.c_str(), strerror(errno));
    ::close(fd[0]);
    ::close(fd[1]);
    return nullptr;
  }

  if (!child_pid) {
    int err;
    sigset_t mask;
    sigfillset(&mask);
    // Reset sigmask
    err = pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
    if (err != 0) spdlog::error("pthread_sigmask in open failed: {}", strerror(err));
    // Kill child if Waybar exits
    int deathsig = SIGTERM;
#ifdef __linux__
    if (prctl(PR_SET_PDEATHSIG, deathsig) != 0) {
      spdlog::error("prctl(PR_SET_PDEATHSIG) in open failed: {}", strerror(errno));
    }
#endif
#ifdef __FreeBSD__
    if (procctl(P_PID, 0, PROC_PDEATHSIG_CTL, reinterpret_cast<void*>(&deathsig)) == -1) {
      spdlog::error("procctl(PROC_PDEATHSIG_CTL) in open failed: {}", strerror(errno));
    }
#endif
    ::close(fd[0]);
    dup2(fd[1], 1);
    setpgid(child_pid, child_pid);
    if (!output_name.empty()) {
      setenv("WAYBAR_OUTPUT_NAME", output_name.c_str(), 1);
    }
    execlp("/bin/sh", "sh", "-c", cmd.c_str(), (char*)0);
    const int saved_errno = errno;
    spdlog::error("execlp(/bin/sh) failed in open: {}", strerror(saved_errno));
    _exit(kExecFailureExitCode);
  } else {
    ::close(fd[1]);
  }
  pid = child_pid;
  return fdopen(fd[0], "r");
}

inline struct res exec(const std::string& cmd, const std::string& output_name) {
  int pid = 0;
  auto fp = command::open(cmd, pid, output_name);
  if (!fp) return {-1, ""};
  auto output = command::read(fp);
  auto stat = command::close(fp, pid);
  return {WEXITSTATUS(stat), output};
}

inline struct res execNoRead(const std::string& cmd) {
  int pid = 0;
  auto fp = command::open(cmd, pid, "");
  if (!fp) return {-1, ""};
  auto stat = command::close(fp, pid);
  return {WEXITSTATUS(stat), ""};
}

inline int32_t forkExec(const std::string& cmd, const std::string& output_name) {
  if (cmd == "") return -1;

  pid_t pid = fork();

  if (pid < 0) {
    spdlog::error("Unable to exec cmd {}, error {}", cmd.c_str(), strerror(errno));
    return pid;
  }

  // Child executes the command
  if (!pid) {
    int err;
    sigset_t mask;
    sigfillset(&mask);
    // Reset sigmask
    err = pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
    if (err != 0) spdlog::error("pthread_sigmask in forkExec failed: {}", strerror(err));
    setpgid(pid, pid);
    if (!output_name.empty()) {
      setenv("WAYBAR_OUTPUT_NAME", output_name.c_str(), 1);
    }
    execl("/bin/sh", "sh", "-c", cmd.c_str(), (char*)0);
    const int saved_errno = errno;
    spdlog::error("execl(/bin/sh) failed in forkExec: {}", strerror(saved_errno));
    _exit(kExecFailureExitCode);
  } else {
    reap_mtx.lock();
    reap.push_back(pid);
    reap_mtx.unlock();
    spdlog::debug("Added child to reap list: {}", pid);
  }

  return pid;
}

inline int32_t forkExec(const std::string& cmd) { return forkExec(cmd, ""); }

inline void killChild(int32_t pid) {
  if (pid > 0) killpg(pid, SIGTERM);
}

#endif  // _WIN32

}  // namespace waybar::util::command
