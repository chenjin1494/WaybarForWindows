#pragma once

#include <gdkmm/monitor.h>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <json/json.h>

#include <memory>
#include <optional>
#include <vector>

#include "AModule.hpp"
#include "group.hpp"
#include "util/kill_signal.hpp"

#ifdef _WIN32
#include "win32/bar_backend.hpp"
#include "win32/layout.hpp"
#else
#include "xdg-output-unstable-v1-client-protocol.h"
#endif

#ifdef HAVE_BAR_BLUR
struct ext_background_effect_surface_v1;
#endif

struct wl_surface;

namespace waybar {

class Factory;
struct waybar_output {
  Glib::RefPtr<Gdk::Monitor> monitor;
  std::string name;
  std::string identifier;
  int32_t width;
  int32_t height;

#ifndef _WIN32
  std::unique_ptr<struct zxdg_output_v1, decltype(&zxdg_output_v1_destroy)> xdg_output = {
      nullptr, &zxdg_output_v1_destroy};
#endif
};

enum class bar_layer : uint8_t {
  BOTTOM,
  TOP,
  OVERLAY,
};

struct bar_margins {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
};

struct bar_mode {
  bar_layer layer;
  bool exclusive;
  bool passthrough;
  bool visible;
};

#ifdef HAVE_SWAY
namespace modules::sway {
class BarIpcClient;
}
#endif  // HAVE_SWAY

class Bar : public sigc::trackable {
 public:
  using bar_mode_map = std::map<std::string, struct bar_mode>;
  static const bar_mode_map PRESET_MODES;
  static const std::string MODE_DEFAULT;
  static const std::string MODE_INVISIBLE;

  Bar(struct waybar_output* w_output, const Json::Value&);
  Bar(const Bar&) = delete;
  ~Bar();

  void setMode(const std::string& mode);
  void setVisible(bool value);
  // 全屏应用时真正隐藏原生窗口（不改变模块更新与手动显示状态）
  void setWindowHidden(bool hidden);
  void toggle();
  void show();
  void hide();
  void handleSignal(int);
  util::KillSignalAction getOnSigusr1Action();
  util::KillSignalAction getOnSigusr2Action();

  void toggleSuspend(bool suspend);

#ifdef HAVE_BAR_BLUR
  // Re-applies the blur region; called after a CSS reload since the bar's
  // border-radius may have changed without a resize event.
  void updateBlurRegion();
#endif

  struct waybar_output* output;
  Json::Value config;
  struct wl_surface* surface;
  bool visible = true;
  Gtk::Window window;
  Gtk::Orientation orientation = Gtk::ORIENTATION_HORIZONTAL;
  Gtk::PositionType position = Gtk::POS_TOP;

  int x_global;
  int y_global;

#ifdef HAVE_SWAY
  std::string bar_id;
#endif

#ifdef _WIN32
  // Windows 平台后端（定位/置顶/AppBar/鼠标穿透）
  win32::BarBackend& backend() { return *backend_; }
  const win32::BarBackend& backend() const { return *backend_; }
#endif

 private:
  void onMap(GdkEventAny*);
  auto setupWidgets() -> void;
  void getModules(const Factory&, const std::string&, waybar::Group*);
  void setupAltFormatKeyForModule(const std::string& module_name);
  void setupAltFormatKeyForModuleList(const char* module_list_name);
  void setMode(const bar_mode&);
  void setPassThrough(bool passthrough);
  void setPosition(Gtk::PositionType position);
  void forceLayerCommit();
  void onConfigure(GdkEventConfigure* ev);
  void configureGlobalOffset(int width, int height);
  void onOutputGeometryChanged();
#ifdef HAVE_BAR_BLUR
  void setupBackgroundBlur();
#endif

#ifdef _WIN32
  win32::LayoutOptions buildLayoutOptions() const;
  win32::MonitorInfo buildMonitorInfo() const;
  void applyModeToBackend(const bar_mode& mode);
  std::unique_ptr<win32::BarBackend> backend_;
#endif

  /* Copy initial set of modes to allow customization */
  bar_mode_map configured_modes = PRESET_MODES;
  std::string last_mode_{MODE_DEFAULT};

  struct bar_margins margins_;
  uint32_t width_, height_;
  bool passthrough_;
#ifdef HAVE_BAR_BLUR
  bool blur_enabled_ = false;
  struct ext_background_effect_surface_v1* background_effect_surface_ = nullptr;
#endif

  Gtk::Box left_;
  Gtk::Box center_;
  Gtk::Box right_;
  Gtk::Box box_;
  std::vector<std::shared_ptr<waybar::AModule>> modules_left_;
  std::vector<std::shared_ptr<waybar::AModule>> modules_center_;
  std::vector<std::shared_ptr<waybar::AModule>> modules_right_;
#ifdef HAVE_SWAY
  using BarIpcClient = modules::sway::BarIpcClient;
  std::unique_ptr<BarIpcClient> _ipc_client;
#endif
  std::vector<std::shared_ptr<waybar::AModule>> modules_all_;

  waybar::util::KillSignalAction onSigusr1 = util::SIGNALACTION_DEFAULT_SIGUSR1;
  waybar::util::KillSignalAction onSigusr2 = util::SIGNALACTION_DEFAULT_SIGUSR2;

  /* Disconnected in ~Bar before the modules are destroyed (#5182). */
  sigc::connection map_conn_;
  sigc::connection unmap_conn_;
};

}  // namespace waybar
