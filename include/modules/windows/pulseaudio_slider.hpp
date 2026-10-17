#pragma once

#include <string>

#include "ASlider.hpp"

namespace waybar::modules {

// Windows 版 pulseaudio/slider：滑块直接读写默认输出端点的音量。
class PulseaudioSlider : public ASlider {
 public:
  PulseaudioSlider(const std::string&, const Json::Value&);
  virtual ~PulseaudioSlider() = default;

  void update() override;
  void onValueChanged() override;

 private:
  bool updating_ = false;
};

}  // namespace waybar::modules
