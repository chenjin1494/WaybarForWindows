#include "modules/windows/network.hpp"

#include <fmt/args.h>
#include <spdlog/spdlog.h>

// winsock2.h 必须先于 windows.h，否则 iphlpapi 的地址结构不可用
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <wlanapi.h>

#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cstring>
#include <memory>

#include "util/format.hpp"
#include "win32/common.hpp"

namespace waybar::modules {

namespace {

std::string wide_to_utf8(const wchar_t* value) {
  if (value == nullptr) return {};
  return waybar::win32::to_utf8(value);
}

// 把 GetAdaptersAddresses 的地址链表转成点分/冒号分隔字符串
std::string address_to_string(IP_ADAPTER_ADDRESSES* adapter, int family) {
  for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
       unicast = unicast->Next) {
    const auto* sockaddr = unicast->Address.lpSockaddr;
    if (sockaddr->sa_family != family) continue;
    char buffer[INET6_ADDRSTRLEN] = {};
    if (family == AF_INET) {
      const auto* sin = reinterpret_cast<const sockaddr_in*>(sockaddr);
      if (InetNtopA(AF_INET, &sin->sin_addr, buffer, sizeof(buffer)) != nullptr) {
        return buffer;
      }
    } else {
      const auto* sin6 = reinterpret_cast<const sockaddr_in6*>(sockaddr);
      if (InetNtopA(AF_INET6, &sin6->sin6_addr, buffer, sizeof(buffer)) != nullptr) {
        return buffer;
      }
    }
  }
  return {};
}

std::string prefix_length_to_cidr(IP_ADAPTER_ADDRESSES* adapter) {
  for (auto* unicast = adapter->FirstUnicastAddress; unicast != nullptr;
       unicast = unicast->Next) {
    if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
      return std::to_string(unicast->OnLinkPrefixLength);
    }
  }
  return "";
}

bool has_gateway(IP_ADAPTER_ADDRESSES* adapter) {
  return adapter->FirstGatewayAddress != nullptr;
}

// Wi-Fi 信息：只在无线适配器上尝试，失败则静默返回
void query_wlan(const std::string& description, std::string& essid, int& signal_strength) {
  HANDLE handle = nullptr;
  DWORD negotiated = 0;
  if (WlanOpenHandle(2, nullptr, &negotiated, &handle) != ERROR_SUCCESS) {
    return;
  }

  PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
  if (WlanEnumInterfaces(handle, nullptr, &interfaces) == ERROR_SUCCESS && interfaces != nullptr) {
    for (DWORD i = 0; i < interfaces->dwNumberOfItems; ++i) {
      const auto& info = interfaces->InterfaceInfo[i];
      if (description.find(waybar::win32::to_utf8(info.strInterfaceDescription)) ==
          std::string::npos) {
        continue;
      }
      PWLAN_CONNECTION_ATTRIBUTES attributes = nullptr;
      DWORD size = 0;
      WLAN_OPCODE_VALUE_TYPE opcode{};
      if (WlanQueryInterface(handle, &info.InterfaceGuid, wlan_intf_opcode_current_connection,
                             nullptr, &size, reinterpret_cast<PVOID*>(&attributes),
                             &opcode) == ERROR_SUCCESS &&
          attributes != nullptr) {
        if (attributes->isState == wlan_interface_state_connected) {
          const auto& assoc = attributes->wlanAssociationAttributes;
          const int ssid_len = assoc.dot11Ssid.uSSIDLength;
          essid.assign(reinterpret_cast<const char*>(assoc.dot11Ssid.ucSSID),
                       std::min<int>(ssid_len, sizeof(assoc.dot11Ssid.ucSSID)));
          signal_strength = static_cast<int>(assoc.wlanSignalQuality);
        }
        WlanFreeMemory(attributes);
      }
      break;
    }
    WlanFreeMemory(interfaces);
  }
  WlanCloseHandle(handle, nullptr);
}

std::string trim(const std::string& value) {
  const auto begin = value.find_first_not_of(" \t");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t");
  return value.substr(begin, end - begin + 1);
}

}  // namespace

