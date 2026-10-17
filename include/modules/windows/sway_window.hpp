#pragma once

#include <fmt/format.h>

#include <string>

#include "ALabel.hpp"
#include "bar.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules::sway {

// Windows 版 sway/window：显示前台窗口标题。
// 配置项与上游对齐：format（{title}/{app_id}/{class}）、max-length、rewrite、
// tooltip、tooltip-format。cursor 跟随/off-screen 等合成器行为不适用。
class Window : public ALabel {
 public:
  Window(const std::string&, const waybar::Bar&, const Json::Value&);
  virtual ~Window() = default;
  auto update() -> void override;

 private:
  std::string rewriteTitle(const std::string& title, const std::string& app_id) const;

  const Bar& bar_;
  util::SleeperThread thread_;
};

}  // namespace waybar::modules::sway
