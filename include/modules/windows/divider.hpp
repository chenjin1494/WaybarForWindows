#pragma once

#include <gtkmm/drawingarea.h>

#include "AModule.hpp"

namespace waybar::modules {

// Windows 原生分隔符：用 Cairo 画精确的等腰三角形，底边铺满 bar 高度。
// 主题 CSS 仍通过 #custom-right_div_5 等选择器提供背景色/前景色。
class Divider : public AModule {
 public:
  Divider(const std::string& id, const std::string& widget_name, bool point_left,
          const Json::Value& config);
  ~Divider() override = default;

  auto update() -> void override {}

 private:
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr);

  Gtk::DrawingArea area_;
  bool point_left_;
};

}  // namespace waybar::modules
