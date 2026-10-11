#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "assets/smacker.h"
#include "core/error.h"
#include "game_data.h"

namespace sh {
namespace {

// FNV-1a 64-bit, chained through `hash`.
std::uint64_t fnv1a(std::span<const std::uint8_t> bytes, std::uint64_t hash = 0xcbf29ce484222325) {
  for (const std::uint8_t b : bytes) {
    hash = (hash ^ b) * 0x100000001b3;
  }
  return hash;
}

// FNV-1a 64-bit over little-endian 64-bit words (`bytes` holds a multiple
// of 8 bytes), chained through `hash`.
std::uint64_t fnv1a_words(std::span<const std::uint8_t> bytes, std::uint64_t hash) {
  for (std::size_t i = 0; i < bytes.size(); i += 8) {
    std::uint64_t word = 0;
    for (std::size_t k = 0; k < 8; ++k) {
      word |= std::uint64_t{bytes[i + k]} << (8 * k);
    }
    hash = (hash ^ word) * 0x100000001b3;
  }
  return hash;
}

// Returns the palette as ffmpeg's `pal8` raw frames store it after the
// pixels: 256 little-endian 0xAARRGGBB words, opaque.
std::array<std::uint8_t, 1024> ffmpeg_palette(const Palette& palette) {
  std::array<std::uint8_t, 1024> out{};
  for (std::size_t i = 0; i < 256; ++i) {
    const Rgb& c = palette.colors[i];
    out[4 * i] = c.b;
    out[4 * i + 1] = c.g;
    out[4 * i + 2] = c.r;
    out[4 * i + 3] = 0xff;
  }
  return out;
}

// Returns the frame converted to RGB24 through its palette, as ffmpeg's
// `-pix_fmt rgb24` output.
std::vector<std::uint8_t> to_rgb24(const SmackerVideo& video) {
  std::vector<std::uint8_t> rgb;
  rgb.reserve(video.image().pixels().size() * 3);
  for (const std::uint8_t index : video.image().pixels()) {
    const Rgb& c = video.palette().colors[index];
    rgb.insert(rgb.end(), {c.r, c.g, c.b});
  }
  return rgb;
}

// Writes bit fields least significant bit first, as Smacker reads them.
class BitWriter {
 public:
  void put(std::uint32_t value, int count) {
    for (int i = 0; i < count; ++i, ++bits_) {
      if (bits_ % 8 == 0) {
        bytes_.push_back(0);
      }
      bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | ((value >> i) & 1) << (bits_ % 8));
    }
  }
  const std::vector<std::uint8_t>& bytes() const { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
  int bits_ = 0;
};

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  }
}

// Builds an 8x8, 2-frame SMK2 whose block-type tree always yields "solid
// color 5, longest run", with palette chunks in both frames and a raw 8-bit
// audio track (11025 Hz) with 4 samples in frame 1.
std::vector<std::uint8_t> synthetic_smk(std::uint32_t flags) {
  BitWriter trees;
  trees.put(0, 3);  // MMap, MClr and Full trees absent.
  trees.put(1, 1);  // Type tree present.
  for (const std::uint32_t byte : {0xffu, 0x05u}) {  // Low (type 3, run 63), high (color 5).
    trees.put(1, 1);
    trees.put(0, 1);
    trees.put(byte, 8);
    trees.put(0, 1);
  }
  trees.put(0x1111, 16);
  trees.put(0x2222, 16);
  trees.put(0x3333, 16);
  trees.put(0, 2);  // A single leaf, then the end bit.

  const std::vector<std::uint8_t> frame0 = {2, 0x3f, 0x00, 0x10, 0x83, 0x01, 0x02, 0x03};
  std::vector<std::uint8_t> frame1 = {1, 0x41, 0x05, 0xff};
  put_u32(frame1, 8);
  frame1.insert(frame1.end(), {0x80, 0x90, 0x70, 0x81});

  std::vector<std::uint8_t> smk = {'S', 'M', 'K', '2'};
  for (const std::uint32_t v : {8u, 8u, 2u, static_cast<std::uint32_t>(-5000), flags}) {
    put_u32(smk, v);
  }
  for (int i = 0; i < 7; ++i) {
    put_u32(smk, 0);
  }
  put_u32(smk, static_cast<std::uint32_t>(trees.bytes().size()));
  for (int i = 0; i < 4; ++i) {
    put_u32(smk, 16);
  }
  put_u32(smk, 0x40000000 | 11025);
  for (int i = 0; i < 7; ++i) {
    put_u32(smk, 0);  // Other audio rates, then the unused field.
  }
  put_u32(smk, static_cast<std::uint32_t>(frame0.size()) | 1);  // Key frame.
  put_u32(smk, static_cast<std::uint32_t>(frame1.size()));
  smk.insert(smk.end(), {0x01, 0x03});
  smk.insert(smk.end(), trees.bytes().begin(), trees.bytes().end());
  smk.insert(smk.end(), frame0.begin(), frame0.end());
  smk.insert(smk.end(), frame1.begin(), frame1.end());
  return smk;
}

