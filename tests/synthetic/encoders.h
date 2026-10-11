// Writers of the game's file formats, the inverses of the parsers of
// src/assets: the synthetic install (synthetic_install.h) writes its files
// with them. Each one round-trips through its parser
// (synthetic_encoders_test.cpp).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/environment.h"
#include "assets/objects.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "assets/world.h"
#include "core/image.h"

namespace sh::test {

// A little-endian byte sink for the writers.
class Bytes {
 public:
  void u8(std::uint8_t v) { data_.push_back(v); }
  void u16(std::uint16_t v);
  void u32(std::uint32_t v);
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  void bytes(std::span<const std::uint8_t> b) { data_.insert(data_.end(), b.begin(), b.end()); }
  // Writes `s` in an `n`-byte field, NUL-padded (cut to n - 1 characters).
  void fixed_string(std::string_view s, std::size_t n);
  // Writes the 6-byte header of the game's own formats: magic, NUL, version.
  void format_header(std::string_view magic, std::uint8_t version);
  void zeros(std::size_t n) { data_.insert(data_.end(), n, 0); }
  // Overwrites the u32 at `offset` (written before).
  void patch_u32(std::size_t offset, std::uint32_t v);

  std::size_t size() const { return data_.size(); }
  std::vector<std::uint8_t>& data() { return data_; }
  std::vector<std::uint8_t> take() { return std::move(data_); }

 private:
  std::vector<std::uint8_t> data_;
};

// Returns `data` packed as RLE90 (`rle90_decode`'s input).
std::vector<std::uint8_t> rle90_encode(std::span<const std::uint8_t> data);

// Returns `data` as a compressed block (`read_compressed`'s input: the
// u32 size, then the LZW stream of its RLE90 packing).
std::vector<std::uint8_t> encode_compressed(std::span<const std::uint8_t> data);

// Returns the 8-bit RLE PCX of `image` with the 6-bit palette `palette`.
std::vector<std::uint8_t> encode_pcx(const Image8& image, const VgaPalette& palette);

// One sprite frame to write: its pixels, the transparent colour (none: all
// opaque) and the hotspot's place in the picture.
struct SpriteImage {
  Image8 pixels;
  std::optional<std::uint8_t> transparent;
  int hotspot_x = 0;
  int hotspot_y = 0;
  std::vector<ShpPaletteEntry> palette;
};

// Returns a `.SHP` holding `frames` (an empty picture writes an empty
// frame).
std::vector<std::uint8_t> encode_shp(std::span<const SpriteImage> frames);

// One glyph of a font: `width` x the font's height pixels, row by row
// (0xff transparent).
struct GlyphImage {
  std::uint32_t width = 0;
  std::vector<std::uint8_t> pixels;
};

// Returns a `.FNT` of `glyphs` (indexed by character code) of `height`.
std::vector<std::uint8_t> encode_fnt(std::span<const GlyphImage> glyphs, std::uint32_t height);

// One member of a `.CAT` archive.
struct CatMember {
  std::string name;  // at most 12 characters
  std::vector<std::uint8_t> data;
};

// Returns a `.CAT` archive of `members`.
std::vector<std::uint8_t> encode_cat(std::span<const CatMember> members);

// Returns the `.WLD` of `world` (its version decides the optional parts;
// the chart's rows are RLE90-packed).
std::vector<std::uint8_t> encode_world(const World& world);

// Returns the `.OBS` of `objects`.
std::vector<std::uint8_t> encode_objects(std::span<const MapObject> objects);

// Returns the `.ENV` of `env` (version 8 or 9); its header's PCX size is
// set from `env.sky_pcx`.
std::vector<std::uint8_t> encode_environment(const Environment& env);

}  // namespace sh::test
