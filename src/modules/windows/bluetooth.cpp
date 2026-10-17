#include "modules/windows/bluetooth.hpp"

#include <winsock2.h>
#include <windows.h>
#include <bluetoothapis.h>
#include <spdlog/spdlog.h>

#include "win32/exec.hpp"

namespace waybar::modules {

Bluetooth::Bluetooth(const std::string& id, const Json::Value& config)
    : AModule(config, "bluetooth", id, true, false) {
  format_ = config_["format"].isString() ? config_["format"].asString() : "󰂯";
  formatDisabled_ =
      config_["format-disabled"].isString() ? config_["format-disabled"].asString() : "󰂲";
  label_.set_name("bluetooth");
  label_.get_style_context()->add_class(id);
  label_.get_style_context()->add_class(MODULE_CLASS);
  label_.show();
  event_box_.add(label_);
}

bool Bluetooth::radioPresent() const {
  BLUETOOTH_FIND_RADIO_PARAMS params{};
  params.dwSize = sizeof(params);
  HANDLE radio = nullptr;
  HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
  if (find == nullptr) {
    return false;
  }
  if (radio != nullptr) {
    CloseHandle(radio);
  }
  BluetoothFindRadioClose(find);
  return true;
}

auto Bluetooth::update() -> void {
  const bool present = radioPresent();
  label_.set_markup(present ? format_ : formatDisabled_);
  auto context = label_.get_style_context();
  if (present) {
    context->remove_class("disabled");
  } else {
    context->add_class("disabled");
  }
  if (tooltipEnabled()) {
    const std::string tooltip = present
                                    ? (config_["tooltip-format-on"].isString()
                                           ? config_["tooltip-format-on"].asString()
                                           : "左键：打开蓝牙设置\n右键：切换蓝牙")
                                    : (config_["tooltip-format-disabled"].isString()
                                           ? config_["tooltip-format-disabled"].asString()
                                           : "未检测到蓝牙硬件\n---\n左键：打开蓝牙设置");
    event_box_.set_tooltip_markup(tooltip);
  }
}

bool Bluetooth::handleToggle(GdkEventButton* const& event) {
  const std::string output_name;
  if (event->button == 1) {
    win32::run_detached("cmd /d /s /c start ms-settings:bluetooth", output_name);
  } else if (event->button == 3) {
    win32::run_detached(
        "powershell -NoProfile -ExecutionPolicy Bypass -File "
        "\"%LOCALAPPDATA%\\Programs\\Waybar\\config\\scripts\\toggle-bluetooth.ps1\"",
        output_name);
  }
  return true;
}

}  // namespace waybar::modules
