// The SDL3 audio output: pulls stereo 16-bit frames from a `MelLayer` on
// SDL's audio thread (SDL converts them to the device's format and rate).
#pragma once

#include <memory>

#include "audio/mel_layer.h"

namespace sh {

// An open playback device fed by `layer`. Only the audio thread renders; any
// other access to the layer while the device exists must hold a `Lock`.
// Initializes and shuts down the SDL audio subsystem.
class AudioDevice {
 public:
  // Opens the default playback device at the layer's output rate and starts
  // it. `layer` must outlive the device. Throws std::runtime_error if SDL
  // fails.
  explicit AudioDevice(MelLayer& layer);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  // Holds off the audio thread while it lives (the SDL stream lock).
  class Lock {
   public:
    explicit Lock(AudioDevice& device);
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;

   private:
    AudioDevice& device_;
  };

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sh
