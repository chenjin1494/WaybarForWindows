#include "modules/windows/sway_language.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <cctype>
#include <string>

#include "win32/common.hpp"

namespace waybar::modules::sway {

namespace {

std::string locale_string(LANGID lang_id, LCTYPE type) {
  wchar_t buffer[256] = {};
  const int length = GetLocaleInfoW(lang_id, type, buffer, static_cast<int>(std::size(buffer)));
  if (length <= 1) return {};
  return waybar::win32::to_utf8(std::wstring(buffer, static_cast<size_t>(length - 1)));
}

}  // namespace

Language::Language(const std::string& id, const Json::Value& config)
    : ALabel(config, "language", id, "{short}", 1) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

Language::LayoutInfo Language::query_layout() const {
  LayoutInfo info;

  HKL layout = nullptr;
  HWND foreground = GetForegroundWindow();
  if (foreground != nullptr) {
    const DWORD thread_id = GetWindowThreadProcessId(foreground, nullptr);
    layout = GetKeyboardLayout(thread_id);
  }
  if (layout == nullptr) {
    layout = GetKeyboardLayout(0);
  }
  if (layout == nullptr) return info;

  const LANGID lang_id = LOWORD(reinterpret_cast<DWORD_PTR>(layout));
  // mingw 头文件可能缺少 LOCALE_SISO639L2，这里用 LOCALE_SABBREVLANGNAME（如 ENU/CHS）
  // 并归一化为两位小写语言码（en/zh/ja…），对常见语言足够。
  std::string abbrev = locale_string(lang_id, LOCALE_SABBREVLANGNAME);
  if (!abbrev.empty()) {
    std::transform(abbrev.begin(), abbrev.end(), abbrev.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    // 常见语言的 Windows 三字母缩写 → ISO 639-1 两字母码
    static const std::unordered_map<std::string, std::string> kIsoMap = {
        {"enu", "en"}, {"eng", "en"}, {"chs", "zh"}, {"cht", "zh"}, {"jpn", "ja"},
        {"kor", "ko"}, {"deu", "de"}, {"ger", "de"}, {"fra", "fr"}, {"fre", "fr"},
        {"rus", "ru"}, {"esp", "es"}, {"ita", "it"}, {"ptb", "pt"}, {"ptg", "pt"},
        {"nld", "nl"}, {"dut", "nl"}, {"plk", "pl"}, {"csy", "cs"}, {"hun", "hu"},
        {"trk", "tr"}, {"sve", "sv"}, {"nor", "nb"}, {"dan", "da"}, {"fin", "fi"},
        {"ell", "el"}, {"heb", "he"}, {"ara", "ar"}, {"tha", "th"}, {"vie", "vi"},
        {"ukr", "uk"}, {"rom", "ro"}, {"ind", "id"}, {"msl", "ms"},
    };
    const auto it = kIsoMap.find(abbrev);
    info.short_code = it != kIsoMap.end() ? it->second : abbrev.substr(0, 2);
  }
  info.long_name = locale_string(lang_id, LOCALE_SENGLISHLANGUAGENAME);

  wchar_t klid[KL_NAMELENGTH] = {};
  if (GetKeyboardLayoutNameW(klid)) {
    info.klid = waybar::win32::to_utf8(klid);
  }

  if (info.short_code.empty()) {
    info.short_code = std::to_string(lang_id);
  }
  if (info.long_name.empty()) {
    info.long_name = info.short_code;
  }
  return info;
}

auto Language::update() -> void {
  const LayoutInfo info = query_layout();
  if (info.short_code.empty()) {
    event_box_.hide();
    ALabel::update();
    return;
  }
  event_box_.show();

  auto format = format_;
  if (format.empty()) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  const auto state = info.short_code;
  const auto icons = std::vector<std::string>{state, info.klid, "default"};
  updateLabelAndTooltipForState(
      state, format, "{long}", fmt::arg("short", info.short_code),
      fmt::arg("long", info.long_name), fmt::arg("layout", info.klid),
      fmt::arg("icon", getIcon(0, icons)));

  label_.get_style_context()->remove_class("empty");
  ALabel::update();
}

}  // namespace waybar::modules::sway
