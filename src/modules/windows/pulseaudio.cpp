#include "modules/windows/pulseaudio.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>

#include "util/command.hpp"

namespace waybar::modules {

namespace {

std::mutex g_com_mutex;

// Core Audio 的 COM 对象按“每次读取”创建/释放，避免跨线程持有；
// interval 默认 5s，开销可以忽略。
struct AudioState {
  bool available = false;
  bool muted = false;
  double volume = 0.0;  // 0.0 - 1.0

  // 采集端点（麦克风），用于上游的 {format_source} 占位符
  bool source_available = false;
  bool source_muted = false;
  double source_volume = 0.0;
};

AudioState read_audio_state() {
  AudioState state;

  std::lock_guard<std::mutex> lock(g_com_mutex);
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
    return state;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&enumerator)))) {
    if (SUCCEEDED(init)) CoUninitialize();
    return state;
  }

  const auto read_endpoint = [enumerator](EDataFlow flow, bool& available, bool& muted,
                                          double& volume) {
    IMMDevice* device = nullptr;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &device)) ||
        device == nullptr) {
      return;
    }
    IAudioEndpointVolume* endpoint = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&endpoint))) &&
        endpoint != nullptr) {
      float level = 0.0F;
      BOOL mute = FALSE;
      if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&level)) &&
          SUCCEEDED(endpoint->GetMute(&mute))) {
        available = true;
        volume = std::clamp(static_cast<double>(level), 0.0, 1.0);
        muted = mute != FALSE;
      }
      endpoint->Release();
    }
    device->Release();
  };

  read_endpoint(eRender, state.available, state.muted, state.volume);
  read_endpoint(eCapture, state.source_available, state.source_muted, state.source_volume);

  enumerator->Release();
  if (SUCCEEDED(init)) CoUninitialize();
  return state;
}

bool write_volume(double volume, bool mute) {
  bool ok = false;

  std::lock_guard<std::mutex> lock(g_com_mutex);
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) {
    return false;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator),
                              reinterpret_cast<void**>(&enumerator)))) {
    if (SUCCEEDED(init)) CoUninitialize();
    return false;
  }

  IMMDevice* device = nullptr;
  if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
      device != nullptr) {
    IAudioEndpointVolume* endpoint = nullptr;
    if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                   reinterpret_cast<void**>(&endpoint))) &&
        endpoint != nullptr) {
      const float clamped = static_cast<float>(std::clamp(volume, 0.0, 1.0));
      if (SUCCEEDED(endpoint->SetMasterVolumeLevelScalar(clamped, nullptr)) &&
          SUCCEEDED(endpoint->SetMute(mute, nullptr))) {
        ok = true;
      }
      endpoint->Release();
    }
    device->Release();
  }

  enumerator->Release();
  if (SUCCEEDED(init)) CoUninitialize();
  return ok;
}

}  // namespace

Pulseaudio::Pulseaudio(const std::string& id, const Json::Value& config)
    : ALabel(config, "pulseaudio", id, "{icon} {volume}%", 5),
      scroll_step_(config_["scroll-step"].isInt() ? config_["scroll-step"].asInt() : 5),
      max_volume_(config_["max-volume"].isInt() ? config_["max-volume"].asInt() : 100) {
  thread_ = [this] {
    dp.emit();
    thread_.sleep_for(interval_);
  };
}

Pulseaudio::~Pulseaudio() = default;

bool Pulseaudio::setVolume(double volume) const {
  return write_volume(volume, false);
}

bool Pulseaudio::toggleMute() const {
  const AudioState state = read_audio_state();
  if (!state.available) {
    return false;
  }
  return write_volume(state.volume, !state.muted);
}

