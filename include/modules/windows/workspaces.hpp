#pragma once

#include <gtkmm/box.h>
#include <gtkmm/button.h>

#include <vector>

#include "AModule.hpp"

namespace waybar::modules {

// Windows 下的“工作区”替代：1..N 数字按钮。
// Windows 没有对外暴露的虚拟桌面枚举 API，这里用 1..N 指示 + 模拟
// Ctrl+Win+Left/Right 切换虚拟桌面，并本地记录当前高亮的序号。
class Workspaces : public AModule {
 public:
  Workspaces(const std::string& id, const Json::Value& config);
  ~Workspaces() override = default;

  auto update() -> void override;

 protected:
  bool handleScroll(GdkEventScroll* event) override;

 private:
  void rebuild();
  void switchBy(int delta);
  void setActive(int index);

  Gtk::Box box_;
  std::vector<Gtk::Button*> buttons_;
  int count_ = 3;
  int active_ = 0;
};

}  // namespace waybar::modules
