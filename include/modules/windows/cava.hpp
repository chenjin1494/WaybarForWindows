#pragma once

#include <gtkmm/label.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "AModule.hpp"

namespace waybar::modules {

// Windows 版音频可视化（替代 custom/cava + cava.sh）：
// WASAPI 回环捕获默认扬声器输出，Goertzel 计算对数频段能量，用方块字符渲染。
class Cava : public AModule {
 public:
  Cava(const std::string& id, const Json::Value& config);
  ~Cava() override;

  auto update() -> void override;

 private:
  void captureLoop();
  void render(const std::vector<float>& levels);
  std::string renderLine(const std::vector<float>& levels) const;

  Gtk::Label label_;
  std::thread worker_;
  std::atomic<bool> stop_{false};
  std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);
  int bars_ = 16;
  int framerate_ = 30;
};

}  // namespace waybar::modules
