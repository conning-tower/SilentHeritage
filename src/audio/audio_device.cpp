#include "audio/audio_device.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sh {

namespace {

// The longest stall limit, reached by doubling after each reopen that did
// not bring the output back.
constexpr std::uint64_t kMaxStallMs = 60'000;

}  // namespace

std::string_view audio_fault_name(AudioFault fault) {
  switch (fault) {
    case AudioFault::kRemoved:
      return "device removed";
    case AudioFault::kStalled:
      return "output stalled";
    case AudioFault::kClosed:
      return "output closed";
  }
  return "?";
}

struct AudioDevice::Impl {
  explicit Impl(MelLayer& l) : layer(l) {}

  MelLayer& layer;
  SDL_AudioStream* stream = nullptr;
  std::vector<std::int16_t> buffer;  // Audio thread only.
  std::atomic<std::uint64_t> fed{0};
  bool audio_held = false;  // A reference to SDL's audio subsystem.
  Log log;

  // Written by the event watch, read by `check`.
  std::mutex events_mutex;
  std::vector<std::string> event_lines;
  std::atomic<bool> removed{false};
  std::atomic<SDL_AudioDeviceID> stream_device{0};

  // `check`'s view of the output's progress.
  std::optional<std::uint64_t> progress_at;
  std::uint64_t seen_fed = 0;
  std::uint64_t stall_ms = kStallMs;
  std::uint64_t retry_at = 0;
  int failures = 0;  // Failed reopens in a row.

  // Called by SDL with the stream locked; `additional` is in bytes.
  static void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto* self = static_cast<Impl*>(user);
    if (additional <= 0) {
      return;
    }
    const std::size_t frames = static_cast<std::size_t>(additional) / (2 * sizeof(std::int16_t));
    self->buffer.resize(frames * 2);
    self->layer.render(self->buffer);
    SDL_PutAudioStreamData(stream, self->buffer.data(),
                           static_cast<int>(self->buffer.size() * sizeof(std::int16_t)));
    self->fed.fetch_add(frames, std::memory_order_relaxed);
  }

  // The event watch: notes the playback device events (SDL sends them from
  // the event poll that delivers them).
  static bool SDLCALL on_event(void* user, SDL_Event* event) {
    const char* kind = event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED          ? "removed"
                       : event->type == SDL_EVENT_AUDIO_DEVICE_ADDED          ? "added"
                       : event->type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED ? "format changed"
                                                                              : nullptr;
    if (kind == nullptr || event->adevice.recording) {
      return true;
    }
    auto* self = static_cast<Impl*>(user);
    const SDL_AudioDeviceID id = event->adevice.which;
    const bool physical = SDL_IsAudioDevicePhysical(id);
    // A physical device lost may be the one under the output (SDL does not
    // tell which); a logical one only matters when it is the output's.
    const bool lost =
        event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED && (physical || id == self->stream_device.load());
    std::string line = std::format("audio: {} device {} {}", physical ? "physical" : "logical", id, kind);
    if (event->type == SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED) {
      SDL_AudioSpec spec{};
      int frames = 0;
      if (SDL_GetAudioDeviceFormat(id, &spec, &frames)) {
        line += std::format(" ({} Hz, {} channels, {} frames per buffer)", spec.freq, spec.channels, frames);
      }
    }
    {
      const std::lock_guard<std::mutex> guard(self->events_mutex);
      self->event_lines.push_back(std::move(line));
    }
    if (lost) {
      self->removed = true;
    }
    return true;
  }

  void write(std::string_view line) const {
    if (log) {
      log(line);
    }
  }

  // Opens and starts the output on the default playback device. Returns
  // false with SDL's error set.
  bool open() {
    const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = 2, .freq = layer.output_rate()};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::feed, this);
    if (stream == nullptr) {
      return false;
    }
    if (!SDL_ResumeAudioStreamDevice(stream)) {
      const std::string error = SDL_GetError();
      close();
      SDL_SetError("%s", error.c_str());
      return false;
    }
    stream_device = SDL_GetAudioStreamDevice(stream);
    return true;
  }

  void close() {
    SDL_DestroyAudioStream(stream);
    stream = nullptr;
    stream_device = 0;
  }

  // Returns the output's state for the log.
  std::string describe() const {
    if (stream == nullptr) {
      return "audio: output closed";
    }
    const SDL_AudioDeviceID id = stream_device.load();
    const char* driver = SDL_GetCurrentAudioDriver();
    const char* name = SDL_GetAudioDeviceName(id);
    SDL_AudioSpec spec{};
    int frames = 0;
    std::string line = std::format("audio: playing through {} on \"{}\"", driver != nullptr ? driver : "?",
                                   name != nullptr ? name : "?");
    if (SDL_GetAudioDeviceFormat(id, &spec, &frames)) {
      line += std::format(" at {} Hz, {} channels, {} frames per buffer", spec.freq, spec.channels, frames);
    }
    return line;
  }
};

