#pragma once

#include <fmt/format.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 network：
//   - 适配器/IP：GetAdaptersAddresses（IPv4/IPv6、网关、接口名）
//   - Wi-Fi：WlanAPI（SSID、信号强度、连接状态）
//   - 带宽：GetIfEntry2 的 InOctets/OutOctets 差值
// 配置项与上游 network 对齐（format-wifi/format-ethernet/format-linked/
// format-disconnected/format-alt/tooltip-format*/format-icons/interface/interval）。
class Network : public ALabel {
 public:
  Network(const std::string&, const Json::Value&);
  virtual ~Network() = default;
  auto update() -> void override;

 private:
  struct AdapterState {
    std::string ifname;      // 友好名（如 "WLAN"）
    std::string description;  // 适配器描述
    std::string ipaddr;
    std::string ipaddr6;
    std::string cidr;
    bool connected = false;
    bool wireless = false;
    std::string essid;
    int signal_strength = 0;
    double bandwidth_down = 0.0;  // bytes/s
    double bandwidth_up = 0.0;
    uint64_t in_octets = 0;
    uint64_t out_octets = 0;
    unsigned long if_index = 0;
    bool has_counters = false;
  };

  void refreshAdapter(AdapterState& state);
  void updateBandwidth(AdapterState& state);
  std::string formatBandwidth(double bytes_per_second) const;

  std::string interface_;
  std::string format_wifi_;
  std::string format_ethernet_;
  std::string format_linked_;
  std::string format_disconnected_;
  std::string tooltip_format_;
  bool alt_format_ = false;

  AdapterState state_;
  std::chrono::steady_clock::time_point last_bandwidth_sample_{};

  util::SleeperThread thread_;
};

}  // namespace waybar::modules
