// The MEL sound layer: the original game's interface to its sound effects and
// speech (api/src/API.c over Miles AIL 3), re-created with our own
// deterministic software mixer. The SDL device (audio/audio_device.h) only
// pulls rendered frames from it.
//
// Effects play on a fixed set of channels (AIL sample handles; the game
// allocates 12). A new effect takes the first finished channel; when all are
// busy it is not played (there is no channel stealing). Speech is one
// separate voice: starting a clip replaces the current one.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "audio/sample_table.h"
#include "core/install.h"

namespace sh {

// Bits of the mute mode (mel_set_mute_mode 0xb2fbe).
enum MuteBits : std::uint8_t {
  kMuteMidi = 1,
  kMuteEffects = 2,
  kMuteSpeech = 4,
};

// Returns the next value of the game's C `rand()` (0..32767).
using RandFn = std::function<std::int32_t()>;

class MelLayer {
 public:
  // Effect channels allocated by init_sound (mel_set_sfx_channels(12)).
  static constexpr int kDefaultSfxChannels = 12;
  // The hardware rate init_sound selects (mel_set_hw_sample_rate).
  static constexpr std::int32_t kDefaultOutputRate = 22050;
  // AIL's maximum sample volume; the game passes 10..100 on this scale.
  static constexpr std::int32_t kAilMaxVolume = 127;
  static constexpr std::int32_t kCenterPan = 64;

  // A finished effect channel or the speech voice as seen by `channel()`.
  struct ChannelState {
    std::int32_t sample_id = -1;  // -1 once reset by a later play.
    bool playing = false;
    std::int32_t rate = 0;        // Hz.
    std::int32_t volume = 0;      // AIL scale 0..127.
    std::int32_t pan = kCenterPan;  // 0 left .. 127 right.
    std::int32_t loop_count = 1;  // Remaining AIL loop count (<= 0 forever).
  };

  // A layer drawing effects from `samples` (which must outlive it), mixing
  // `channels` effect channels plus the speech voice into stereo frames at
  // `output_rate` Hz; `rand` draws the random effect rates.
  MelLayer(SampleTable& samples, RandFn rand, std::int32_t output_rate = kDefaultOutputRate,
           int channels = kDefaultSfxChannels);

  std::int32_t output_rate() const { return output_rate_; }
  int channel_count() const { return static_cast<int>(channels_.size()); }

  // mel_set_mute_mode (0xb2fbe): `mode` is a `MuteBits` mask, clamped to 0..7.
  void set_mute_mode(std::int32_t mode);
  std::uint8_t mute_mode() const { return mute_mode_; }
  // Master effects volume 0..100 (clamped), applied when effects start or
  // change volume (mel_set_sfx_volume 0xb2eae).
  void set_sfx_volume(std::int32_t volume);
  std::int32_t sfx_volume() const { return sfx_volume_; }
  // Speech volume 0..100 (clamped), applied to the speech voice at once
  // (mel_set_speech_volume 0xb2eed).
  void set_speech_volume(std::int32_t volume);
  std::int32_t speech_volume() const { return speech_volume_; }

  // Returns whether the effect / speech entry points work under the mute
  // mode (mel_device_available 0xb37e2 kinds 4 and 8). When they do not,
  // they return false/0 and do nothing; voices already playing continue.
  bool sfx_available() const;
  bool speech_available() const;

  // sound_play_sfx (0xb2bbe): starts effect `id` with `volume` 0..100 (-1 =
  // 100) and `pan` 0..127 (-1 = centre). Returns its handle (channel + 1),
  // or 0 if effects are unavailable, the sample does not exist or no channel
  // is free.
  std::int32_t play_sfx(std::int32_t id, std::int32_t volume = -1, std::int32_t pan = -1);
  // Loads effect `id` into the sample table when effects are available, so
  // that a `play_sfx` of it reads no file. It touches no voice: a caller
  // calls it without holding off the audio thread (on the thread playing
  // effects), then `play_sfx` under the lock. Throws `DataError` as
  // `SampleTable::find_or_load`.
  void preload_sfx(std::int32_t id);

  // The functions below take a channel reference `ref`: a sample id (>= 0,
  // the first channel last started with it) or `-handle` (channel
  // -1 - ref), as the original API does.

