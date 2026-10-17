#include "modules/windows/windows_logo.hpp"

#include <cairomm/context.h>
#include <gdkmm/rgba.h>
#include <windows.h>

#include <algorithm>

#include "win32/exec.hpp"

namespace waybar::modules {

WindowsLogo::WindowsLogo(const std::string& id, const std::string& widget_name,
                         const Json::Value& config)
    : AModule(config, widget_name, id, true, false) {
  area_.set_name(widget_name);
  auto context = area_.get_style_context();
  context->add_class(id);
  context->add_class("module");
  context->add_class("applauncher");
  area_.set_size_request(46, -1); // 中间 + 两侧箭头，整体形成菱形
  area_.show();
  event_box_.add(area_);
  area_.signal_draw().connect(sigc::mem_fun(*this, &WindowsLogo::on_draw));
  context->signal_changed().connect([this] { area_.queue_draw(); });
}

bool WindowsLogo::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int width = area_.get_allocated_width();
  const int height = area_.get_allocated_height();
  auto context = area_.get_style_context();
  Gdk::RGBA background = context->get_background_color();
  Gdk::RGBA foreground = context->get_color();

  cr->save();
  // 六边形：左右各一个箭头 + 中间矩形，整体是参考图的“菱形徽标”
  const double wing = std::min(11.0, static_cast<double>(width) / 4.0);
  const double h = static_cast<double>(height);
  const double w = static_cast<double>(width);
  cr->set_source_rgba(background.get_red(), background.get_green(), background.get_blue(),
                      background.get_alpha());
  cr->move_to(0, h / 2.0);
  cr->line_to(wing, 0);
  cr->line_to(w - wing, 0);
  cr->line_to(w, h / 2.0);
  cr->line_to(w - wing, h);
  cr->line_to(wing, h);
  cr->close_path();
  cr->fill();

  // 用底色把中间矩形填实，避免两侧箭头与中间之间出现缝隙
  cr->rectangle(wing, 0, w - wing * 2, h);
  cr->fill();

  // Windows 11 logo：2x2 四格，留出十字间隙
  const double side = std::min(static_cast<double>(width) - wing * 2.0, static_cast<double>(height)) * 0.62;
  const double gap = side * 0.12;
  const double cell = (side - gap) / 2.0;
  const double origin_x = (width - side) / 2.0;
  const double origin_y = (height - side) / 2.0;
  cr->set_source_rgba(foreground.get_red(), foreground.get_green(), foreground.get_blue(),
                      foreground.get_alpha());
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      const double x = origin_x + column * (cell + gap);
      const double y = origin_y + row * (cell + gap);
      cr->rectangle(x, y, cell, cell);
    }
  }
  cr->fill();
  cr->restore();
  return true;
}

bool WindowsLogo::handleToggle(GdkEventButton* const& event) {
  if (event->button == 1) {
    // 左键：Win 键打开开始菜单
    INPUT input[2] = {};
    input[0].type = INPUT_KEYBOARD;
    input[0].ki.wVk = VK_LWIN;
    input[1].type = INPUT_KEYBOARD;
    input[1].ki.wVk = VK_LWIN;
    input[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, input, sizeof(INPUT));
    return true;
  }
  if (event->button == 3) {
    win32::run_detached("cmd /d /s /c start ms-settings:", "");
    return true;
  }
  return false;
}

}  // namespace waybar::modules
