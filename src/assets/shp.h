// `.SHP` sprite sets (also embedded in `.CAT`, `.OBS` and `.WLD` files).
// Layout in docs/formats.md. Frames
// keep their row RLE as stored, since the blitters (gfx/sprite.h) decode it
// while drawing like the original.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/pal.h"
#include "core/image.h"

namespace sh {

class Install;

// One entry of a frame's partial palette: 6-bit VGA colour for `index`.
struct ShpPaletteEntry {
  std::uint8_t index = 0;
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

// An indexed frame decoded to pixels: `opaque` is 1 where the frame has a
// pixel and 0 where it is transparent (`image` holds 0 there).
struct DecodedFrame {
  Image8 image;
  Image8 opaque;
};

// One sprite frame: its box relative to the hotspot and its RLE rows.
//
// Row RLE (one row per y in y0..y1): control byte `b`, `n = b >> 1`,
// `f = b & 1`: `n > 0, f = 0` run of the next byte n times; `n > 0, f = 1`
// the next n bytes literally; `n = 0, f = 1` skip the next byte's count of
// transparent pixels; `n = 0, f = 0` end of row. Parsing guarantees that
// every row stays within the box and the data.
struct SpriteFrame {
  std::int32_t x0 = 0;  // inclusive box, relative to the hotspot
  std::int32_t y0 = 0;
  std::int32_t x1 = 0;
  std::int32_t y1 = 0;
  std::vector<std::uint8_t> rle;           // the rows; empty for an empty frame
  std::vector<ShpPaletteEntry> palette;    // the frame's own colours, if any

  // Returns true for a frame with nothing to draw (no rows or an empty box,
  // e.g. `SBCHART.SHP` #42).
  bool empty() const { return rle.empty(); }
  int width() const { return empty() ? 0 : static_cast<int>(std::int64_t{x1} - x0 + 1); }
  int height() const { return empty() ? 0 : static_cast<int>(std::int64_t{y1} - y0 + 1); }

  // Returns the frame's pixels and transparency mask, `width() x height()`
  // with (0, 0) at (x0, y0).
  DecodedFrame decode() const;
};

// A parsed sprite set.
class Shp {
 public:
  // Parses the sprite set in `data` (a whole `.SHP` or an embedded one);
  // throws `DataError` naming `context` on a bad header, a frame or palette
  // outside the data, a frame with rows whose box exceeds +-0x7fff, or a
  // row overrunning its box or the data.
  static Shp parse(std::span<const std::uint8_t> data, std::string_view context);
  // Reads and parses the `.SHP` file at `dos_path` of `install`.
  static Shp load(const Install& install, std::string_view dos_path);

  // The version string, "1.10" or "1.00".
  const std::string& version() const { return version_; }
  std::size_t frame_count() const { return frames_.size(); }
  const SpriteFrame& frame(std::size_t i) const { return frames_.at(i); }
  const std::vector<SpriteFrame>& frames() const { return frames_; }

 private:
  std::string version_;
  std::vector<SpriteFrame> frames_;
};

}  // namespace sh