struct Reference {
  const char* path;
  int width;
  int height;
  std::uint32_t frames;
  std::uint32_t audio_bytes;  // 0: no audio track.
  std::uint64_t video_fnv;    // All frames as `pal8` (pixels, palette), `fnv1a_words`.
  std::uint64_t frame10_fnv;  // Frame 10 in RGB24, `fnv1a`.
  std::uint64_t audio_fnv;
};

// Values from ffmpeg 6 (`-f rawvideo -pix_fmt pal8` or `rgb24`, and the
// native audio sample format: `u8` or `s16le`).
constexpr Reference kReferences[] = {
    {"CINES/BOT.SMK", 360, 440, 105, 231524, 0xdcc5f338cbe6b005, 0x44fe5ccca5608c14, 0x9fcb80d766e5dab2},
    {"CINES/COPYR.SMK", 640, 480, 35, 0, 0x6737403de6e1f16a, 0x7699fbc2d1941cdf, 0xcbf29ce484222325},
    {"CINES/HOME.SMK", 360, 440, 225, 496124, 0x47ea85f4ec6f5c1e, 0x8c0dcd04d11c3912, 0x6e6e17bfd9d99237},
    {"CINES/IMP.SMK", 360, 440, 104, 229320, 0x87f29627453730d8, 0x05967c4f5b13e3b3, 0x2b056d6e47f4b396},
    {"CINES/LOGO444.SMK", 640, 480, 212, 1869840, 0x251193a002e5cf0e, 0xf7bf209b97ec430c, 0x9a6b0b4f0de78562},
    {"CINES/SHIN.SMK", 360, 440, 392, 864360, 0xf718d9381f167b74, 0x8b8900948a0deca0, 0xbc56afc04a7360cb},
    {"CINES/SREC.SMK", 360, 440, 105, 231524, 0x85a72ad393687780, 0xd4d0dcad37396a91, 0xac4d82c43fdfb7f7},
    {"CINES/SURF.SMK", 360, 440, 103, 227112, 0xb2a59c9eed0d2764, 0xfdc57542c6af5f2e, 0x92f557fd0d1a13bc},
    {"BUD/13.SMK", 320, 240, 1296, 2857680, 0xc283993e73c420ce, 0x8865524875bda4a1, 0x52753c468a838c9c},
};

}  // namespace

TEST_CASE("Smacker videos decode exactly like ffmpeg", "[smk][content]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const Reference& ref : kReferences) {
    INFO(ref.path);
    SmackerVideo video = SmackerVideo::load(install, ref.path);
    CHECK(video.width() == ref.width);
    CHECK(video.height() == ref.height);
    CHECK(video.y_scale() == SmackerYScale::kNone);
    CHECK(video.frame_count() == ref.frames);
    CHECK(video.frame_duration_us() == 100000);

    std::uint64_t video_fnv = 0xcbf29ce484222325;
    std::uint64_t audio_fnv = 0xcbf29ce484222325;
    std::size_t audio_bytes = 0;
    while (video.decode_next()) {
      video_fnv = fnv1a_words(video.image().pixels(), video_fnv);
      video_fnv = fnv1a_words(ffmpeg_palette(video.palette()), video_fnv);
      if (video.frame_index() == 10) {
        CHECK(fnv1a(to_rgb24(video)) == ref.frame10_fnv);
      }
      if (!video.audio_tracks().empty()) {
        audio_fnv = fnv1a(video.audio(0), audio_fnv);
        audio_bytes += video.audio(0).size();
      }
    }
    CHECK(video.frame_index() == static_cast<int>(ref.frames) - 1);
    CHECK(video_fnv == ref.video_fnv);
    CHECK(audio_bytes == ref.audio_bytes);
    CHECK(audio_fnv == ref.audio_fnv);
  }
}

