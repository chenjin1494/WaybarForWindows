#include "modules/windows/divider.hpp"

#include <cairomm/context.h>
#include <gdkmm/rgba.h>
#include <spdlog/spdlog.h>

#include <cmath>

namespace waybar::modules {

Divider::Divider(const std::string& id, const std::string& widget_name, bool point_left,
                 const Json::Value& config)
    : AModule(config, widget_name, id, false, false), point_left_(point_left) {
  area_.set_name(widget_name);
  auto context = area_.get_style_context();
  context->add_class(id);
  context->add_class("module");
  context->add_class("divider");
  context->add_class(point_left_ ? "left" : "right");

  // 三角形宽高比 √3/2 ≈ 0.866；高度跟随 bar（垂直方向填满）
  area_.set_size_request(21, -1);
  area_.show();
  event_box_.add(area_);

  area_.signal_draw().connect(sigc::mem_fun(*this, &Divider::on_draw));
  context->signal_changed().connect([this] { area_.queue_draw(); });

  if (tooltipEnabled()) {
    event_box_.set_tooltip_text("");
  }
}

bool Divider::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int width = area_.get_allocated_width();
  const int height = area_.get_allocated_height();
  auto context = area_.get_style_context();
  Gdk::RGBA background = context->get_background_color();
  Gdk::RGBA foreground = context->get_color();

  cr->save();
  cr->set_source_rgba(background.get_red(), background.get_green(), background.get_blue(),
                      background.get_alpha());
  cr->rectangle(0, 0, width, height);
  cr->fill();

  const double w = static_cast<double>(width);
  const double h = static_cast<double>(height);
  cr->set_source_rgba(foreground.get_red(), foreground.get_green(), foreground.get_blue(),
                      foreground.get_alpha());
  // 底边向上下各延伸 2px，保证抗锯齿后仍从 bar 最上沿铺到最下沿
  constexpr double kOvershoot = 2.0;
  cr->move_to(point_left_ ? w : 0.0, -kOvershoot);
  cr->line_to(point_left_ ? 0.0 : w, h / 2.0);
  cr->line_to(point_left_ ? w : 0.0, h + kOvershoot);
  cr->close_path();
  cr->fill();
  cr->restore();
  return true;
}

}  // namespace waybar::modules
