#pragma once

#include <fmt/format.h>
#include <gdk/gdk.h>
#ifndef _WIN32
#include <gdk/gdkwayland.h>
#include <wayland-client.h>
#endif

#include <memory>

#include "bar.hpp"
#include "config.hpp"
#include "util/SafeSignal.hpp"
#include "util/css_reload_helper.hpp"
#include "util/portal.hpp"

#ifdef _WIN32
#include "win32/control.hpp"
#else
struct zwp_idle_inhibitor_v1;
struct zwp_idle_inhibit_manager_v1;
struct ext_idle_notifier_v1;
#ifdef HAVE_BAR_BLUR
struct ext_background_effect_manager_v1;
#endif
#endif

namespace waybar {

class Client {
 public:
  static Client* inst();
  int main(int argc, char* argv[]);
  void reset();

  Glib::RefPtr<Gtk::Application> gtk_app;
  Glib::RefPtr<Gdk::Display> gdk_display;
#ifndef _WIN32
  struct wl_display* wl_display = nullptr;
  struct wl_registry* registry = nullptr;
  struct zxdg_output_manager_v1* xdg_output_manager = nullptr;
  struct zwp_idle_inhibit_manager_v1* idle_inhibit_manager = nullptr;
  struct ext_idle_notifier_v1* idle_notifier = nullptr;
#ifdef HAVE_BAR_BLUR
  struct ext_background_effect_manager_v1* background_effect_manager = nullptr;
#endif
#endif
  std::vector<std::unique_ptr<Bar>> bars;
  Config config;
  std::string bar_id;

#ifdef _WIN32
  // 控制命令（--toggle/--reload 等）在主线程处理完后，告知 main 是否需要重新初始化
  bool takeReloadRequest() {
    const bool requested = reload_requested_;
    reload_requested_ = false;
    return requested;
  }
  const std::string& configPath() const { return config_file_; }
#endif

 private:
  Client() = default;
  const std::string getStyle(const std::string& style, std::optional<Appearance> appearance);
  void bindInterfaces();
  auto setupCss(const std::string& css_file) -> void;
  std::vector<Json::Value> getOutputConfigs(struct waybar_output& output);
  void createBarsBatch();

#ifndef _WIN32
  void handleOutput(struct waybar_output& output);
  struct waybar_output& getOutput(void*);
  static void handleGlobal(void* data, struct wl_registry* registry, uint32_t name,
                           const char* interface, uint32_t version);
  static void handleGlobalRemove(void* data, struct wl_registry* registry, uint32_t name);
  static void handleOutputLogicalSize(void*, struct zxdg_output_v1*, int32_t, int32_t);
  static void handleOutputDone(void*, struct zxdg_output_v1*);
  static void handleOutputName(void*, struct zxdg_output_v1*, const char*);
  static void handleOutputDescription(void*, struct zxdg_output_v1*, const char*);
#endif

  void handleMonitorAdded(Glib::RefPtr<Gdk::Monitor> monitor);
  void handleMonitorRemoved(Glib::RefPtr<Gdk::Monitor> monitor);
  void handleDeferredMonitorRemoval(Glib::RefPtr<Gdk::Monitor> monitor);

#ifdef _WIN32
  void startControlServer();
  // 在主线程执行一条控制请求，返回应答文本（"OK" / "ERR ..."）
  std::string applyControlRequest(const win32::ControlRequest& request);
  void handleControlRequest(const win32::ControlRequest& request);
#endif

  Glib::RefPtr<Gtk::StyleContext> style_context_;
  Glib::RefPtr<Gtk::CssProvider> css_provider_;
  std::unique_ptr<Portal> portal;
  std::list<struct waybar_output> outputs_;
  std::unique_ptr<CssReloadHelper> m_cssReloadHelper;
  std::string m_cssFile;
  sigc::connection monitor_added_connection_;
  sigc::connection monitor_removed_connection_;
  std::list<waybar_output*> pending_outputs_;
  bool bars_scheduled_ = false;
#ifdef _WIN32
  std::unique_ptr<win32::ControlServer> control_server_;
  waybar::SafeSignal<win32::ControlRequest> control_signal_;
  bool reload_requested_ = false;
  std::string config_file_;
#endif
};

}  // namespace waybar