auto Pulseaudio::update() -> void {
  const AudioState state = read_audio_state();

  if (!state.available) {
    // 没有默认输出设备（例如无音频服务）：隐藏模块
    event_box_.hide();
    ALabel::update();
    return;
  }

  const int volume = static_cast<int>(std::lround(state.volume * 100.0));

  auto format = state.muted ? (config_["format-muted"].isString()
                                  ? config_["format-muted"].asString()
                                  : std::string("{icon} muted"))
                           : format_;
  if (!state.muted && config_["format"].isString()) {
    format = config_["format"].asString();
  }

  auto state_name = getState(volume);
  if (!state_name.empty() && config_["format-" + state_name].isString()) {
    format = config_["format-" + state_name].asString();
  }

  if (format.empty()) {
    event_box_.hide();
    ALabel::update();
    return;
  }

  event_box_.show();

  // 上游 {format_source}：采集端点的音量/静音模板
  const int source_volume = static_cast<int>(std::lround(state.source_volume * 100.0));
  const std::string source_template =
      state.source_muted
          ? (config_["format-source-muted"].isString() ? config_["format-source-muted"].asString()
                                                       : std::string("muted"))
          : (config_["format-source"].isString() ? config_["format-source"].asString()
                                                 : std::string("{volume}%"));
  std::string format_source;
  if (state.source_available) {
    try {
      format_source = fmt::format(fmt::runtime(source_template),
                                  fmt::arg("volume", source_volume));
    } catch (const fmt::format_error& e) {
      spdlog::warn("pulseaudio: invalid format-source '{}': {}", source_template, e.what());
    }
  }

  const auto icon_alts = state.muted
                             ? std::vector<std::string>{"muted", "speaker", "default"}
                             : std::vector<std::string>{"speaker", "headphone", "headset",
                                                        "bluetooth", "default"};
  const std::string icon = getIcon(state.muted ? 0 : static_cast<uint16_t>(volume), icon_alts);

  setLabelMarkup(fmt::format(fmt::runtime(format), fmt::arg("volume", volume),
                             fmt::arg("icon", icon), fmt::arg("format_source", format_source)));

  if (tooltipEnabled()) {
    auto tooltip_format = config_["tooltip-format"].isString()
                              ? config_["tooltip-format"].asString()
                              : std::string("{volume}%");
    setTooltipMarkup(fmt::format(fmt::runtime(tooltip_format), fmt::arg("volume", volume),
                                 fmt::arg("icon", icon),
                                 fmt::arg("format_source", format_source)));
  }

  label_.get_style_context()->remove_class("muted");
  if (state.muted) {
    label_.get_style_context()->add_class("muted");
  }

  ALabel::update();
}

bool Pulseaudio::handleToggle(GdkEventButton* const& e) {
  auto on_click = [&](const char* key) -> std::string {
    return config_[key].isString() ? config_[key].asString() : std::string();
  };

  if (e->button == 1) {
    if (auto command = on_click("on-click"); !command.empty()) {
      util::command::exec(command, "");
      return true;
    }
    if (toggleMute()) {
      update();
    }
    return true;
  }
  if (e->button == 2) {
    if (auto command = on_click("on-click-middle"); !command.empty()) {
      util::command::exec(command, "");
      return true;
    }
  }
  if (e->button == 3) {
    if (auto command = on_click("on-click-right"); !command.empty()) {
      util::command::exec(command, "");
      return true;
    }
  }
  return false;
}

bool Pulseaudio::handleScroll(GdkEventScroll* e) {
  const SCROLL_DIR dir = getScrollDir(e);
  if (dir == SCROLL_DIR::NONE) {
    return false;
  }

  auto on_scroll = [&](const char* key) -> std::string {
    return config_[key].isString() ? config_[key].asString() : std::string();
  };

  const bool reverse = config_["reverse-scroll"].isBool() && config_["reverse-scroll"].asBool();
  std::string command;
  if (dir == SCROLL_DIR::UP) {
    command = on_scroll(reverse ? "on-scroll-down" : "on-scroll-up");
  } else if (dir == SCROLL_DIR::DOWN) {
    command = on_scroll(reverse ? "on-scroll-up" : "on-scroll-down");
  }
  if (!command.empty()) {
    util::command::exec(command, "");
    return true;
  }

  const AudioState state = read_audio_state();
  if (!state.available) {
    return false;
  }

  const double step = static_cast<double>(scroll_step_) / 100.0;
  const double max_volume = static_cast<double>(max_volume_) / 100.0;
  double target = state.volume + (dir == SCROLL_DIR::UP ? step : -step);
  target = std::clamp(target, 0.0, max_volume);

  if (write_volume(target, false)) {
    update();
    return true;
  }
  return false;
}

}  // namespace waybar::modules
