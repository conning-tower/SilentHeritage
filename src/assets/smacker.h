// RAD Game Tools Smacker `.SMK` videos: the cinematics (`CINES/*.SMK`) and
// the Bud Gruner interviews (`BUD/<n>.SMK`). The game played them with the
// Smacker 2.x/3.x library (`SmackOpen`, `SmackDoFrame`...); this decoder
// supports the version 2 ("SMK2") files it shipped with, with their Huffman
// DPCM-compressed or raw audio tracks.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/image.h"

namespace sh {

class Install;

namespace smacker_detail {

// A Huffman tree of 16-bit values flattened in pre-order, with the indices
// of its three cache leaves (implementation detail of `SmackerVideo`).
struct BigTree {
  std::vector<std::uint32_t> entries;
  std::array<std::size_t, 3> cache{};
};

}  // namespace smacker_detail

// Vertical scaling selected by the header flags. The stored picture then has
// half the displayed height: each stored row is shown twice (`kDoubled`) or
// followed by a black (index 0) row (`kInterlaced`).
enum class SmackerYScale : std::uint8_t { kNone, kInterlaced, kDoubled };

// The format of one audio track of a video.
struct SmackerAudioFormat {
  std::uint8_t track = 0;     // Smacker track number, 0..6.
  std::uint32_t rate = 0;     // Samples per second.
  std::uint8_t channels = 1;  // 1 (mono) or 2 (stereo, samples interleaved left first).
  std::uint8_t bits = 8;      // 8 (unsigned) or 16 (signed little-endian).
  bool compressed = false;    // Huffman DPCM in the file (decoded transparently).
  std::uint32_t max_chunk_size = 0;  // The header's largest decoded chunk, in bytes.
};

// A Smacker video held in memory, decoded frame by frame in order.
//
// Usage: construct (or `load`), then call `decode_next()` until it returns
// false; after each call, `image()`, `palette()` and `audio(i)` hold the
// frame's picture, palette and sound, and the frame is meant to be shown for
// `frame_duration_us()`.
class SmackerVideo {
 public:
  // Parses the header and tables of the file `data`; `name` names it in
  // error messages. Throws `DataError` if it is not a well-formed SMK2 file.
  SmackerVideo(std::vector<std::uint8_t> data, std::string_view name);

  // Reads and parses the video `dos_path` of the install (e.g. "CINES/HOME.SMK").
  static SmackerVideo load(const Install& install, std::string_view dos_path);

  // Returns the displayed size (the height includes the Y scaling).
  int width() const { return width_; }
  int height() const { return y_scale_ == SmackerYScale::kNone ? stored_height_ : 2 * stored_height_; }
  SmackerYScale y_scale() const { return y_scale_; }
  std::uint32_t frame_count() const { return frame_count_; }
  // Returns how long each frame is shown, in microseconds.
  std::uint32_t frame_duration_us() const { return frame_duration_us_; }
  // Returns the tracks present in the file, by increasing track number.
  const std::vector<SmackerAudioFormat>& audio_tracks() const { return audio_tracks_; }

  // Decodes the next frame and returns true, or returns false (changing
  // nothing) when all frames were decoded. Throws `DataError` on corrupt data.
  bool decode_next();
  // Goes back to before the first frame (picture and palette cleared).
  void rewind();

  // Returns the index of the last decoded frame, or -1 before the first.
  int frame_index() const { return static_cast<int>(next_frame_) - 1; }
  // Returns the picture of the last decoded frame (`width()` x `height()`).
  const Image8& image() const { return y_scale_ == SmackerYScale::kNone ? stored_ : scaled_; }
  // Returns the palette of the last decoded frame.
  const Palette& palette() const { return palette_; }
  // Returns whether the last decoded frame changed the palette.
  bool palette_changed() const { return palette_changed_; }
  // Returns the PCM bytes of the last decoded frame for `audio_tracks()[i]`
  // (empty when the frame has no sound for it).
  std::span<const std::uint8_t> audio(std::size_t i) const { return audio_[i]; }

 private:
  void decode_palette(std::span<const std::uint8_t> chunk);
  void decode_video(std::span<const std::uint8_t> chunk);

  std::vector<std::uint8_t> data_;
  std::string name_;
  int width_ = 0;
  int stored_height_ = 0;
  SmackerYScale y_scale_ = SmackerYScale::kNone;
  std::uint32_t frame_count_ = 0;
  std::uint32_t frame_duration_us_ = 0;
  std::vector<SmackerAudioFormat> audio_tracks_;
  std::vector<std::size_t> frame_offsets_;  // frame_count_ + 1 entries.
  std::vector<std::uint8_t> frame_types_;
  smacker_detail::BigTree mmap_tree_;  // Two-color block masks.
  smacker_detail::BigTree mclr_tree_;  // Two-color block colors.
  smacker_detail::BigTree full_tree_;  // Full block pixel pairs.
  smacker_detail::BigTree type_tree_;  // Block types and run lengths.

  std::uint32_t next_frame_ = 0;
  Image8 stored_;
  Image8 scaled_;
  Palette palette_;
  bool palette_changed_ = false;
  std::vector<std::vector<std::uint8_t>> audio_;
};

}  // namespace sh
