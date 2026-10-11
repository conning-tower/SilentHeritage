#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include "assets/mel.h"
#include "assets/speech.h"
#include "audio/mel_layer.h"
#include "audio/sample_table.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "game_data.h"

namespace sh {

namespace {

MelSample synthetic(std::vector<std::uint8_t> pcm, std::int32_t rate, std::int32_t loop_count = 1,
                    std::int32_t volume_offset = 0) {
  return MelSample{MelHeader{.rate = rate, .loop_count = loop_count, .volume_offset = volume_offset}, std::move(pcm)};
}

// The mixer's output for signed sample `s` at AIL volume `volume` and pan
// weight `weight` (0..64).
std::int16_t level(std::int32_t s, std::int32_t volume, std::int32_t weight = 64) {
  return static_cast<std::int16_t>(static_cast<std::int64_t>(s) * volume * weight * 256 / (127 * 64));
}

std::vector<std::int16_t> render(MelLayer& layer, std::size_t frames) {
  std::vector<std::int16_t> out(frames * 2);
  layer.render(out);
  return out;
}

RandFn fixed_rand(std::int32_t value, int* calls = nullptr) {
  return [value, calls] {
    if (calls != nullptr) {
      ++*calls;
    }
    return value;
  };
}

}  // namespace

TEST_CASE("MEL files parse to the reference values", "[mel][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto files = install.list("SFX", ".MEL");
  CHECK(files.size() == 54);
  for (const auto& f : files) {
    const MelSample s = parse_mel(install.read(f), f);
    CHECK(s.pcm.size() + kMelHeaderSize == install.read(f).size());
  }

  const MelSample click = parse_mel(install.read("SFX/1.MEL"));
  CHECK(click.header.rate == 22050);
  CHECK(click.header.loop_count == 1);
  CHECK(click.pcm.size() == 526);
  CHECK(click.pcm[0] == 141);
  CHECK(click.pcm.back() == 105);

  const MelSample splash = parse_mel(install.read("SFX/27.MEL"));
  CHECK(splash.header.random_rate());
  CHECK(splash.header.rate_min == 14000);
  CHECK(splash.header.rate_max == 21000);
  CHECK(splash.header.nominal_rate() == 17500);
  CHECK(splash.pcm.size() == 32920);

  const MelSample engine = parse_mel(install.read("SFX/70.MEL"));
  CHECK(engine.header.rate == 11025);
  CHECK(engine.header.loop_count == -1);
  CHECK(engine.header.volume_offset == -20);
  CHECK(engine.pcm.size() == 27944);

  const MelSample contact = parse_mel(install.read("SFX/10.MEL"));
  CHECK(contact.header.rate == 12000);
  CHECK(contact.header.loop_count == 2);
  CHECK(contact.pcm.size() == 24643);
}

TEST_CASE("BNK preload lists parse to the reference values", "[mel][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto bank1 = parse_bnk(install.read("SFX/1.BNK"));
  const std::vector<std::int32_t> ids1 = {1, 2, 50, 51, 52, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 90, 91, 92, 93};
  REQUIRE(bank1.size() == ids1.size());
  for (std::size_t i = 0; i < ids1.size(); ++i) {
    CHECK(bank1[i].sample_id == ids1[i]);
    CHECK(bank1[i].size == 0);
    CHECK(bank1[i].pointer == 0);
  }
  CHECK(bank1[5].header.rate == 11025);
  CHECK(bank1[5].header.loop_count == -1);
  CHECK(bank1[5].header.volume_offset == -20);

  const auto bank2 = parse_bnk(install.read("SFX/2.BNK"));
  REQUIRE(bank2.size() == 33);
  CHECK(bank2.front().sample_id == 3);
  CHECK(bank2.back().sample_id == 40);
  CHECK(bank2.back().header.rate == 30000);  // Stale: 40.MEL says 22050.
}

TEST_CASE("MEL and BNK parsers reject malformed data", "[mel]") {
  std::vector<std::uint8_t> mel(kMelHeaderSize, 0);
  CHECK_THROWS_AS(parse_mel(mel), DataError);  // Rate 0.
  mel[4] = 0xff, mel[5] = 0xff, mel[6] = 0xff, mel[7] = 0xff;  // Random rate, empty range.
  CHECK_THROWS_AS(parse_mel(mel), DataError);
  CHECK_THROWS_AS(parse_mel(std::vector<std::uint8_t>(10, 0)), DataError);
  CHECK_THROWS_AS(parse_bnk(std::vector<std::uint8_t>(0x25, 0)), DataError);
  std::vector<std::uint8_t> early_end(2 * kBankEntrySize, 0);
  early_end[0] = early_end[1] = early_end[2] = early_end[3] = 0xff;
  CHECK_THROWS_AS(parse_bnk(early_end), DataError);
  CHECK(mel_path(70) == "SFX/70.MEL");
  CHECK(bnk_path(1) == "SFX/1.BNK");
}

TEST_CASE("Speech RAW files are found in SPEECH or ALT", "[audio]") {
  SH_REQUIRE_INSTALL(install);
  CHECK(speech_file_name("CAPT0425") == "CAPT0425.RAW");
  const auto capt = read_speech_raw(install, "CAPT0425.RAW");
  REQUIRE(capt.has_value());
  CHECK(capt->size() == 23552);
  // A missing speech directory falls back to ALT\ (play_speech_raw).
  const auto radio = read_speech_raw(install, "RADIO.RAW", "NOPE/");
  REQUIRE(radio.has_value());
  CHECK(radio->size() == 37057);
  CHECK_FALSE(read_speech_raw(install, "CAPT0425.RAW", "NOPE/").has_value());
  CHECK_FALSE(read_speech_raw(install, "NOSUCH.RAW").has_value());
}

TEST_CASE("The sample table preloads bank 1 and loads other effects on demand", "[audio]") {
  SH_REQUIRE_INSTALL(install);
  SampleTable table(&install);
  table.load_bank(1);
  CHECK(table.size() == 19);
  CHECK(table.contains(93));
  CHECK_FALSE(table.contains(40));
  const MelSample* gun = table.find_or_load(40);
  REQUIRE(gun != nullptr);
  CHECK(gun->header.rate == 22050);  // The .MEL, not 2.BNK's stale copy.
  CHECK(table.size() == 20);
  CHECK(table.find_or_load(999) == nullptr);
}

TEST_CASE("Effects mix with the original volume and pan rules", "[audio]") {
  SampleTable table(nullptr);
  table.add(1, synthetic(std::vector<std::uint8_t>(64, 228), 22050));  // Signed +100.
  table.add(2, synthetic(std::vector<std::uint8_t>(64, 228), 22050, 1, -20));
  MelLayer layer(table, fixed_rand(0));

  SECTION("default volume and centre pan") {
    CHECK(layer.play_sfx(1) == 1);
    CHECK(layer.channel(0).volume == 100);
    CHECK(layer.channel(0).pan == 64);
    const auto out = render(layer, 4);
    CHECK(out[0] == level(100, 100));
    CHECK(out[1] == level(100, 100));
  }
  SECTION("master volume and offset, floored at 10") {
    layer.set_sfx_volume(50);
    layer.play_sfx(2, 100);
    CHECK(layer.channel(0).volume == 30);
    layer.set_sfx_volume(0);
    layer.play_sfx(1, 40);
    CHECK(layer.channel(1).volume == 10);
    layer.set_sfx_volume(150);
    CHECK(layer.sfx_volume() == 100);
  }
  SECTION("hard pans") {
    layer.play_sfx(1, 100, 0);
    auto out = render(layer, 1);
    CHECK(out[0] == level(100, 100));
    CHECK(out[1] == 0);
    layer.stop_sfx(1);
    layer.play_sfx(1, 100, 127);
    out = render(layer, 1);
    CHECK(out[0] == 0);
    CHECK(out[1] == level(100, 100));
  }
  SECTION("volume/pan updates skip the sample's offset") {
    layer.play_sfx(2, 100);
    CHECK(layer.channel(0).volume == 80);
    CHECK(layer.set_sfx_volume_pan(2, 100, 10));
    CHECK(layer.channel(0).volume == 100);
    CHECK(layer.channel(0).pan == 10);
    CHECK(layer.set_sfx_volume_pan(-1, -1, 20));  // By handle; volume kept.
    CHECK(layer.channel(0).volume == 100);
    CHECK(layer.channel(0).pan == 20);
    CHECK_FALSE(layer.set_sfx_volume_pan(1, 50, 50));  // Not playing.
  }
  SECTION("voices sum and clip") {
    for (int i = 0; i < 3; ++i) {
      layer.play_sfx(1);
    }
    const auto out = render(layer, 1);
    CHECK(out[0] == 32767);
  }
}

TEST_CASE("Effects are resampled to the output rate and loop", "[audio]") {
  SampleTable table(nullptr);
  table.add(1, synthetic({129, 130, 131, 132}, 11025));
  table.add(2, synthetic({129, 130}, 22050, 2));
  table.add(3, synthetic({129, 130}, 22050, -1));
  MelLayer layer(table, fixed_rand(0));

  SECTION("half rate repeats each sample") {
    layer.play_sfx(1);
    const auto out = render(layer, 10);
    const std::array<std::int16_t, 10> left = {level(1, 100), level(1, 100), level(2, 100), level(2, 100),
                                              level(3, 100), level(3, 100), level(4, 100), level(4, 100), 0, 0};
    for (std::size_t i = 0; i < left.size(); ++i) {
      CHECK(out[2 * i] == left[i]);
    }
    CHECK_FALSE(layer.sfx_is_playing(1));
  }
  SECTION("double output rate") {
    MelLayer fast(table, fixed_rand(0), 44100);
    fast.play_sfx(2);
    const auto out = render(fast, 10);
    CHECK(out[0] == level(1, 100));
    CHECK(out[2] == level(1, 100));
    CHECK(out[4] == level(2, 100));
    CHECK(out[14] == level(2, 100));
    CHECK(out[16] == 0);
  }
  SECTION("loop count 2 plays twice") {
    layer.play_sfx(2);
    const auto out = render(layer, 6);
    CHECK(out[6] == level(2, 100));
    CHECK(out[8] == 0);
    CHECK_FALSE(layer.sfx_is_playing(2));
  }
  SECTION("loop count -1 loops until stopped") {
    const std::int32_t handle = layer.play_sfx(3);
    render(layer, 1001);
    CHECK(layer.sfx_is_playing(3));
    CHECK(render(layer, 1)[0] == level(2, 100));
    CHECK(layer.stop_sfx(-handle));
    CHECK_FALSE(layer.sfx_is_playing(3));
    CHECK(render(layer, 1)[0] == 0);
    CHECK_FALSE(layer.stop_sfx(-handle));
  }
}

TEST_CASE("Random-rate effects draw rand() before looking for a channel", "[audio]") {
  SampleTable table(nullptr);
  MelSample s = synthetic(std::vector<std::uint8_t>(1000, 128), 22050, -1);
  s.header.rate = kMelRandomRate;
  s.header.rate_min = 14000;
  s.header.rate_max = 21000;
  table.add(27, std::move(s));
  int calls = 0;
  MelLayer layer(table, fixed_rand(12345, &calls), 22050, 2);
  CHECK(layer.play_sfx(27) == 1);
  CHECK(layer.channel(0).rate == 12345 % 7000 + 14000);
  CHECK(layer.play_sfx(27) == 2);
  CHECK(layer.play_sfx(27) == 0);  // All channels busy: no stealing.
  CHECK(calls == 3);
}

TEST_CASE("Channels are taken lowest free first and referenced by id or handle", "[audio]") {
  SampleTable table(nullptr);
  for (std::int32_t id = 1; id <= 13; ++id) {
    table.add(id, synthetic(std::vector<std::uint8_t>(100, 128), 22050, -1));
  }
  table.add(50, synthetic({128}, 22050));
  MelLayer layer(table, fixed_rand(0));
  for (std::int32_t id = 1; id <= 12; ++id) {
    CHECK(layer.play_sfx(id) == id);
  }
  CHECK(layer.play_sfx(13) == 0);
  CHECK(layer.play_sfx(999) == 0);  // Unknown sample.
  CHECK(layer.stop_sfx(5));
  CHECK(layer.stop_sfx(-8));  // Handle 8 = channel 7.
  CHECK_FALSE(layer.sfx_is_playing(8));
  CHECK(layer.channel(4).sample_id == 5);  // Kept until the next play.
  CHECK(layer.play_sfx(13) == 5);
  CHECK(layer.channel(7).sample_id == -1);  // Finished channels forget their id.
  CHECK(layer.play_sfx(50) == 8);
  CHECK_FALSE(layer.stop_sfx(-100));
}

TEST_CASE("Mute mode gates effects and speech separately", "[audio]") {
  SampleTable table(nullptr);
  table.add(1, synthetic({200, 200}, 22050));
  MelLayer layer(table, fixed_rand(0));

  apply_sound_options(layer, SoundOptions{.sound_effects = false, .effects_volume = 70, .speech_volume = 60});
  CHECK(layer.mute_mode() == kMuteEffects);
  CHECK(layer.sfx_volume() == 70);
  CHECK(layer.speech_volume() == 60);
  CHECK(layer.play_sfx(1) == 0);
  CHECK(layer.speech_start({200, 200}));
  CHECK(layer.channel(layer.channel_count()).volume == 60);

  layer.set_mute_mode(kMuteSpeech);
  CHECK_FALSE(layer.speech_start({200}));
  CHECK_FALSE(layer.speech_stop());
  CHECK(layer.play_sfx(1) == 1);
  layer.set_mute_mode(9);
  CHECK(layer.mute_mode() == 7);
}

TEST_CASE("Speech plays one clip at 22050 Hz, replacing the previous one", "[audio]") {
  SampleTable table(nullptr);
  MelLayer layer(table, fixed_rand(0), 44100);
  layer.set_speech_volume(50);
  CHECK(layer.speech_start({138, 148}));  // +10, +20.
  CHECK(layer.speech_is_playing());
  auto out = render(layer, 2);
  CHECK(out[0] == level(10, 50));
  CHECK(out[2] == level(10, 50));
  CHECK(layer.speech_start({118}));  // -10.
  out = render(layer, 3);
  CHECK(out[0] == level(-10, 50));
  CHECK(out[3] == level(-10, 50));
  CHECK(out[4] == 0);
  CHECK_FALSE(layer.speech_is_playing());
  CHECK(layer.speech_start({138, 148}));
  CHECK(layer.speech_stop());
  CHECK_FALSE(layer.speech_is_playing());
}

TEST_CASE("Crew speech plays the message's RAW clip", "[audio]") {
  SH_REQUIRE_INSTALL(install);
  SampleTable table(&install);
  MelLayer layer(table, fixed_rand(0));
  CHECK(play_crew_speech(layer, install, SoundOptions{}, "2DEEP"));
  render(layer, 20111);
  CHECK(layer.speech_is_playing());
  render(layer, 1);
  CHECK_FALSE(layer.speech_is_playing());

  CHECK_FALSE(play_crew_speech(layer, install, SoundOptions{.speech = false}, "2DEEP"));
  CHECK_FALSE(play_crew_speech(layer, install, SoundOptions{}, ""));
  CHECK(play_speech_raw(layer, install, "CAPT0425.RAW"));
  CHECK_FALSE(play_speech_raw(layer, install, "NOSUCH.RAW"));
  CHECK_FALSE(layer.speech_is_playing());  // The failed open stopped the clip.
}

}  // namespace sh
