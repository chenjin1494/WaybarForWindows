#pragma once

#include <windows.h>

#include <chrono>
#include <string>
#include <vector>

#include "AModule.hpp"
#include "bar.hpp"
#include "util/sleeper_thread.hpp"

namespace waybar::modules::wlr {

// Windows 版 wlr/taskbar：枚举可见顶层窗口并显示标题按钮，点击切换到该窗口。
// 配置项：format（{title}/{app_id}/{class}）、tooltip-format、max-length。
// 说明：图标（icon-size/icon-theme）在 Windows 上暂未实现，按纯文本显示。
class Taskbar : public AModule {
 public:
  struct WindowEntry {
    HWND hwnd = nullptr;
    std::string title;
    std::string app_id;
    std::string window_class;
  };

  Taskbar(const std::string&, const waybar::Bar&, const Json::Value&);
  ~Taskbar() override;
  auto update() -> void override;

 private:
  void rebuild(const std::vector<WindowEntry>& windows);
  void clear();

  const Bar& bar_;
  Gtk::Box box_;
  std::string format_ = "{title}";
  int max_length_ = 0;
  std::chrono::seconds interval_{1};
  std::vector<WindowEntry> last_windows_;
  std::vector<Gtk::Button*> buttons_;
  util::SleeperThread thread_;
};

}  // namespace waybar::modules::wlr
