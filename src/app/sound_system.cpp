#include "app/sound_system.h"

#include <cstdio>
#include <stdexcept>
#include <utility>

#include "core/edition.h"

namespace sh {

SoundSystem::SoundSystem(const Install& install, const ShPaths& paths)
    : samples_(&install, paths.dir(GameDir::kSfx)), layer_(samples_, [this] { return random_.rand(); }) {
  try {
    device_ = std::make_unique<AudioDevice>(layer_);
  } catch (const std::runtime_error& e) {
    std::fprintf(stderr, "%s: no sound: %s\n", edition::kExecutable, e.what());
  }
}

SoundSystem::~SoundSystem() = default;

void SoundSystem::with_layer(const std::function<void(MelLayer&)>& fn) {
  if (device_) {
    const AudioDevice::Lock lock(*device_);
    fn(layer_);
  } else {
    fn(layer_);
  }
}

void SoundSystem::play_sfx(std::int32_t id, std::int32_t volume, std::int32_t pan) {
  layer_.preload_sfx(id);
  with_layer([&](MelLayer& layer) { layer.play_sfx(id, volume, pan); });
}

bool SoundSystem::start_speech(std::optional<std::vector<std::uint8_t>> pcm) {
  bool started = false;
  with_layer([&](MelLayer& layer) { started = start_speech_raw(layer, std::move(pcm)); });
  return started;
}

}  // namespace sh
