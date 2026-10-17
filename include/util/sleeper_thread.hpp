#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <functional>
#include <thread>

#include "prepare_for_sleep.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace waybar::util {

#ifdef _WIN32

// Windows 没有 pthread_cancel 的等价语义（TerminateThread 会泄漏资源并可能死锁），
// 因此取消被降级为「设置停止标志 + 唤醒条件变量」，由线程在检查点自行退出。
class CancellationGuard {
 public:
  CancellationGuard() = default;
  ~CancellationGuard() = default;
};

#else

/**
 * Defer pthread_cancel until the end of a current scope.
 *
 * Required to protect a scope where it's unsafe to raise `__forced_unwind` exception.
 * An example of these is a call of a method marked as `noexcept`; an attempt to cancel within such
 * a method may result in a `std::terminate` call.
 */
class CancellationGuard {
  int oldstate;

 public:
  CancellationGuard() { pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldstate); }
  ~CancellationGuard() { pthread_setcancelstate(oldstate, &oldstate); }
};

#endif

class SleeperThread {
 public:
  SleeperThread() = default;

  SleeperThread(std::function<void()> func)
      : thread_{[this, func] {
          while (do_run_.load(std::memory_order_relaxed)) {
            signal_.store(false, std::memory_order_relaxed);
            func();
          }
        }} {
    connection_ = prepare_for_sleep().connect([this](bool sleep) {
      if (not sleep) wake_up();
    });
  }

  SleeperThread& operator=(std::function<void()> func) {
    if (thread_.joinable()) {
      stop();
      thread_.join();
    }
    {
      std::lock_guard<std::mutex> lck(mutex_);
      do_run_.store(true, std::memory_order_relaxed);
      signal_.store(false, std::memory_order_relaxed);
    }
    thread_ = std::thread([this, func] {
      while (do_run_.load(std::memory_order_relaxed)) {
        signal_.store(false, std::memory_order_relaxed);
        func();
      }
    });
    if (connection_.empty()) {
      connection_ = prepare_for_sleep().connect([this](bool sleep) {
        if (not sleep) wake_up();
      });
    }
    return *this;
  }

  bool isRunning() const { return do_run_.load(std::memory_order_relaxed); }

  auto sleep() {
    std::unique_lock<std::mutex> lk(mutex_);
    CancellationGuard cancel_lock;
    return condvar_.wait(lk, [this] {
      return signal_.load(std::memory_order_relaxed) || !do_run_.load(std::memory_order_relaxed);
    });
  }

  auto sleep_for(std::chrono::system_clock::duration dur) {
    std::unique_lock<std::mutex> lk(mutex_);
    CancellationGuard cancel_lock;

    condvar_.wait(lk, [this] {
      return !is_paused_ || signal_.load(std::memory_order_relaxed) ||
             !do_run_.load(std::memory_order_relaxed);
    });

    constexpr auto max_time_point = std::chrono::steady_clock::time_point::max();
    auto wait_end = max_time_point;
    auto now = std::chrono::steady_clock::now();
    if (now < max_time_point - dur) {
      wait_end = now + dur;
    }
    return condvar_.wait_until(lk, wait_end, [this] {
      return signal_.load(std::memory_order_relaxed) || !do_run_.load(std::memory_order_relaxed);
    });
  }

  auto sleep_until(
      std::chrono::time_point<std::chrono::system_clock, std::chrono::system_clock::duration>
          time_point) {
    std::unique_lock<std::mutex> lk(mutex_);
    CancellationGuard cancel_lock;

    condvar_.wait(lk, [this] {
      return !is_paused_ || signal_.load(std::memory_order_relaxed) ||
             !do_run_.load(std::memory_order_relaxed);
    });

    return condvar_.wait_until(lk, time_point, [this] {
      return signal_.load(std::memory_order_relaxed) || !do_run_.load(std::memory_order_relaxed);
    });
  }

  void wake_up() {
    {
      std::lock_guard<std::mutex> lck(mutex_);
      signal_.store(true, std::memory_order_relaxed);
    }
    condvar_.notify_all();
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lck(mutex_);
      signal_.store(true, std::memory_order_relaxed);
      do_run_.store(false, std::memory_order_relaxed);
    }
    condvar_.notify_all();
#ifdef _WIN32
    // 无法安全强杀 std::thread：线程会在下一个检查点自然退出。
#else
    auto handle = thread_.native_handle();
    if (handle != 0) {
      // TODO: find a proper way to terminate thread...
      pthread_cancel(handle);
    }
#endif
  }

  void pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_paused_ = true;
  }

  void resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_paused_ = false;
    condvar_.notify_all();
  }

  ~SleeperThread() {
    connection_.disconnect();
    stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

 private:
  std::thread thread_;
  std::condition_variable condvar_;
  std::mutex mutex_;
  std::atomic<bool> do_run_ = true;
  std::atomic<bool> signal_ = false;
  sigc::connection connection_;
  bool is_paused_{false};
};

}  // namespace waybar::util
