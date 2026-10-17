#pragma once

#include <gtkmm/label.h>

#include "AModule.hpp"

namespace waybar::modules {

// Windows 蓝牙状态：检测本机是否存在蓝牙无线电，点击打开蓝牙设置，右键运行切换脚本。
class Bluetooth : public AModule {
 public:
  Bluetooth(const std::string& id, const Json::Value& config);
  ~Bluetooth() override = default;

  auto update() -> void override;

 protected:
  bool handleToggle(GdkEventButton* const& event) override;

 private:
  bool radioPresent() const;

  Gtk::Label label_;
  std::string format_;
  std::string formatDisabled_;
};

}  // namespace waybar::modules
