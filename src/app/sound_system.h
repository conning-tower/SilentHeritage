// The program's sound output: the MEL layer with its effect samples, played
// through the SDL audio device when one can be opened.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "audio/audio_device.h"
#include "audio/mel_layer.h"
#include "audio/sample_table.h"
#include "core/random.h"
#include "game/station_env.h"
#include "game/shpath.h"

namespace sh {

class SoundSystem {
 public:
  // Sets up the layer reading effects from the install's SFX directory and
  // opens the audio device; when the device cannot be opened, prints why and
  // keeps the layer silent. `install` must outlive the system.
  SoundSystem(const Install& install, const ShPaths& paths);
  ~SoundSystem();
  SoundSystem(const SoundSystem&) = delete;
  SoundSystem& operator=(const SoundSystem&) = delete;

  // Returns whether sound reaches a device.
  bool has_device() const { return device_ != nullptr; }

  // `AudioDevice::set_log`; nothing without a device.
  void set_log(AudioDevice::Log log);
  // `AudioDevice::check`; nothing without a device.
  std::optional<AudioFault> audio_fault(std::uint64_t now_ms, bool foreground);
  // `AudioDevice::reopen`. Requires a device.
  bool reopen_audio(AudioFault fault, std::uint64_t now_ms);

  // Runs `fn` on the layer, holding off the audio thread.
  void with_layer(const std::function<void(MelLayer&)>& fn);
  // `MelLayer::play_sfx`, loading the sample before holding off the audio
  // thread.
  void play_sfx(std::int32_t id, std::int32_t volume = -1, std::int32_t pan = -1);
  // `start_speech_raw` under the lock (`pcm` as `read_speech_raw` returns
  // it). Returns whether a clip started.
  bool start_speech(std::optional<std::vector<std::uint8_t>> pcm);

  // The layer, for a user that holds `lock()` around each access (the
  // mission's `SoundPort`).
  MelLayer& layer() { return layer_; }
  // Returns the lock holding off the audio thread, or null without a device.
  SoundLock* lock() { return device_ ? &lock_ : nullptr; }

 private:
  class DeviceLock final : public SoundLock {
   public:
    explicit DeviceLock(SoundSystem& system) : system_(system) {}
    void lock() override { held_.emplace(*system_.device_); }
    void unlock() override { held_.reset(); }

   private:
    SoundSystem& system_;
    std::optional<AudioDevice::Lock> held_;
  };

  SampleTable samples_;
  WatcomRandom random_;
  MelLayer layer_;
  std::unique_ptr<AudioDevice> device_;
  DeviceLock lock_{*this};
};

}  // namespace sh
