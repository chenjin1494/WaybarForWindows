#include "modules/windows/cava.hpp"

#include <audioclient.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <spdlog/spdlog.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

#include "win32/common.hpp"

namespace waybar::modules {

namespace {

constexpr int kWindowSize = 1024;
constexpr double kPi = 3.14159265358979323846;
const char* const kGlyphs[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

struct ComInit {
  bool ok = false;
  ComInit() { ok = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)); }
  ~ComInit() {
    if (ok) CoUninitialize();
  }
};

// Goertzel：单频点能量，O(N) 且足够绘制频谱
float goertzel(const std::vector<float>& samples, double frequency, double sample_rate) {
  const double omega = 2.0 * kPi * frequency / sample_rate;
  const double coeff = 2.0 * std::cos(omega);
  double s0 = 0.0;
  double s1 = 0.0;
  double s2 = 0.0;
  for (float sample : samples) {
    s0 = sample + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return static_cast<float>(std::sqrt(std::max(0.0, power)) / samples.size());
}

}  // namespace

Cava::Cava(const std::string& id, const Json::Value& config)
    : AModule(config, "custom-cava", id, false, false) {
  if (config_["bars"].isInt()) {
    bars_ = std::clamp(config_["bars"].asInt(), 4, 64);
  }
  if (config_["framerate"].isInt()) {
    framerate_ = std::clamp(config_["framerate"].asInt(), 5, 60);
  }
  label_.set_name("custom-cava");
  label_.get_style_context()->add_class(id);
  label_.get_style_context()->add_class(MODULE_CLASS);
  label_.set_markup(renderLine(std::vector<float>(bars_, 0.0f)));
  label_.show();
  event_box_.add(label_);
  try {
    worker_ = std::thread([this] { captureLoop(); });
  } catch (const std::exception& e) {
    spdlog::warn("cava: failed to start audio capture thread: {}", e.what());
  }
}

Cava::~Cava() {
  stop_ = true;
  alive_->store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::string Cava::renderLine(const std::vector<float>& levels) const {
  std::string line;
  line.reserve(levels.size() * 3);
  for (float level : levels) {
    const int index = std::clamp(static_cast<int>(level * 8.0f), 0, 7);
    line += kGlyphs[index];
  }
  return line;
}

void Cava::render(const std::vector<float>& levels) {
  const std::string markup = renderLine(levels);
  const auto alive = alive_;
  Cava* self = this;
  Glib::signal_idle().connect_once([self, alive, markup] {
    if (!alive->load()) return;
    self->label_.set_markup(markup);
  });
}

auto Cava::update() -> void {}

void Cava::captureLoop() {
  ComInit com;
  if (!com.ok) {
    spdlog::warn("cava: COM initialization failed");
    return;
  }

  IMMDeviceEnumerator* enumerator = nullptr;
  IMMDevice* device = nullptr;
  IAudioClient* client = nullptr;
  IAudioCaptureClient* capture = nullptr;
  WAVEFORMATEX* mix_format = nullptr;

  auto cleanup = [&] {
    if (capture) capture->Release();
    if (client) {
      client->Stop();
      client->Release();
    }
    if (device) device->Release();
    if (enumerator) enumerator->Release();
    if (mix_format) CoTaskMemFree(mix_format);
  };

  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&enumerator));
  if (FAILED(hr)) {
    spdlog::warn("cava: no audio device enumerator");
    cleanup();
    return;
  }
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
  if (FAILED(hr)) {
    spdlog::warn("cava: no default output device");
    cleanup();
    return;
  }
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(&client));
  if (FAILED(hr)) {
    spdlog::warn("cava: failed to activate audio client");
    cleanup();
    return;
  }
  hr = client->GetMixFormat(&mix_format);
  if (FAILED(hr) || mix_format == nullptr) {
    cleanup();
    return;
  }

  static const GUID kIeeeFloat = {0x00000003,
                                  0x0000,
                                  0x0010,
                                  {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
  const bool is_float =
      mix_format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
      (mix_format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
       IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix_format)->SubFormat, kIeeeFloat));
  const int channels = std::max<int>(1, mix_format->nChannels);
  if (!is_float || mix_format->wBitsPerSample != 32) {
    spdlog::warn("cava: unsupported mix format (need 32-bit float)");
    cleanup();
    return;
  }

  hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000, 0,
                          mix_format, nullptr);
  if (FAILED(hr)) {
    spdlog::warn("cava: loopback initialization failed: 0x{:08x}", static_cast<unsigned>(hr));
    cleanup();
    return;
  }
  hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
  if (FAILED(hr)) {
    cleanup();
    return;
  }
  client->Start();

  const double sample_rate = static_cast<double>(mix_format->nSamplesPerSec);
  std::vector<float> mono;
  mono.reserve(kWindowSize * 4);
  std::vector<float> window(kWindowSize);
  for (int i = 0; i < kWindowSize; ++i) {
    window[i] = 0.5f - 0.5f * std::cos(2.0 * kPi * i / (kWindowSize - 1));
  }

  std::vector<float> bands(bars_, 0.0f);
  std::vector<float> smoothed(bars_, 0.0f);
  std::vector<float> scratch(kWindowSize);
  const auto frame_interval = std::chrono::milliseconds(1000 / std::max(1, framerate_));
  auto last_render = std::chrono::steady_clock::now() - frame_interval;

  while (!stop_.load()) {
    UINT32 packet = 0;
    if (capture->GetNextPacketSize(&packet) == S_OK) {
      while (packet > 0) {
        BYTE* data = nullptr;
        UINT32 frames = 0;
        DWORD flags = 0;
        if (capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr) != S_OK) break;
        if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 && data != nullptr) {
          const auto* samples = reinterpret_cast<const float*>(data);
          for (UINT32 i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
              sum += samples[i * channels + ch];
            }
            mono.push_back(sum / channels);
          }
          if (mono.size() > static_cast<size_t>(kWindowSize) * 8) {
            mono.erase(mono.begin(), mono.end() - kWindowSize * 8);
          }
        }
        capture->ReleaseBuffer(frames);
        if (capture->GetNextPacketSize(&packet) != S_OK) break;
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - last_render >= frame_interval) {
      last_render = now;
      if (mono.size() >= static_cast<size_t>(kWindowSize)) {
        const size_t offset = mono.size() - kWindowSize;
        for (int i = 0; i < kWindowSize; ++i) {
          scratch[i] = mono[offset + i] * window[i];
        }
        const double low = 60.0;
        const double high = std::min(12000.0, sample_rate * 0.45);
        for (int band = 0; band < bars_; ++band) {
          const double t0 = static_cast<double>(band) / bars_;
          const double t1 = static_cast<double>(band + 1) / bars_;
          const double freq = low * std::pow(high / low, (t0 + t1) * 0.5);
          float magnitude = goertzel(scratch, freq, sample_rate);
          float db = 20.0f * std::log10(magnitude + 1e-9f);
          float level = (db + 70.0f) / 60.0f;  // -70dB..-10dB -> 0..1
          level = std::clamp(level, 0.0f, 1.0f);
          bands[band] = std::sqrt(level);
          float& smoothed_value = smoothed[band];
          smoothed_value = smoothed_value * 0.55f + bands[band] * 0.45f;
        }
        render(smoothed);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  cleanup();
}

}  // namespace waybar::modules
