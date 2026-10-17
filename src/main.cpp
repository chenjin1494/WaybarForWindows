#include <spdlog/spdlog.h>

#include "bar.hpp"
#include "client.hpp"

#ifdef _WIN32

// Windows：没有 POSIX 信号，控制命令通过命名管道（waybar --toggle/--reload/...）下发，
// 具体处理在 Client 内部完成。这里只负责「请求 reload 时重新初始化」的循环。

#include <shellapi.h>
#include <windows.h>

#include <string>
#include <vector>

#include "win32/common.hpp"

namespace {

// MinGW 的 main(argc, argv) 会把命令行按 ANSI 代码页转换，含中文的路径（如
// C:\Users\<中文>\...）会损坏。这里改用 CommandLineToArgvW 拿 UTF-16，
// 再逐项转为 UTF-8，保证 --config/--style 与开机启动命令中的中文路径可用。
std::vector<std::string> g_args_storage;
std::vector<char*> g_argv_storage;

void rebuild_args_from_wide() {
  int argc = 0;
  LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (wide == nullptr || argc <= 0) {
    return;
  }

  g_args_storage.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    g_args_storage.push_back(waybar::win32::to_utf8(wide[i]));
  }
  LocalFree(wide);

  g_argv_storage.reserve(g_args_storage.size() + 1);
  for (auto& arg : g_args_storage) {
    g_argv_storage.push_back(arg.data());
  }
  g_argv_storage.push_back(nullptr);
}

}  // namespace

int main(int /*argc*/, char** /*argv*/) {
  try {
    rebuild_args_from_wide();
    int argc = static_cast<int>(g_args_storage.size());
    char** argv = g_argv_storage.empty() ? nullptr : g_argv_storage.data();

    auto* client = waybar::Client::inst();
    int ret = 0;
    do {
      ret = client->main(argc, argv);
    } while (ret == 0 && client->takeReloadRequest());
    return ret;
  } catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  } catch (const Glib::Exception& e) {
    spdlog::error("{}", static_cast<std::string>(e.what()));
    return 1;
  }
}

#else

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <csignal>
#include <list>
#include <mutex>

#ifdef HAVE_LIBSYSTEMD
#include <spdlog/sinks/systemd_sink.h>
#include <sys/stat.h>

#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <system_error>
#endif

#include "util/SafeSignal.hpp"

std::mutex reap_mtx;
std::list<pid_t> reap;

static int signal_pipe_write_fd;

// Write a single signal to `signal_pipe_write_fd`.
// This function is set as a signal handler, so it must be async-signal-safe.
static void writeSignalToPipe(int signum) {
  ssize_t amt = write(signal_pipe_write_fd, &signum, sizeof(int));

  // There's not much we can safely do inside of a signal handler.
  // Let's just ignore any errors.
  (void)amt;
}

// This initializes `signal_pipe_write_fd`, and sets up signal handlers.
//
// This function will run forever, emitting every `SIGUSR1`, `SIGUSR2`,
// `SIGINT`, `SIGCHLD`, and `SIGRTMIN + 1`...`SIGRTMAX` signal received
// to `signal_handler`.
static void catchSignals(waybar::SafeSignal<int>& signal_handler) {
  int fd[2];
  if (pipe(fd) != 0) {
    spdlog::error("Failed to create signal pipe: {}", strerror(errno));
    return;
  }

  int signal_pipe_read_fd = fd[0];
  signal_pipe_write_fd = fd[1];

  // This pipe should be able to buffer ~thousands of signals. If it fills up,
  // we'll drop signals instead of blocking.

  // We can't allow the write end to block because we'll be writing to it in a
  // signal handler, which could interrupt the loop that's reading from it and
  // deadlock.

  fcntl(signal_pipe_write_fd, F_SETFL, O_NONBLOCK);

  std::signal(SIGUSR1, writeSignalToPipe);
  std::signal(SIGUSR2, writeSignalToPipe);
  std::signal(SIGINT, writeSignalToPipe);
  std::signal(SIGCHLD, writeSignalToPipe);

#ifdef SIGRTMIN
  for (int sig = SIGRTMIN + 1; sig <= SIGRTMAX; ++sig) {
    std::signal(sig, writeSignalToPipe);
  }
#endif

  while (true) {
    int signum;
    ssize_t amt = read(signal_pipe_read_fd, &signum, sizeof(int));
    if (amt < 0) {
      spdlog::error("read from signal pipe failed with error {}, closing thread", strerror(errno));
      break;
    }

    if (amt != sizeof(int)) {
      continue;
    }

    signal_handler.emit(signum);
  }
}

waybar::util::KillSignalAction getActionForBar(waybar::Bar* bar, int signal) {
  switch (signal) {
    case SIGUSR1:
      return bar->getOnSigusr1Action();
    case SIGUSR2:
      return bar->getOnSigusr2Action();
    default:
      return waybar::util::KillSignalAction::NOOP;
  }
}

