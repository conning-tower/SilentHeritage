// The SDL sound output and its recovery when the system loses it, on SDL's
// dummy audio driver: the device events are faked with SDL_PushEvent, a
// stall with the device paused.

#include <SDL3/SDL.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "audio/audio_device.h"
#include "audio/mel_layer.h"
#include "audio/sample_table.h"

namespace sh {

namespace {

// Waits up to two seconds for SDL to pull frames beyond `after`.
bool wait_for_frames(const AudioDevice& device, std::uint64_t after) {
  for (int i = 0; i < 400; ++i) {
    if (device.frames_fed() > after) {
      return true;
    }
    SDL_Delay(5);
  }
  return false;
}

// Returns the first physical playback device, 0 when none.
SDL_AudioDeviceID first_playback_device() {
  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  const SDL_AudioDeviceID id = count > 0 ? ids[0] : 0;
  SDL_free(ids);
  return id;
}

void push_device_event(std::uint32_t type, SDL_AudioDeviceID which, bool recording) {
  SDL_Event e{};
  e.type = type;
  e.adevice.which = which;
  e.adevice.recording = recording;
  SDL_PushEvent(&e);
}

bool has_line(const std::vector<std::string>& lines, std::string_view text) {
  return std::ranges::any_of(lines, [&](const std::string& l) { return l.find(text) != std::string::npos; });
}

// A layer of silent effects on a dummy output that logs into `lines`.
struct DummyOutput {
  DummyOutput() {
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    device.emplace(layer);
    device->set_log([this](std::string_view l) { lines.emplace_back(l); });
  }
  ~DummyOutput() {
    device.reset();
    SDL_FlushEvents(SDL_EVENT_AUDIO_DEVICE_ADDED, SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED);
  }

  SampleTable table{nullptr};
  MelLayer layer{table, [] { return 0; }};
  std::optional<AudioDevice> device;
  std::vector<std::string> lines;
};

}  // namespace

TEST_CASE("A removed playback device restarts SDL's audio and reopens the output", "[audio][device]") {
  DummyOutput out;
  AudioDevice& device = *out.device;
  REQUIRE(out.lines.size() == 1);
  CHECK(out.lines[0].starts_with("audio: playing through dummy on "));
  REQUIRE(wait_for_frames(device, 0));
  CHECK_FALSE(device.check(0, true));

  const SDL_AudioDeviceID physical = first_playback_device();
  REQUIRE(physical != 0);
  push_device_event(SDL_EVENT_AUDIO_DEVICE_REMOVED, physical, false);
  const std::optional<AudioFault> fault = device.check(10, true);
  CHECK(fault == AudioFault::kRemoved);
  CHECK(has_line(out.lines, std::format("audio: physical device {} removed", physical)));

  REQUIRE(device.reopen(*fault, 10));
  CHECK(device.is_open());
  CHECK(has_line(out.lines, "audio: reopening the output (device removed)"));
  CHECK(out.lines.back().starts_with("audio: playing through dummy"));
  // A new subsystem: its devices are new ones.
  CHECK(first_playback_device() != physical);
  const std::uint64_t fed = device.frames_fed();
  CHECK(wait_for_frames(device, fed));
  CHECK_FALSE(device.check(20, true));

  // Sound still reaches the layer through the new stream's lock.
  {
    const AudioDevice::Lock lock(device);
    out.layer.set_sfx_volume(50);
  }
  CHECK(out.layer.sfx_volume() == 50);
}

TEST_CASE("Recording devices and other streams' devices removed are only logged", "[audio][device]") {
  DummyOutput out;
  AudioDevice& device = *out.device;
  const SDL_AudioDeviceID own = device.device_id();
  REQUIRE(own != 0);

  push_device_event(SDL_EVENT_AUDIO_DEVICE_REMOVED, first_playback_device(), true);
  CHECK_FALSE(device.check(0, true));
  CHECK(out.lines.size() == 1);

  SDL_AudioStream* other = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr, nullptr, nullptr);
  REQUIRE(other != nullptr);
  const SDL_AudioDeviceID logical = SDL_GetAudioStreamDevice(other);
  push_device_event(SDL_EVENT_AUDIO_DEVICE_REMOVED, logical, false);
  CHECK_FALSE(device.check(0, true));
  CHECK(out.lines.back() == std::format("audio: logical device {} removed", logical));
  SDL_DestroyAudioStream(other);

  // Its own logical device lost is a fault.
  push_device_event(SDL_EVENT_AUDIO_DEVICE_REMOVED, own, false);
  CHECK(device.check(0, true) == AudioFault::kRemoved);
}

TEST_CASE("An output not pulled for a while in the foreground is reopened", "[audio][device]") {
  DummyOutput out;
  AudioDevice& device = *out.device;
  REQUIRE(wait_for_frames(device, 0));
  // SDL stops pulling from a paused device, as from an output that did not
  // restart.
  REQUIRE(SDL_PauseAudioDevice(device.device_id()));
  SDL_Delay(100);

  CHECK_FALSE(device.check(1000, true));
  CHECK_FALSE(device.check(1000 + AudioDevice::kStallMs - 1, true));
  // The background does not count, and the wait starts over in the
  // foreground.
  CHECK_FALSE(device.check(100'000, false));
  CHECK_FALSE(device.check(100'001, true));
  CHECK_FALSE(device.check(100'000 + AudioDevice::kStallMs, true));
  const std::optional<AudioFault> fault = device.check(100'001 + AudioDevice::kStallMs, true);
  CHECK(fault == AudioFault::kStalled);

  REQUIRE(device.reopen(*fault, 200'000));
  CHECK(has_line(out.lines, "audio: reopening the output (output stalled)"));
  const std::uint64_t fed = device.frames_fed();
  CHECK(wait_for_frames(device, fed));
}

TEST_CASE("A failed reopen leaves the output closed and retries", "[audio][device]") {
  DummyOutput out;
  AudioDevice& device = *out.device;
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "no-such-driver");
  CHECK_FALSE(device.reopen(AudioFault::kRemoved, 1000));
  CHECK_FALSE(device.is_open());
  CHECK(device.device_id() == 0);
  CHECK(has_line(out.lines, "audio: reopen failed (1 in a row)"));
  // The layer stays usable without an audio thread.
  {
    const AudioDevice::Lock lock(device);
    out.layer.set_sfx_volume(40);
  }
  CHECK_FALSE(device.check(1000 + AudioDevice::kRetryMs - 1, true));
  CHECK(device.check(1000 + AudioDevice::kRetryMs, true) == AudioFault::kClosed);
  CHECK_FALSE(device.reopen(AudioFault::kClosed, 4000));
  // Failures are logged at powers of two.
  CHECK(has_line(out.lines, "audio: reopen failed (2 in a row)"));
  CHECK_FALSE(device.reopen(AudioFault::kClosed, 8000));
  CHECK_FALSE(has_line(out.lines, "(3 in a row)"));

  SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  CHECK(device.check(8000 + AudioDevice::kRetryMs, true) == AudioFault::kClosed);
  REQUIRE(device.reopen(AudioFault::kClosed, 10'000));
  CHECK(has_line(out.lines, "audio: reopened after 3 failed attempts"));
  CHECK(wait_for_frames(device, device.frames_fed()));
}

}  // namespace sh
