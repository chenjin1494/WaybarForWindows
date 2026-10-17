#pragma once

#include <fmt/format.h>

#include <string>

#include "ALabel.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules {

// Windows 版 pulseaudio：用 Core Audio（IAudioEndpointVolume）读写默认输出端点的
// 音量/静音状态。配置项与上游 pulseaudio 对齐：
//   format / format-muted / format-icons / states / scroll-step / max-volume /
//   tooltip-format / interval / on-click(mute) / 滚轮调节
class Pulseaudio : public ALabel {
 public:
  Pulseaudio(const std::string&, const Json::Value&);
  virtual ~Pulseaudio();
  auto update() -> void override;

 private:
  bool handleToggle(GdkEventButton* const& e) override;
  bool handleScroll(GdkEventScroll* e) override;

  bool setVolume(double volume) const;
  bool toggleMute() const;

  util::SleeperThread thread_;
  int scroll_step_;
  int max_volume_;
};

}  // namespace waybar::modules