void handleUserSignal(int signal, bool& reload) {
  int i = 0;
  for (auto& bar : waybar::Client::inst()->bars) {
    switch (getActionForBar(bar.get(), signal)) {
      case waybar::util::KillSignalAction::HIDE:
        spdlog::debug("Visibility 'hide' for bar ", i);
        bar->hide();
        break;
      case waybar::util::KillSignalAction::SHOW:
        spdlog::debug("Visibility 'show' for bar ", i);
        bar->show();
        break;
      case waybar::util::KillSignalAction::TOGGLE:
        spdlog::debug("Visibility 'toggle' for bar ", i);
        bar->toggle();
        break;
      case waybar::util::KillSignalAction::RELOAD:
        spdlog::info("Reloading...");
        reload = true;
        waybar::Client::inst()->reset();
        return;
      case waybar::util::KillSignalAction::NOOP:
        break;
    }
    i++;
  }
}

// Must be called on the main thread.
//
// If this signal should restart or close the bar, this function will write
// `true` or `false`, respectively, into `reload`.
static void handleSignalMainThread(int signum, bool& reload) {
#ifdef SIGRTMIN
  if (signum >= SIGRTMIN + 1 && signum <= SIGRTMAX) {
    for (auto& bar : waybar::Client::inst()->bars) {
      bar->handleSignal(signum);
    }
    return;
  }
#endif

  switch (signum) {
    case SIGUSR1:
      handleUserSignal(SIGUSR1, reload);
      break;
    case SIGUSR2:
      handleUserSignal(SIGUSR2, reload);
      break;
    case SIGINT:
      spdlog::info("Quitting.");
      reload = false;
      waybar::Client::inst()->reset();
      break;
    case SIGCHLD:
      spdlog::debug("Received SIGCHLD in signalThread");
      {
        std::lock_guard<std::mutex> lock(reap_mtx);
        for (auto it = reap.begin(); it != reap.end();) {
          if (waitpid(*it, nullptr, WNOHANG) == *it) {
            spdlog::debug("Reaped child with PID: {}", *it);
            it = reap.erase(it);
          } else {
            ++it;
          }
        }
      }
      break;
    default:
      spdlog::debug("Received signal with number {}, but not handling", signum);
      break;
  }
}

static void logToJournalIfRunAsService() {
#ifdef HAVE_LIBSYSTEMD
  /* Implementation of automatic protocol upgrading (from stderr to journal)
  ** as described in https://systemd.io/JOURNAL_NATIVE_PROTOCOL */
  char const* journal_stream = std::getenv("JOURNAL_STREAM");

  if (journal_stream != nullptr) {
    dev_t device;
    ino_t inode;
    size_t len = std::strlen(journal_stream);

    auto result = std::from_chars(journal_stream, journal_stream + len, device);
    if (result.ec == std::errc{})
      result = std::from_chars(result.ptr + 1, journal_stream + len, inode);
    if (result.ec != std::errc{}) {
      spdlog::warn("malformed JOURNAL_STREAM (\"{}\"): {}, logging to console", journal_stream,
                   std::make_error_condition(result.ec).message());
    }

    struct stat f_stderr;

    if (fstat(STDERR_FILENO, &f_stderr) != 0) {
      spdlog::warn("unable to check stderr device and inode numbers: {}", strerror(errno));
    } else if (device == f_stderr.st_dev && inode == f_stderr.st_ino) {
      auto journald = spdlog::systemd_logger_st("native_journal", "waybar", false);
      /* systemd_logger_st is thread-safe with enable_formatter = false
      ** thanks to underlying sd_journal_send being thread-safe
      ** https://github.com/gabime/spdlog/issues/2320#issuecomment-1079766037
      */
      spdlog::set_default_logger(journald);
    } else {
      spdlog::info("JOURNAL_STREAM does not point to stderr, logging to console");
    }
  } else {
    spdlog::info("no JOURNAL_STREAM, logging to console");
  }
#endif
}

int main(int argc, char* argv[]) {
  logToJournalIfRunAsService();

  try {
    auto* client = waybar::Client::inst();

    bool reload;

    waybar::SafeSignal<int> posix_signal_received;
    posix_signal_received.connect([&](int signum) { handleSignalMainThread(signum, reload); });

    std::thread signal_thread([&]() { catchSignals(posix_signal_received); });

    // Every `std::thread` must be joined or detached.
    // This thread should run forever, so detach it.
    signal_thread.detach();

    auto ret = 0;
    do {
      reload = false;
      ret = client->main(argc, argv);
    } while (reload);

    std::signal(SIGUSR1, SIG_IGN);
    std::signal(SIGUSR2, SIG_IGN);
    std::signal(SIGINT, SIG_IGN);

    delete client;
    return ret;
  } catch (const std::exception& e) {
    spdlog::error("{}", e.what());
    return 1;
  } catch (const Glib::Exception& e) {
    spdlog::error("{}", static_cast<std::string>(e.what()));
    return 1;
  }
}

#endif  // _WIN32
