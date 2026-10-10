#include "game/station_env.h"

#include <algorithm>
#include <mutex>

#include "assets/pcx.h"
#include "assets/speech.h"

namespace sh {

StationAssets StationAssets::load(const Install& install) {
  return {Shp::load(install, "ART/CURSORS.SHP"), Shp::load(install, "ART/GLOBAL.SHP"),
          Font::load(install, "ART/BEARING.FNT"), Font::load(install, "ART/DIGITS.FNT"),
          Font::load(install, "ART/DIGITS2.FNT"), Font::load(install, "ART/FONT1.FNT"),
          Font::load(install, "ART/MSG.FNT")};
}

void load_screen_pcx(StationEnv& env, std::string_view art_file) {
  const PcxImage pcx = load_pcx(env.install, env.paths.path(GameDir::kArt, art_file));
  env.screen().blit(pcx.image, 0, 0);
  env.palette = pcx.palette;
}

void load_screen_palette(StationEnv& env, std::string_view art_file) {
  env.palette = load_pcx(env.install, env.paths.path(GameDir::kArt, art_file)).palette;
}

template <class Fn>
void SoundPort::with_layer(Fn&& fn) const {
  if (layer_ == nullptr) {
    return;
  }
  if (lock_ != nullptr) {
    const std::lock_guard<SoundLock> guard(*lock_);
    fn(*layer_);
  } else {
    fn(*layer_);
  }
}

bool SoundPort::sfx_playing(std::int32_t sfx) const {
  bool playing = false;
  with_layer([&](MelLayer& layer) { playing = layer.sfx_is_playing(sfx); });
  return playing;
}

bool SoundPort::speech_playing() const {
  bool playing = false;
  with_layer([&](MelLayer& layer) { playing = layer.speech_is_playing(); });
  return playing;
}

void SoundPort::play_sfx(std::int32_t sfx, std::int32_t volume, std::int32_t pan) {
  if (layer_ != nullptr) {
    layer_->preload_sfx(sfx);
  }
  with_layer([&](MelLayer& layer) { layer.play_sfx(sfx, volume, pan); });
}

void SoundPort::stop_sfx(std::int32_t sfx) {
  with_layer([&](MelLayer& layer) { layer.stop_sfx(sfx); });
}

void SoundPort::stop_all() {
  with_layer([](MelLayer& layer) {
    for (int c = 0; c < layer.channel_count(); ++c) {
      layer.stop_sfx(-1 - c);
    }
    layer.speech_stop();
  });
}

SoundOptions SoundPort::sound_options() const {
  return {options_->sound_effects != 0, options_->engine_sounds != 0, options_->speech != 0,
          options_->effects_volume, options_->speech_volume};
}

void SoundPort::apply_options() {
  with_layer([&](MelLayer& layer) { apply_sound_options(layer, sound_options()); });
}

void SoundPort::play_speech(std::string_view file_name) {
  if (options_->speech != 0) {
    // The file is read before holding off the audio thread.
    std::optional<std::vector<std::uint8_t>> pcm = read_speech_raw(*install_, file_name, paths_->dir(GameDir::kSpeech));
    with_layer([&](MelLayer& layer) { start_speech_raw(layer, std::move(pcm)); });
  }
}

void SoundPort::play_crew_speech(std::string_view sound) {
  if (layer_ == nullptr || options_->speech == 0 || sound.empty()) {
    return;
  }
  std::optional<std::vector<std::uint8_t>> pcm =
      read_speech_raw(*install_, speech_file_name(sound), paths_->dir(GameDir::kSpeech));
  with_layer([&](MelLayer& layer) { start_speech_raw(layer, std::move(pcm)); });
}

namespace {

// Returns whether every entry but the GUI colours 1..8 is the blue of
// `palette_fade_to_blue`.
bool all_blue(const VgaPalette& dac) {
  for (std::size_t e = 0; e < 256; ++e) {
    if ((e < 1 || e > 8) && (dac[3 * e] != 0 || dac[3 * e + 1] != 0 || dac[3 * e + 2] != 30)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool Dac::finished(const Op& op) const {
  switch (op.fade) {
    case Fade::kOut:
      return std::all_of(dac_.begin(), dac_.end(), [](std::uint8_t c) { return c == 0; });
    case Fade::kIn:
      return dac_ == op.target;
    case Fade::kBlue:
      return all_blue(dac_);
  }
  return true;
}

void Dac::step() {
  // The original loops step / wait for the retrace until a step changes
  // nothing; the step that would change nothing takes no retrace here.
  while (!fades_.empty() && finished(fades_.front())) {
    fades_.pop_front();
  }
  if (fades_.empty()) {
    return;
  }
  const Op& op = fades_.front();
  switch (op.fade) {
    case Fade::kOut:
      // palette_fade_out (0x58d09)
      for (std::uint8_t& c : dac_) {
        c = static_cast<std::uint8_t>(std::max(0, c - op.step));
      }
      break;
    case Fade::kIn:
      // palette_fade_in (0x58bd0): the components move together per entry.
      for (std::size_t i = 0; i < dac_.size(); i += 3) {
        if (dac_[i] == op.target[i] && dac_[i + 1] == op.target[i + 1] && dac_[i + 2] == op.target[i + 2]) {
          continue;
        }
        for (std::size_t k = i; k < i + 3; ++k) {
          dac_[k] = static_cast<std::uint8_t>(std::min<std::int32_t>(dac_[k] + op.step, op.target[k]));
        }
      }
      break;
    case Fade::kBlue:
      // palette_fade_to_blue (0x58f28): entries 1..8 (the GUI colours) stay.
      for (std::size_t e = 0; e < 256; ++e) {
        std::uint8_t* rgb = &dac_[3 * e];
        if ((e >= 1 && e <= 8) || (rgb[2] == 30 && rgb[1] == 0 && rgb[0] == 0)) {
          continue;
        }
        rgb[2] = static_cast<std::uint8_t>(std::min(rgb[2] + op.step, 30));
        rgb[1] = static_cast<std::uint8_t>(std::max(rgb[1] - op.step, 0));
        rgb[0] = static_cast<std::uint8_t>(std::max(rgb[0] - op.step, 0));
      }
      break;
  }
  while (!fades_.empty() && finished(fades_.front())) {
    fades_.pop_front();
  }
}

void Dac::grey_out(std::int32_t percent, std::int32_t last) {
  // palette_grey_out (0x58e29)
  for (std::int32_t e = 9; e <= last; ++e) {
    const auto i = static_cast<std::size_t>(3 * e);
    std::copy_n(&dac_[i], 3, &saved_[i]);
    const std::uint8_t r = dac_[i];
    const std::uint8_t g = dac_[i + 1];
    const std::uint8_t b = dac_[i + 2];
    const std::int32_t hi = std::max({r, g, b});
    const std::int32_t lo = std::min({r, g, b});
    const std::int32_t grey = std::max(0, percent * (hi - (hi - lo) / 2) / 100);
    std::fill_n(&dac_[i], 3, static_cast<std::uint8_t>(grey));
  }
}

void Dac::restore_saved(std::int32_t last) {
  // palette_restore_saved (0x58ef1)
  for (std::int32_t e = 9; e <= last; ++e) {
    const auto i = static_cast<std::size_t>(3 * e);
    std::copy_n(&saved_[i], 3, &dac_[i]);
  }
}

}  // namespace sh
