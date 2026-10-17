#pragma once

#include <gtkmm/drawingarea.h>

#include "AModule.hpp"

namespace waybar::modules {

// Windows 11 徽标：用 Cairo 画 2x2 的四格方块（官方 Win11 logo 样式），
// 背景/前景色由主题 CSS 提供（#custom-applauncher）。
class WindowsLogo : public AModule {
 public:
  WindowsLogo(const std::string& id, const std::string& widget_name, const Json::Value& config);
  ~WindowsLogo() override = default;

  auto update() -> void override {}

 protected:
  bool handleToggle(GdkEventButton* const& event) override;

 private:
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr);

  Gtk::DrawingArea area_;
};

}  // namespace waybar::modules