Network::Network(const std::string& id, const Json::Value& config)
    : ALabel(config, "network", id, "{ifname}", 60),
      interface_(config_["interface"].isString() ? config_["interface"].asString() : ""),
      format_wifi_(config_["format-wifi"].isString() ? config_["format-wifi"].asString()
                                                     : "{essid} ({signalStrength}%)"),
      format_ethernet_(config_["format-ethernet"].isString()
                           ? config_["format-ethernet"].asString()
                           : "{ipaddr}/{cidr}"),
      format_linked_(config_["format-linked"].isString() ? config_["format-linked"].asString()
                                                         : "{ifname} (No IP)"),
      format_disconnected_(config_["format-disconnected"].isString()
                               ? config_["format-disconnected"].asString()
                               : "disconnected") {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

std::string Network::formatBandwidth(double bytes_per_second) const {
  if (bytes_per_second <= 0.0) {
    return "0 B/s";
  }
  return fmt::format("{}", pow_format(static_cast<long long>(bytes_per_second), "B/s", true));
}

void Network::refreshAdapter(AdapterState& state) {
  ULONG size = 16 * 1024;
  std::vector<uint8_t> buffer(size);
  auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
  const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_ANYCAST;

  ULONG result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
  if (result == ERROR_BUFFER_OVERFLOW) {
    buffer.resize(size);
    adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    result = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, adapters, &size);
  }
  if (result != NO_ERROR) {
    state = AdapterState{};
    state.has_counters = false;
    return;
  }

  IP_ADAPTER_ADDRESSES* chosen = nullptr;
  for (auto* adapter = adapters; adapter != nullptr; adapter = adapter->Next) {
    if (adapter->OperStatus != IfOperStatusUp) continue;
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

    const std::string friendly = wide_to_utf8(adapter->FriendlyName);
    const std::string description = wide_to_utf8(adapter->Description);

    if (!interface_.empty() && interface_ != friendly && interface_ != description) {
      continue;
    }
    // 无显式 interface 时优先选择“有默认网关”的适配器
    if (chosen == nullptr ||
        (!has_gateway(chosen) && has_gateway(adapter)) ||
        (has_gateway(chosen) == has_gateway(adapter) && adapter->IfType == IF_TYPE_IEEE80211)) {
      chosen = adapter;
    }
  }

  if (chosen == nullptr) {
    state = AdapterState{};
    state.has_counters = false;
    spdlog::debug("network: no active adapter (interface='{}')", interface_);
    return;
  }

  state.ifname = wide_to_utf8(chosen->FriendlyName);
  state.description = wide_to_utf8(chosen->Description);
  state.wireless = chosen->IfType == IF_TYPE_IEEE80211;
  state.ipaddr = address_to_string(chosen, AF_INET);
  state.ipaddr6 = address_to_string(chosen, AF_INET6);
  state.cidr = prefix_length_to_cidr(chosen);
  state.connected = true;
  state.if_index = chosen->IfIndex;

  state.essid.clear();
  state.signal_strength = 0;
  if (state.wireless) {
    query_wlan(state.description, state.essid, state.signal_strength);
    state.connected = !state.essid.empty();
  }
}

