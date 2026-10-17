#include "win32/control.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

#include "util/fs.hpp"
#include "win32/common.hpp"
#include "win32/paths.hpp"

namespace fs = std::filesystem;

namespace waybar::win32 {

namespace {

constexpr const wchar_t* kPipePrefix = L"\\\\.\\pipe\\waybar-control-";
constexpr const char* kInstanceMagic = "WAYBAR-INSTANCE 1";

std::string instances_dir() { return local_appdata() + "\\waybar\\instances"; }

std::string instance_file(int pid) {
  return instances_dir() + "\\" + std::to_string(pid) + ".instance";
}

bool process_alive(DWORD pid) {
  HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) return false;
  const DWORD wait = WaitForSingleObject(process, 0);
  CloseHandle(process);
  return wait == WAIT_TIMEOUT;
}

struct InstanceRecord {
  DWORD pid = 0;
  DWORD session = 0;
  std::string config;
  std::vector<std::string> bars;
};

bool parse_instance(const std::string& content, InstanceRecord& record) {
  std::istringstream stream(content);
  std::string line;
  if (!std::getline(stream, line) || line != kInstanceMagic) return false;
  while (std::getline(stream, line)) {
    const auto space = line.find(' ');
    if (space == std::string::npos) continue;
    const std::string key = line.substr(0, space);
    const std::string value = line.substr(space + 1);
    if (key == "PID") {
      record.pid = static_cast<DWORD>(std::stoul(value));
    } else if (key == "SESSION") {
      record.session = static_cast<DWORD>(std::stoul(value));
    } else if (key == "CONFIG") {
      record.config = value;
    } else if (key == "BARS") {
      record.bars.clear();
      std::istringstream bars(value);
      std::string bar;
      while (std::getline(bars, bar, ',')) {
        if (!bar.empty()) record.bars.push_back(bar);
      }
    }
  }
  return record.pid != 0;
}

std::string serialize_request(const ControlRequest& request) {
  std::ostringstream out;
  out << kInstanceMagic << "\n";
  out << "COMMAND " << request.command << "\n";
  if (!request.bar_id.empty()) out << "BAR " << request.bar_id << "\n";
  if (!request.style.empty()) out << "STYLE " << request.style << "\n";
  if (!request.config.empty()) out << "CONFIG " << request.config << "\n";
  out << "END\n";
  return out.str();
}

bool parse_request(const std::string& text, ControlRequest& request) {
  std::istringstream stream(text);
  std::string line;
  if (!std::getline(stream, line) || line != kInstanceMagic) return false;
  while (std::getline(stream, line)) {
    if (line == "END") return true;
    const auto space = line.find(' ');
    if (space == std::string::npos) continue;
    const std::string key = line.substr(0, space);
    const std::string value = line.substr(space + 1);
    if (key == "COMMAND") {
      request.command = value;
    } else if (key == "BAR") {
      request.bar_id = value;
    } else if (key == "STYLE") {
      request.style = value;
    } else if (key == "CONFIG") {
      request.config = value;
    }
  }
  return true;
}

}  // namespace

// ---------------------------------------------------------------- Server

struct ControlServer::Impl {
  Handler handler;
  std::thread thread;
  std::atomic<bool> running{false};
  HANDLE stop_event = nullptr;
  std::wstring pipe_name;
  std::string config_path;
  std::vector<std::string> bars;
  DWORD pid = 0;
};

ControlServer::ControlServer() : impl_(std::make_unique<Impl>()) {}

ControlServer::~ControlServer() { stop(); }

int ControlServer::process_id() const { return static_cast<int>(impl_->pid); }

std::string ControlServer::pipe_name() const { return to_utf8(impl_->pipe_name); }

