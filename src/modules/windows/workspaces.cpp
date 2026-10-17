#include "modules/windows/workspaces.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <string>

namespace waybar::modules {

namespace {

void sendKey(WORD virtual_key, bool down) {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = virtual_key;
  input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
  SendInput(1, &input, sizeof(input));
}

void sendWorkspaceSwitch(int direction) {
  // Ctrl+Win+Left/Right：Windows 切换虚拟桌面
  const WORD arrow = direction < 0 ? VK_LEFT : VK_RIGHT;
  sendKey(VK_LWIN, true);
  sendKey(VK_CONTROL, true);
  sendKey(arrow, true);
  sendKey(arrow, false);
  sendKey(VK_CONTROL, false);
  sendKey(VK_LWIN, false);
}

}  // namespace

Workspaces::Workspaces(const std::string& id, const Json::Value& config)
    : AModule(config, "workspaces", id, false, true) {
  if (config_["count"].isInt()) {
    count_ = std::clamp(config_["count"].asInt(), 1, 9);
  }
  event_box_.set_name("workspaces");
  event_box_.add(box_);
  box_.set_name("workspaces");
  box_.get_style_context()->add_class("workspaces");
  box_.show();
  rebuild();
}

void Workspaces::rebuild() {
  for (auto* button : buttons_) {
    box_.remove(*button);
    delete button;
  }
  buttons_.clear();
  for (int index = 0; index < count_; ++index) {
    auto* button = new Gtk::Button(std::to_string(index + 1));
    button->set_relief(Gtk::RELIEF_NONE);
    button->set_can_focus(false);
    button->get_style_context()->add_class("flat");
    if (index == active_) {
      button->get_style_context()->add_class("active");
    }
    button->signal_clicked().connect([this, index] {
      const int delta = index - active_;
      if (delta != 0) {
        switchBy(delta);
      }
    });
    box_.pack_start(*button, false, false, 0);
    button->show();
    buttons_.push_back(button);
  }
}

void Workspaces::switchBy(int delta) {
  const int steps = std::min(std::abs(delta), 8);
  const int direction = delta > 0 ? 1 : -1;
  for (int i = 0; i < steps; ++i) {
    sendWorkspaceSwitch(direction);
  }
  setActive(std::clamp(active_ + delta, 0, count_ - 1));
}

void Workspaces::setActive(int index) {
  active_ = index;
  for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
    auto context = buttons_[i]->get_style_context();
    if (i == active_) {
      context->add_class("active");
    } else {
      context->remove_class("active");
    }
  }
}

auto Workspaces::update() -> void {
  for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
    auto context = buttons_[i]->get_style_context();
    if (i == active_) {
      context->add_class("active");
    } else {
      context->remove_class("active");
    }
  }
  if (tooltipEnabled()) {
    event_box_.set_tooltip_text("点击数字切换到对应虚拟桌面（Ctrl+Win+方向键）");
  }
}

bool Workspaces::handleScroll(GdkEventScroll* event) {
  const auto dir = getScrollDir(event);
  if (dir == SCROLL_DIR::UP || dir == SCROLL_DIR::LEFT) {
    switchBy(-1);
  } else if (dir == SCROLL_DIR::DOWN || dir == SCROLL_DIR::RIGHT) {
    switchBy(1);
  }
  return true;
}

}  // namespace waybar::modules