void Network::updateBandwidth(AdapterState& state) {
  if (!state.connected) {
    state.bandwidth_down = 0.0;
    state.bandwidth_up = 0.0;
    state.has_counters = false;
    return;
  }

  MIB_IF_ROW2 row{};
  row.InterfaceIndex = state.if_index;
  if (GetIfEntry2(&row) != NO_ERROR) {
    state.has_counters = false;
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (state.has_counters && last_bandwidth_sample_.time_since_epoch().count() != 0) {
    const double seconds =
        std::chrono::duration<double>(now - last_bandwidth_sample_).count();
    if (seconds > 0.0 && row.InOctets >= state.in_octets && row.OutOctets >= state.out_octets) {
      state.bandwidth_down = static_cast<double>(row.InOctets - state.in_octets) / seconds;
      state.bandwidth_up = static_cast<double>(row.OutOctets - state.out_octets) / seconds;
    }
  }

  state.in_octets = row.InOctets;
  state.out_octets = row.OutOctets;
  state.has_counters = true;
  last_bandwidth_sample_ = now;
}

auto Network::update() -> void {
  refreshAdapter(state_);
  updateBandwidth(state_);

  std::string format;
  std::string tooltip_format = "";
  std::string state_name;
  std::string icon;

  if (!state_.connected) {
    state_name = "disconnected";
    format = format_disconnected_;
    tooltip_format = config_["tooltip-format-disconnected"].isString()
                         ? config_["tooltip-format-disconnected"].asString()
                         : "disconnected";
    icon = getIcon(0, std::vector<std::string>{"disconnected"});
  } else if (state_.wireless && !state_.essid.empty()) {
    format = alt_format_ && config_["format-alt"].isString() ? config_["format-alt"].asString()
                                                            : format_wifi_;
    tooltip_format = config_["tooltip-format-wifi"].isString()
                         ? config_["tooltip-format-wifi"].asString()
                         : "{essid} ({signalStrength}%)";
    state_name = "wifi";
    icon = getIcon(state_.signal_strength, std::vector<std::string>{"wifi"});
  } else if (!state_.ipaddr.empty()) {
    format = alt_format_ && config_["format-alt"].isString() ? config_["format-alt"].asString()
                                                            : format_ethernet_;
    tooltip_format = config_["tooltip-format-ethernet"].isString()
                         ? config_["tooltip-format-ethernet"].asString()
                         : "{ipaddr}/{cidr}";
    state_name = "ethernet";
    icon = getIcon(0, std::vector<std::string>{"ethernet"});
  } else {
    format = format_linked_;
    tooltip_format = config_["tooltip-format-linked"].isString()
                         ? config_["tooltip-format-linked"].asString()
                         : "{ifname} (No IP)";
    state_name = "linked";
    icon = getIcon(0, std::vector<std::string>{"linked"});
  }

  if (config_["tooltip-format"].isString()) {
    tooltip_format = config_["tooltip-format"].asString();
  }

  auto format_args = [&]() {
    return fmt::dynamic_format_arg_store<fmt::format_context>();
  };
  (void)format_args;

  const auto make_label = [&](const std::string& pattern) {
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    store.push_back(fmt::arg("ifname", state_.ifname));
    store.push_back(fmt::arg("essid", state_.essid));
    store.push_back(fmt::arg("signalStrength", state_.signal_strength));
    store.push_back(fmt::arg("ipaddr", state_.ipaddr));
    store.push_back(fmt::arg("ipaddr6", state_.ipaddr6));
    store.push_back(fmt::arg("cidr", state_.cidr));
    store.push_back(fmt::arg("bandwidthDownBits", formatBandwidth(state_.bandwidth_down * 8)));
    store.push_back(fmt::arg("bandwidthUpBits", formatBandwidth(state_.bandwidth_up * 8)));
    store.push_back(fmt::arg("bandwidthDownBytes", formatBandwidth(state_.bandwidth_down)));
    store.push_back(fmt::arg("bandwidthUpBytes", formatBandwidth(state_.bandwidth_up)));
    store.push_back(fmt::arg("icon", icon));
    return fmt::vformat(pattern, store);
  };

  try {
    if (format.empty()) {
      event_box_.hide();
    } else {
      event_box_.show();
      setLabelMarkup(make_label(format));
      if (tooltipEnabled() && !tooltip_format.empty()) {
        setTooltipMarkup(make_label(tooltip_format));
      }
    }
  } catch (const fmt::format_error& e) {
    spdlog::warn("network: format error: {}", e.what());
  }

  // CSS 状态类：wifi / ethernet / linked / disconnected
  for (const auto* name : {"wifi", "ethernet", "linked", "disconnected"}) {
    label_.get_style_context()->remove_class(name);
  }
  label_.get_style_context()->add_class(state_name);
  if (state_.wireless) {
    label_.get_style_context()->add_class("wifi");
  }

  ALabel::update();
}

}  // namespace waybar::modules