bool ControlServer::start(Handler handler, const std::string& config_path,
                          const std::vector<std::string>& bars) {
  if (impl_->running.exchange(true)) return true;

  impl_->handler = std::move(handler);
  impl_->config_path = config_path;
  impl_->bars = bars;
  impl_->pid = GetCurrentProcessId();
  impl_->pipe_name =
      std::wstring(kPipePrefix) + std::to_wstring(static_cast<unsigned long>(impl_->pid));
  impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (impl_->stop_event == nullptr) {
    spdlog::warn("[control] CreateEvent failed: {}", last_error());
    impl_->running = false;
    return false;
  }

  // 写实例登记文件
  std::error_code ec;
  fs::create_directories(waybar::util::to_path(instances_dir()), ec);
  if (ec) {
    spdlog::warn("[control] create_directories({}) failed: {}", instances_dir(), ec.message());
  }
  {
    auto file = waybar::util::open_ofstream(instance_file(static_cast<int>(impl_->pid)),
                                            std::ios::binary);
    if (file) {
      file << kInstanceMagic << "\n";
      file << "PID " << impl_->pid << "\n";
      file << "SESSION " << ProcessIdToSessionId(impl_->pid, nullptr) << "\n";
      file << "CONFIG " << config_path << "\n";
      file << "BARS ";
      for (size_t i = 0; i < bars.size(); ++i) {
        if (i != 0) file << ",";
        file << bars[i];
      }
      file << "\n";
    } else {
      spdlog::warn("[control] failed to write instance file");
    }
  }

  impl_->thread = std::thread([this] {
    Impl* impl = impl_.get();
    while (impl->running.load()) {
      HANDLE pipe = CreateNamedPipeW(
          impl->pipe_name.c_str(), PIPE_ACCESS_DUPLEX,
          PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 8192, 8192, 0, nullptr);
      if (pipe == INVALID_HANDLE_VALUE) {
        spdlog::warn("[control] CreateNamedPipe failed: {}", last_error());
        return;
      }

      OVERLAPPED overlapped{};
      overlapped.hEvent = impl->stop_event;
      BOOL connected = ConnectNamedPipe(pipe, &overlapped);
      if (!connected) {
        const DWORD error = GetLastError();
        if (error == ERROR_IO_PENDING) {
          const DWORD wait = WaitForSingleObject(impl->stop_event, INFINITE);
          if (wait == WAIT_OBJECT_0) {
            CancelIoEx(pipe, &overlapped);
            CloseHandle(pipe);
            break;
          }
          DWORD transferred = 0;
          GetOverlappedResult(pipe, &overlapped, &transferred, FALSE);
        } else if (error != ERROR_PIPE_CONNECTED) {
          spdlog::warn("[control] ConnectNamedPipe failed: {}", last_error(error));
          CloseHandle(pipe);
          continue;
        }
      }

      char buffer[8192] = {};
      DWORD read = 0;
      if (ReadFile(pipe, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0) {
        ControlRequest request;
        if (parse_request(std::string(buffer, read), request)) {
          std::string reply = "ERR no handler";
          if (impl->handler) reply = impl->handler(request);
          DWORD written = 0;
          WriteFile(pipe, reply.data(), static_cast<DWORD>(reply.size()), &written, nullptr);
        } else {
          const char* reply = "ERR bad request";
          DWORD written = 0;
          WriteFile(pipe, reply, 15, &written, nullptr);
        }
      }
      FlushFileBuffers(pipe);
      DisconnectNamedPipe(pipe);
      CloseHandle(pipe);
    }
  });

  spdlog::debug("[control] listening on {}", pipe_name());
  return true;
}

void ControlServer::stop() {
  if (!impl_ || !impl_->running.exchange(false)) return;
  if (impl_->stop_event != nullptr) SetEvent(impl_->stop_event);
  if (impl_->thread.joinable()) impl_->thread.join();
  if (impl_->stop_event != nullptr) {
    CloseHandle(impl_->stop_event);
    impl_->stop_event = nullptr;
  }
  std::error_code ec;
  fs::remove(waybar::util::to_path(instance_file(static_cast<int>(impl_->pid))), ec);
  spdlog::debug("[control] stopped");
}

// ---------------------------------------------------------------- Client

int send_control_command(const ControlRequest& request) {
  const std::string directory = instances_dir();
  std::error_code ec;
  if (!fs::exists(waybar::util::to_path(directory), ec)) return 0;

  const std::string payload = serialize_request(request);
  int handled = 0;

  for (const auto& entry : fs::directory_iterator(waybar::util::to_path(directory), ec)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".instance") continue;

    auto file = waybar::util::open_ifstream(waybar::util::path_to_utf8(entry.path()),
                                            std::ios::binary);
    if (!file) continue;
    std::stringstream buffer;
    buffer << file.rdbuf();

    InstanceRecord record;
    if (!parse_instance(buffer.str(), record)) {
      std::error_code remove_ec;
      fs::remove(entry.path(), remove_ec);
      continue;
    }
    if (!process_alive(record.pid)) {
      std::error_code remove_ec;
      fs::remove(entry.path(), remove_ec);
      continue;
    }

    // 可选过滤：只投递到包含指定 bar 的实例
    if (!request.bar_id.empty()) {
      const bool has_bar =
          std::find(record.bars.begin(), record.bars.end(), request.bar_id) != record.bars.end();
      if (!has_bar) continue;
    }

    const std::wstring pipe_name = std::wstring(kPipePrefix) + std::to_wstring(record.pid);
    if (!WaitNamedPipeW(pipe_name.c_str(), 1500)) {
      spdlog::warn("control pipe for pid {} not available", record.pid);
      continue;
    }
    HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
      spdlog::warn("failed to open control pipe for pid {}: {}", record.pid, last_error());
      continue;
    }

    DWORD written = 0;
    const BOOL write_ok =
        WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);

    char reply[512] = {};
    DWORD read = 0;
    const BOOL read_ok = ReadFile(pipe, reply, sizeof(reply) - 1, &read, nullptr);
    CloseHandle(pipe);

    if (write_ok && read_ok && read > 0) {
      const std::string response(reply, read);
      if (response.rfind("OK", 0) == 0) {
        ++handled;
        spdlog::debug("control '{}' -> pid {}: {}", request.command, record.pid, response);
      } else {
        spdlog::warn("control '{}' rejected by pid {}: {}", request.command, record.pid, response);
      }
    } else {
      spdlog::warn("control '{}' failed for pid {}", request.command, record.pid);
    }
  }

  return handled;
}

std::vector<std::string> list_instances() {
  std::vector<std::string> result;
  const std::string directory = instances_dir();
  std::error_code ec;
  if (!fs::exists(waybar::util::to_path(directory), ec)) return result;

  for (const auto& entry : fs::directory_iterator(waybar::util::to_path(directory), ec)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".instance") continue;
    auto file = waybar::util::open_ifstream(waybar::util::path_to_utf8(entry.path()),
                                            std::ios::binary);
    if (!file) continue;
    std::stringstream buffer;
    buffer << file.rdbuf();
    InstanceRecord record;
    if (!parse_instance(buffer.str(), record)) continue;
    if (!process_alive(record.pid)) continue;
    std::ostringstream line;
    line << "pid=" << record.pid << " session=" << record.session;
    if (!record.config.empty()) line << " config=" << record.config;
    if (!record.bars.empty()) {
      line << " bars=";
      for (size_t i = 0; i < record.bars.size(); ++i) {
        if (i != 0) line << ",";
        line << record.bars[i];
      }
    }
    result.push_back(line.str());
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace waybar::win32
