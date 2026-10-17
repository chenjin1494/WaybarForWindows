#include "modules/windows/pulseaudio_slider.hpp"

#include <spdlog/spdlog.h>
#include <windows.h>

#include <audioclient.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace waybar::modules {

namespace {

std::mutex g_com_mutex;

struct AudioState {
  bool available = false;
  bool muted = false;
  double volume = 0.0;
};

AudioState read_audio_state() {
  AudioState state;
  std::lock_guard<std::mutex> lock(g_com_mutex);
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) return state;

  IMMDeviceEnumerator* enumerator = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                 __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&enumerator)))) {
    IMMDevice* device = nullptr;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)) &&
        device != nullptr) {
      IAudioEndpointVolume* endpoint = nullptr;
      if (SUCCEEDED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(&endpoint))) &&
          endpoint != nullptr) {
        float volume = 0.0F;
        BOOL mute = FALSE;
        if (SUCCEEDED(endpoint->GetMasterVolumeLevelScalar(&volume)) &&
            SUCCEEDED(endpoint->GetMute(&mute))) {
          state.available = true;
          state.volume = std::clamp(static_cast<double>(volume), 0.0, 1.0);
          state.muted = mute != FALSE;
        }
        endpoint->Release();
      }
      device->Release();
    }
    enumerator->Release();
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return state;
}

bool write_volume(double volume, bool mute) {
  bool ok = false;
  std::lock_guard<std::mutex> lock(g_com_mutex);
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(init) && init != RPC_E_CHANGED_MODE) return false;

  IMMDeviceEnumerator* enumerator = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                 __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&enumerator)))) {
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
  }
  if (SUCCEEDED(init)) CoUninitialize();
  return ok;
}

}  // namespace

PulseaudioSlider::PulseaudioSlider(const std::string& id, const Json::Value& config)
    : ASlider(config, "pulseaudio/slider", id) {
  const AudioState state = read_audio_state();
  if (state.available) {
    curr_ = static_cast<int>(std::lround(state.volume * 100.0));
  }
  updating_ = true;
  scale_.set_value(curr_);
  updating_ = false;
}

void PulseaudioSlider::update() {
  const AudioState state = read_audio_state();
  if (!state.available) {
    event_box_.hide();
    return;
  }
  event_box_.show();
  int value = static_cast<int>(std::lround(state.volume * 100.0));
  if (state.muted && config_["zero-on-mute"].asBool()) {
    value = 0;
  }
  if (value != curr_) {
    curr_ = value;
    updating_ = true;
    scale_.set_value(curr_);
    updating_ = false;
  }
  AModule::update();
}

void PulseaudioSlider::onValueChanged() {
  if (updating_) return;
  curr_ = static_cast<int>(scale_.get_value());
  const AudioState state = read_audio_state();
  if (!state.available) return;
  const bool mute = config_["unmute-on-volume-change"].asBool() ? false : state.muted;
  write_volume(static_cast<double>(curr_) / 100.0, mute);
}

}  // namespace waybar::modules
