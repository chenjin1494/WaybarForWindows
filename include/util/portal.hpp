#pragma once

#include <string>

#include "fmt/format.h"

#ifdef _WIN32
#include <memory>

#include <sigc++/signal.h>
#else
#include <giomm/dbusproxy.h>
#endif

namespace waybar {

enum class Appearance {
  UNKNOWN = 0,
  DARK = 1,
  LIGHT = 2,
};

#ifdef _WIN32

// Windows 实现：读取注册表 AppsUseLightTheme 并监听变化（替代 XDG desktop portal）。
class Portal {
 public:
  Portal();
  ~Portal();
  Portal(const Portal&) = delete;
  Portal& operator=(const Portal&) = delete;

  void refreshAppearance();
  Appearance getAppearance();

  typedef sigc::signal<void, Appearance> type_signal_appearance_changed;
  type_signal_appearance_changed signal_appearance_changed() { return m_signal_appearance_changed; }

 private:
  type_signal_appearance_changed m_signal_appearance_changed;
  Appearance currentMode = Appearance::UNKNOWN;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

#else

class Portal : private Gio::DBus::Proxy {
 public:
  Portal();
  void refreshAppearance();
  Appearance getAppearance();

  typedef sigc::signal<void, Appearance> type_signal_appearance_changed;
  type_signal_appearance_changed signal_appearance_changed() { return m_signal_appearance_changed; }

 private:
  type_signal_appearance_changed m_signal_appearance_changed;
  Appearance currentMode;
  void on_signal(const Glib::ustring& sender_name, const Glib::ustring& signal_name,
                 const Glib::VariantContainerBase& parameters);
  void onAppearanceReceived(Glib::RefPtr<Gio::AsyncResult>& result);
};

#endif

}  // namespace waybar

template <>
struct fmt::formatter<waybar::Appearance> : formatter<fmt::string_view> {
  // parse is inherited from formatter<string_view>.
  auto format(waybar::Appearance c, format_context& ctx) const;
};