  // sound_stop_sfx (0xb2b82): ends the channel. Returns true if it was playing.
  bool stop_sfx(std::int32_t ref);
  // sound_set_sfx_volume_pan (0xb2c37): changes the volume (0..100, -1 =
  // keep; the sample's volume offset is not applied here) and the pan
  // (0..127, -1 = keep) of a playing channel. Returns true if it was playing.
  bool set_sfx_volume_pan(std::int32_t ref, std::int32_t volume, std::int32_t pan);
  // sound_sfx_is_playing (0xb3068).
  bool sfx_is_playing(std::int32_t ref) const;

  // speech_open (0xb30a9): plays the speech clip `pcm` (u8 mono at
  // `kSpeechRate`) once, replacing the current one. Returns false (and plays
  // nothing) when speech is unavailable.
  bool speech_start(std::vector<std::uint8_t> pcm);
  // speech_stop (0xb3125): returns false only when speech is unavailable.
  bool speech_stop();
  // speech_is_playing (0xb31cf).
  bool speech_is_playing() const;

  // Returns the state of effect channel `index` (0-based), or of the speech
  // voice for `index == channel_count()`.
  ChannelState channel(int index) const;

  // Mixes the next `out.size() / 2` interleaved stereo frames (left, right)
  // into `out`, overwriting it, and advances every voice. `out.size()` must
  // be even.
  void render(std::span<std::int16_t> out);

 private:
  struct Voice {
    std::span<const std::uint8_t> pcm;
    ChannelState state;
    std::uint64_t pos = 0;   // 16.16 sample index.
    std::uint32_t step = 0;  // 16.16 samples per output frame.
  };

  // Returns the channel index of `ref`, or -1 when none matches.
  int find_channel(std::int32_t ref) const;
  void set_rate(Voice& v, std::int32_t rate) const;
  // Applies the master effects volume and the 10 floor of the original.
  std::int32_t effect_volume(std::int32_t requested, std::int32_t offset) const;

  SampleTable& samples_;
  RandFn rand_;
  std::int32_t output_rate_;
  std::vector<Voice> channels_;
  Voice speech_;
  std::vector<std::uint8_t> speech_pcm_;
  std::vector<std::int32_t> mix_;  // Render scratch buffer.
  std::uint8_t mute_mode_ = 0;
  std::int32_t sfx_volume_ = 100;
  std::int32_t speech_volume_ = 100;
  int cached_channel_ = -1;  // 0x113394, the last channel found by set_sfx_volume_pan.
};

// The sound settings of the GAME OPTIONS dialog (SAVE/GOPTION.SAV; defaults
// as goption_load 0x524cb).
struct SoundOptions {
  bool sound_effects = true;  // Unchecked: effects muted (speech still plays).
  bool engine_sounds = true;  // Gates player_update_engine_sounds.
  bool speech = true;         // Gates crew-message speech (crew_message).
  std::int32_t effects_volume = 100;
  std::int32_t speech_volume = 100;
};

// sound_apply_options (0x508d2): sets the mute mode (effects muted when
// sound effects are off) and both volumes.
void apply_sound_options(MelLayer& layer, const SoundOptions& options);

// play_speech_raw (0x10b8b): plays `file_name` (e.g. "2DEEP.RAW") from
// `speech_dir`, else from `ALT/`. Returns false if speech is unavailable or
// the file exists in neither directory.
bool play_speech_raw(MelLayer& layer, const Install& install, std::string_view file_name,
                     std::string_view speech_dir = "SPEECH/");
// The second half of `play_speech_raw`, for a caller that reads the clip
// with `read_speech_raw` before holding off the audio thread: plays `pcm`,
// or stops the current clip when `pcm` is empty (the file was not found).
// Returns whether a clip started.
bool start_speech_raw(MelLayer& layer, std::optional<std::vector<std::uint8_t>> pcm);

// The speech part of crew_message (0x4c843): plays "<sound>.RAW" when the
// speech option is on and the message has a sound name. Returns whether a
// clip started.
bool play_crew_speech(MelLayer& layer, const Install& install, const SoundOptions& options, std::string_view sound,
                      std::string_view speech_dir = "SPEECH/");

}  // namespace sh