AudioDevice::AudioDevice(MelLayer& layer) : impl_(std::make_unique<Impl>(layer)) {
  // The events subsystem is held on its own: the watch must outlive the
  // audio subsystem's restarts.
  if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) {
    throw std::runtime_error(std::format("SDL_InitSubSystem(EVENTS): {}", SDL_GetError()));
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    const std::string error = SDL_GetError();
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    throw std::runtime_error(std::format("SDL_InitSubSystem(AUDIO): {}", error));
  }
  impl_->audio_held = true;
  if (!impl_->open()) {
    const std::string error = SDL_GetError();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    throw std::runtime_error(std::format("SDL_OpenAudioDeviceStream: {}", error));
  }
  if (!SDL_AddEventWatch(&Impl::on_event, impl_.get())) {
    const std::string error = SDL_GetError();
    impl_->close();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    throw std::runtime_error(std::format("SDL_AddEventWatch: {}", error));
  }
}

AudioDevice::~AudioDevice() {
  SDL_RemoveEventWatch(&Impl::on_event, impl_.get());
  impl_->close();
  if (impl_->audio_held) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

void AudioDevice::set_log(Log log) {
  impl_->log = std::move(log);
  impl_->write(impl_->describe());
}

std::optional<AudioFault> AudioDevice::check(std::uint64_t now_ms, bool foreground) {
  Impl& d = *impl_;
  std::vector<std::string> lines;
  {
    const std::lock_guard<std::mutex> guard(d.events_mutex);
    lines.swap(d.event_lines);
  }
  for (const std::string& line : lines) {
    d.write(line);
  }
  if (d.stream == nullptr) {
    return now_ms >= d.retry_at ? std::optional(AudioFault::kClosed) : std::nullopt;
  }
  if (d.removed.exchange(false)) {
    return AudioFault::kRemoved;
  }
  if (!foreground) {
    // Paused with the app: the time in the background does not count.
    d.progress_at.reset();
    return std::nullopt;
  }
  const std::uint64_t fed = d.fed.load(std::memory_order_relaxed);
  if (!d.progress_at || fed != d.seen_fed) {
    if (d.progress_at && fed != d.seen_fed) {
      d.stall_ms = kStallMs;
    }
    d.seen_fed = fed;
    d.progress_at = now_ms;
    return std::nullopt;
  }
  if (now_ms - *d.progress_at >= d.stall_ms) {
    return AudioFault::kStalled;
  }
  return std::nullopt;
}

bool AudioDevice::reopen(AudioFault fault, std::uint64_t now_ms) {
  Impl& d = *impl_;
  if (d.failures == 0) {
    d.write(std::format("audio: reopening the output ({})", audio_fault_name(fault)));
  }
  if (fault == AudioFault::kStalled) {
    d.stall_ms = std::min(d.stall_ms * 2, kMaxStallMs);
  }
  d.close();
  if (d.audio_held) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    d.audio_held = false;
  }
  if (SDL_WasInit(SDL_INIT_AUDIO) != 0) {
    // SDL keeps the lost device as long as its subsystem lives.
    d.write("audio: another user holds the audio subsystem: it does not restart");
  }
  d.audio_held = SDL_InitSubSystem(SDL_INIT_AUDIO);
  if (!d.audio_held || !d.open()) {
    ++d.failures;
    // Logged at the 1st, 2nd, 4th, 8th... failure in a row.
    if ((d.failures & (d.failures - 1)) == 0) {
      d.write(std::format("audio: reopen failed ({} in a row): {}", d.failures, SDL_GetError()));
    }
    d.retry_at = now_ms + kRetryMs;
    return false;
  }
  if (d.failures > 0) {
    d.write(std::format("audio: reopened after {} failed attempts", d.failures));
  }
  d.failures = 0;
  d.removed = false;
  d.progress_at.reset();
  d.write(d.describe());
  return true;
}

bool AudioDevice::is_open() const { return impl_->stream != nullptr; }

std::uint32_t AudioDevice::device_id() const { return impl_->stream_device.load(); }

std::uint64_t AudioDevice::frames_fed() const { return impl_->fed.load(std::memory_order_relaxed); }

AudioDevice::Lock::Lock(AudioDevice& device) : stream_(device.impl_->stream) {
  // Without an output no audio thread renders.
  if (stream_ != nullptr) {
    SDL_LockAudioStream(stream_);
  }
}

AudioDevice::Lock::~Lock() {
  if (stream_ != nullptr) {
    SDL_UnlockAudioStream(stream_);
  }
}

}  // namespace sh
