#include "modules/windows/spacer.hpp"

#include <cairomm/context.h>
#include <spdlog/spdlog.h>

namespace waybar::modules {

Spacer::Spacer(const std::string& id, const std::string& widget_name, const Json::Value& config)
    : AModule(config, widget_name, id, false, false) {
  const int width = config["width"].isInt() ? config["width"].asInt() : 0;
  area_.set_name(widget_name);
  auto context = area_.get_style_context();
  context->add_class(id);
  context->add_class("module");
  context->add_class("spacer");
  area_.set_size_request(width, -1);
  area_.show();
  event_box_.add(area_);
  area_.signal_draw().connect(sigc::mem_fun(*this, &Spacer::on_draw));
  spdlog::debug("[spacer] {} width={}", widget_name, width);
}

bool Spacer::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  // 完全透明，只占宽度
  cr->save();
  cr->set_source_rgba(0, 0, 0, 0);
  cr->paint();
  cr->restore();
  return true;
}

}  // namespace waybar::modules
