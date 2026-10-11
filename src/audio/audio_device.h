// The SDL3 audio output: pulls stereo 16-bit frames from a `MelLayer` on
// SDL's audio thread (SDL converts them to the device's format and rate).
// Not in the original: it reopens the output when the system loses it
// (`AudioDevice::check`, `AudioDevice::reopen`).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "audio/mel_layer.h"

struct SDL_AudioStream;

namespace sh {

// Why the output needs reopening.
enum class AudioFault : std::uint8_t {
  // SDL reported a playback device removed (SDL_EVENT_AUDIO_DEVICE_REMOVED).
  // On Android SDL has one default device: once lost, it stays a silent
  // "zombie" that streams are never moved off until the audio subsystem
  // restarts.
  kRemoved,
  // In the foreground, SDL has not pulled a frame for `AudioDevice::kStallMs`
  // (an output that did not restart, e.g. after the app's pause).
  kStalled,
  // The output is closed after a failed reopen: retried every
  // `AudioDevice::kRetryMs`.
  kClosed,
};

// Returns the fault's name in the lifecycle log.
std::string_view audio_fault_name(AudioFault fault);

// An open playback device fed by `layer`. Only the audio thread renders; any
// other access to the layer while the device exists must hold a `Lock`.
// Holds a reference to the SDL audio (and events) subsystem.
class AudioDevice {
 public:
  // Receives the device's diagnostic lines (opening, the system's audio
  // device events, faults and reopens).
  using Log = std::function<void(std::string_view)>;

  // The time without a frame pulled, in the foreground, after which the
  // output counts as stalled.
  static constexpr std::uint64_t kStallMs = 3000;
  // The time between two attempts to reopen a closed output.
  static constexpr std::uint64_t kRetryMs = 2000;

  // Opens the default playback device at the layer's output rate and starts
  // it. `layer` must outlive the device. Throws std::runtime_error if SDL
  // fails.
  explicit AudioDevice(MelLayer& layer);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  // Sends the diagnostic lines to `log` from now on, starting with the
  // output's current state.
  void set_log(Log log);

  // Returns the fault that requires a `reopen`, if any, `now_ms` being a
  // monotonic time in milliseconds and `foreground` false while the
  // program is in the background (where the output is expected to be
  // paused). Called regularly on the thread that polls SDL's events, it
  // also writes the audio device events seen since the last call to the
  // log.
  std::optional<AudioFault> check(std::uint64_t now_ms, bool foreground);

  // Closes the output, restarts SDL's audio subsystem (unless another
  // user still holds it) and opens the default playback device again.
  // Returns whether the output is open; when it is not, `check` reports
  // `kClosed` from `now_ms + kRetryMs`. Must not run while a `Lock` lives.
  bool reopen(AudioFault fault, std::uint64_t now_ms);

  // Returns whether the output is open.
  bool is_open() const;
  // Returns the SDL (logical) device the output plays on, 0 when closed.
  std::uint32_t device_id() const;
  // Returns the number of frames SDL pulled from the layer so far.
  std::uint64_t frames_fed() const;

  // Holds off the audio thread while it lives (the SDL stream lock).
  class Lock {
   public:
    explicit Lock(AudioDevice& device);
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

   private:
    SDL_AudioStream* stream_;
  };

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sh
