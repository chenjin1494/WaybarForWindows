#pragma once

#include <gtkmm/drawingarea.h>

#include "AModule.hpp"

namespace waybar::modules {

// 透明占位模块：宽度来自 config["width"]，用于让中心组左右两侧等长，
// 保证中央徽标始终在屏幕正中。
class Spacer : public AModule {
 public:
  Spacer(const std::string& id, const std::string& widget_name, const Json::Value& config);
  ~Spacer() override = default;

  auto update() -> void override {}

 private:
  bool on_draw(const Cairo::RefPtr<Cairo::Context>& cr);

  Gtk::DrawingArea area_;
};

}  // namespace waybar::modules
