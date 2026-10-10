#include "audio/mel_layer.h"

#include <algorithm>
#include <utility>

#include "assets/speech.h"

namespace sh {

namespace {

// mel_clamp (0xb399c).
std::int32_t mel_clamp(std::int32_t v, std::int32_t lo, std::int32_t hi) { return std::clamp(v, lo, hi); }

// rand_range (0xb5cb8): `rand() % max(1, max - min) + min`.
std::int32_t rand_range(const RandFn& rand, std::int32_t max, std::int32_t min) {
  const std::int32_t span = std::max(max - min, 1);
  return rand() % span + min;
}

// Returns the left and right weights (0..64) of AIL pan 0..127: full on both
// sides at the centre, fading out the opposite side towards either end.
std::pair<std::int32_t, std::int32_t> pan_weights(std::int32_t pan) {
  const std::int32_t left = pan <= 64 ? 64 : (127 - pan) * 64 / 63;
  const std::int32_t right = pan >= 64 ? 64 : pan;
  return {left, right};
}

}  // namespace

MelLayer::MelLayer(SampleTable& samples, RandFn rand, std::int32_t output_rate, int channels)
    : samples_(samples), rand_(std::move(rand)), output_rate_(output_rate),
      channels_(static_cast<std::size_t>(channels)) {}

void MelLayer::set_mute_mode(std::int32_t mode) { mute_mode_ = static_cast<std::uint8_t>(mel_clamp(mode, 0, 7)); }

void MelLayer::set_sfx_volume(std::int32_t volume) { sfx_volume_ = mel_clamp(volume, 0, 100); }

void MelLayer::set_speech_volume(std::int32_t volume) {
  speech_volume_ = mel_clamp(volume, 0, 100);
  speech_.state.volume = speech_volume_;
}

bool MelLayer::sfx_available() const { return (mute_mode_ & kMuteEffects) == 0; }

bool MelLayer::speech_available() const { return (mute_mode_ & kMuteSpeech) == 0; }

std::int32_t MelLayer::effect_volume(std::int32_t requested, std::int32_t offset) const {
  return std::max(std::min(requested, 100) - (100 - sfx_volume_) + offset, 10);
}

void MelLayer::set_rate(Voice& v, std::int32_t rate) const {
  v.state.rate = rate;
  v.step = static_cast<std::uint32_t>((static_cast<std::uint64_t>(rate) << 16) /
                                      static_cast<std::uint64_t>(output_rate_));
}

// sfx_play_on_channel (0xb602b).
std::int32_t MelLayer::play_sfx(std::int32_t id, std::int32_t volume, std::int32_t pan) {
  if (!sfx_available()) {
    return 0;
  }
  if (volume != -1) {
    volume = mel_clamp(volume, 0, 100);
  }
  if (pan != -1) {
    pan = mel_clamp(pan, 0, 127);
  }
  const MelSample* sample = samples_.find_or_load(id);
  if (sample == nullptr) {
    return 0;
  }
  const MelHeader& h = sample->header;
  // The random rate is drawn before looking for a channel, so a play that
  // finds no free channel still consumes a rand() value.
  const std::int32_t random_rate = h.random_rate() ? rand_range(rand_, h.rate_max, h.rate_min) : 0;
  const auto free_it = std::ranges::find_if(channels_, [](const Voice& v) { return !v.state.playing; });
  if (free_it == channels_.end()) {
    return 0;
  }
  for (Voice& v : channels_) {
    if (!v.state.playing) {
      v.state.sample_id = -1;
    }
  }
  Voice& v = *free_it;
  v.pcm = sample->pcm;
  v.pos = 0;
  v.state.sample_id = id;
  v.state.playing = !v.pcm.empty();
  v.state.volume = effect_volume(volume == -1 ? 100 : volume, h.volume_offset);
  v.state.pan = pan == -1 ? kCenterPan : pan;
  v.state.loop_count = h.loop_count;
  set_rate(v, random_rate != 0 ? random_rate : h.rate);
  return static_cast<std::int32_t>(free_it - channels_.begin()) + 1;
}

void MelLayer::preload_sfx(std::int32_t id) {
  if (sfx_available()) {
    samples_.find_or_load(id);
  }
}

int MelLayer::find_channel(std::int32_t ref) const {
  if (ref < 0) {
    const std::int32_t index = -1 - ref;
    return index < channel_count() ? index : -1;
  }
  for (int i = 0; i < channel_count(); ++i) {
    if (channels_[static_cast<std::size_t>(i)].state.sample_id == ref) {
      return i;
    }
  }
  return -1;
}

// 0xb5edf.
bool MelLayer::stop_sfx(std::int32_t ref) {
  if (!sfx_available()) {
    return false;
  }
  const int index = find_channel(ref);
  if (index < 0) {
    return false;
  }
  ChannelState& s = channels_[static_cast<std::size_t>(index)].state;
  const bool was_playing = s.playing;
  s.playing = false;
  return was_playing;
}

// 0xb5d18.
bool MelLayer::set_sfx_volume_pan(std::int32_t ref, std::int32_t volume, std::int32_t pan) {
  if (!sfx_available()) {
    return false;
  }
  volume = mel_clamp(volume, -1, 100);
  pan = mel_clamp(pan, -1, 127);
  int index = -1;
  if (ref < 0) {
    index = find_channel(ref);
  } else if (cached_channel_ >= 0 && channels_[static_cast<std::size_t>(cached_channel_)].state.sample_id == ref) {
    // The original keeps the last channel found for an id even when an
    // earlier channel holds the same id.
    index = cached_channel_;
  } else {
    index = find_channel(ref);
    if (index >= 0) {
      cached_channel_ = index;
    }
  }
  if (index < 0) {
    return false;
  }
  ChannelState& s = channels_[static_cast<std::size_t>(index)].state;
  if (!s.playing) {
    return false;
  }
  if (pan != -1) {
    s.pan = pan;
  }
  if (volume != -1) {
    s.volume = effect_volume(volume, 0);
  }
  return true;
}

// 0xb5f8f.
bool MelLayer::sfx_is_playing(std::int32_t ref) const {
  if (!sfx_available()) {
    return false;
  }
  const int index = find_channel(ref);
  return index >= 0 && channels_[static_cast<std::size_t>(index)].state.playing;
}

// 0xb5348: the original streams the file through two 10000-byte buffers
// refilled by speech_service each frame; the whole clip is kept here.
bool MelLayer::speech_start(std::vector<std::uint8_t> pcm) {
  if (!speech_available()) {
    return false;
  }
  speech_pcm_ = std::move(pcm);
  speech_.pcm = speech_pcm_;
  speech_.pos = 0;
  speech_.state = ChannelState{.sample_id = -1,
                               .playing = !speech_pcm_.empty(),
                               .rate = 0,
                               .volume = speech_volume_,
                               .pan = kCenterPan,
                               .loop_count = 1};
  set_rate(speech_, kSpeechRate);
  return true;
}

bool MelLayer::speech_stop() {
  if (!speech_available()) {
    return false;
  }
  speech_.state.playing = false;
  return true;
}

bool MelLayer::speech_is_playing() const { return speech_available() && speech_.state.playing; }

MelLayer::ChannelState MelLayer::channel(int index) const {
  return index == channel_count() ? speech_.state : channels_.at(static_cast<std::size_t>(index)).state;
}

void MelLayer::render(std::span<std::int16_t> out) {
  const std::size_t frames = out.size() / 2;
  mix_.assign(frames * 2, 0);
  const auto mix_voice = [&](Voice& v) {
    if (!v.state.playing) {
      return;
    }
    const std::uint64_t length = static_cast<std::uint64_t>(v.pcm.size()) << 16;
    const std::int32_t volume = mel_clamp(v.state.volume, 0, kAilMaxVolume);
    const auto [left_w, right_w] = pan_weights(v.state.pan);
    // Full scale: a u8 sample at volume 127 and weight 64 maps to 16 bits.
    const std::int64_t left_gain = static_cast<std::int64_t>(volume) * left_w * 256;
    const std::int64_t right_gain = static_cast<std::int64_t>(volume) * right_w * 256;
    constexpr std::int64_t kGainScale = static_cast<std::int64_t>(kAilMaxVolume) * 64;
    for (std::size_t f = 0; f < frames; ++f) {
      if (v.pos >= length) {
        // AIL loop count: 1 ends, n > 1 counts down, <= 0 loops forever.
        if (v.state.loop_count == 1) {
          v.state.playing = false;
          return;
        }
        if (v.state.loop_count > 1) {
          --v.state.loop_count;
        }
        v.pos %= length;
      }
      const std::int32_t s = static_cast<std::int32_t>(v.pcm[static_cast<std::size_t>(v.pos >> 16)]) - 128;
      mix_[2 * f] += static_cast<std::int32_t>(s * left_gain / kGainScale);
      mix_[2 * f + 1] += static_cast<std::int32_t>(s * right_gain / kGainScale);
      v.pos += v.step;
    }
    // A clip ending exactly at the buffer end is finished now, not at the
    // next render, so its status matches the original's end-of-data report.
    if (v.pos >= length && v.state.loop_count == 1) {
      v.state.playing = false;
    }
  };
  for (Voice& v : channels_) {
    mix_voice(v);
  }
  mix_voice(speech_);
  for (std::size_t i = 0; i < mix_.size(); ++i) {
    out[i] = static_cast<std::int16_t>(std::clamp(mix_[i], -32768, 32767));
  }
}

void apply_sound_options(MelLayer& layer, const SoundOptions& options) {
  layer.set_mute_mode(options.sound_effects ? 0 : kMuteEffects);
  layer.set_sfx_volume(options.effects_volume);
  layer.set_speech_volume(options.speech_volume);
}

bool play_speech_raw(MelLayer& layer, const Install& install, std::string_view file_name, std::string_view speech_dir) {
  if (!layer.speech_available()) {
    return false;
  }
  return start_speech_raw(layer, read_speech_raw(install, file_name, speech_dir));
}

bool start_speech_raw(MelLayer& layer, std::optional<std::vector<std::uint8_t>> pcm) {
  if (!layer.speech_available()) {
    return false;
  }
  if (!pcm) {
    // speech_open closes the current clip before failing to open the file.
    layer.speech_stop();
    return false;
  }
  return layer.speech_start(std::move(*pcm));
}

bool play_crew_speech(MelLayer& layer, const Install& install, const SoundOptions& options, std::string_view sound,
                      std::string_view speech_dir) {
  if (!options.speech || sound.empty()) {
    return false;
  }
  return play_speech_raw(layer, install, speech_file_name(sound), speech_dir);
}

}  // namespace sh
