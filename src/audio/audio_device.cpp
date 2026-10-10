#include "audio/audio_device.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <vector>

namespace sh {

struct AudioDevice::Impl {
  MelLayer& layer;
  SDL_AudioStream* stream = nullptr;
  std::vector<std::int16_t> buffer;

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
  }
};

AudioDevice::AudioDevice(MelLayer& layer)
    : impl_(std::make_unique<Impl>(Impl{.layer = layer, .stream = nullptr, .buffer = {}})) {
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    throw std::runtime_error(std::format("SDL_InitSubSystem(AUDIO): {}", SDL_GetError()));
  }
  const SDL_AudioSpec spec{.format = SDL_AUDIO_S16, .channels = 2, .freq = layer.output_rate()};
  impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Impl::feed, impl_.get());
  if (impl_->stream == nullptr || !SDL_ResumeAudioStreamDevice(impl_->stream)) {
    const std::string error = SDL_GetError();
    SDL_DestroyAudioStream(impl_->stream);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    throw std::runtime_error(std::format("SDL_OpenAudioDeviceStream: {}", error));
  }
}

AudioDevice::~AudioDevice() {
  SDL_DestroyAudioStream(impl_->stream);
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

AudioDevice::Lock::Lock(AudioDevice& device) : device_(device) { SDL_LockAudioStream(device_.impl_->stream); }

AudioDevice::Lock::~Lock() { SDL_UnlockAudioStream(device_.impl_->stream); }

}  // namespace sh