TEST_CASE("Smacker headers of every video", "[smk][content]") {
  SH_REQUIRE_GAME_DATA(install);
  // Interview frame counts, BUD/1.SMK to BUD/16.SMK.
  constexpr std::uint32_t kBudFrames[] = {1771, 750, 945, 736, 616, 872, 709, 1131,
                                          786, 1441, 573, 982, 1296, 628, 695, 1759};
  for (int i = 0; i < 16; ++i) {
    const std::string path = "BUD/" + std::to_string(i + 1) + ".SMK";
    INFO(path);
    const SmackerVideo video = SmackerVideo::load(install, path);
    CHECK(video.width() == 320);
    CHECK(video.height() == 240);
    CHECK(video.frame_count() == kBudFrames[i]);
    REQUIRE(video.audio_tracks().size() == 1);
    const SmackerAudioFormat& audio = video.audio_tracks()[0];
    CHECK((audio.track == 0 && audio.rate == 22050 && audio.channels == 1 && audio.bits == 8 && audio.compressed));
  }
  const SmackerVideo logo = SmackerVideo::load(install, "CINES/LOGO444.SMK");
  REQUIRE(logo.audio_tracks().size() == 1);
  const SmackerAudioFormat& audio = logo.audio_tracks()[0];
  CHECK((audio.rate == 22050 && audio.channels == 2 && audio.bits == 16 && audio.compressed));
  CHECK(SmackerVideo::load(install, "CINES/COPYR.SMK").audio_tracks().empty());
}

TEST_CASE("Smacker palette updates, Y doubling and raw audio", "[smk]") {
  SmackerVideo video(synthetic_smk(0x04), "synthetic");
  CHECK(video.frame_duration_us() == 50000);
  CHECK(video.width() == 8);
  CHECK(video.height() == 16);
  REQUIRE(video.audio_tracks().size() == 1);
  CHECK((video.audio_tracks()[0].rate == 11025 && !video.audio_tracks()[0].compressed));

  REQUIRE(video.decode_next());
  CHECK(video.palette_changed());
  CHECK(video.palette().colors[0] == Rgb{255, 0, 65});
  CHECK(video.palette().colors[5] == Rgb{4, 8, 12});
  CHECK(video.audio(0).empty());
  CHECK(video.image().height() == 16);
  for (const std::uint8_t p : video.image().pixels()) {
    CHECK(p == 5);
  }

  // Frame 1 copies entries 5 and 6 of the previous palette to 0 and 1.
  REQUIRE(video.decode_next());
  CHECK(video.palette().colors[0] == Rgb{4, 8, 12});
  CHECK(video.palette().colors[1] == Rgb{0, 0, 0});
  CHECK(video.palette().colors[5] == Rgb{4, 8, 12});
  CHECK(std::vector(video.audio(0).begin(), video.audio(0).end()) == std::vector<std::uint8_t>{0x80, 0x90, 0x70, 0x81});
  CHECK_FALSE(video.decode_next());
  CHECK(video.frame_index() == 1);

  video.rewind();
  CHECK(video.frame_index() == -1);
  REQUIRE(video.decode_next());
  CHECK(video.palette().colors[0] == Rgb{255, 0, 65});

  SmackerVideo interlaced(synthetic_smk(0x02), "synthetic");
  REQUIRE(interlaced.decode_next());
  CHECK(interlaced.image().at(3, 2) == 5);
  CHECK(interlaced.image().at(3, 3) == 0);
}

TEST_CASE("Smacker rejects malformed files", "[smk]") {
  std::vector<std::uint8_t> smk = synthetic_smk(0);
  CHECK_THROWS_AS(SmackerVideo(std::vector<std::uint8_t>(smk.begin(), smk.end() - 1), "cut"), DataError);
  smk[3] = '4';
  CHECK_THROWS_AS(SmackerVideo(smk, "smk4"), DataError);
}

}  // namespace sh
