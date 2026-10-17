#pragma once

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>

#include <string>

#include "AModule.hpp"

namespace waybar::modules {

// Windows 没有可嵌入的托盘协议：这里提供一个“后台任务”按钮，
// 左键弹出当前窗口列表（点击激活），右键打开任务管理器。
class Tray : public AModule {
 public:
  Tray(const std::string& id, const Json::Value& config);
  ~Tray() override = default;

  auto update() -> void override;

 protected:
  bool handleToggle(GdkEventButton* const& event) override;

 private:
  void rebuildList();

  Gtk::Label label_;
  Gtk::Window popup_;
  Gtk::Box list_;
  std::string format_;
};

}  // namespace waybar::modules
