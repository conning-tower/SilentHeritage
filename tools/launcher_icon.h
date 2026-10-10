// The Android launcher icon, made at build time (tools/make_launcher_icon)
// so that no picture is stored in the repository: RGBA bitmaps, a minimal
// PNG encoder (uncompressed deflate, no zlib), an area-averaging resampler,
// the game-art picture cut from the install's ART/RTNPORT.PCX, and the
// resource PNGs of every density.
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "core/install.h"

namespace sh {

// A pixel with straight (not premultiplied) alpha.
struct Rgba8 {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 0;
  constexpr bool operator==(const Rgba8&) const = default;
};

// A row-major RGBA picture.
class RgbaBitmap {
 public:
  RgbaBitmap() = default;
  RgbaBitmap(int width, int height, Rgba8 fill = {});

  int width() const { return width_; }
  int height() const { return height_; }
  Rgba8 at(int x, int y) const { return pixels_[index(x, y)]; }
  Rgba8& at(int x, int y) { return pixels_[index(x, y)]; }
  std::span<const Rgba8> pixels() const { return pixels_; }

 private:
  std::size_t index(int x, int y) const {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
  }

  int width_ = 0;
  int height_ = 0;
  std::vector<Rgba8> pixels_;
};

// Returns `image` as a PNG file (8-bit RGBA, no filtering, its deflate
// stream made of stored blocks).
std::vector<std::uint8_t> encode_png(const RgbaBitmap& image);

// Returns the CRC-32 of `data` (ISO 3309, as PNG chunks use it).
std::uint32_t crc32(std::span<const std::uint8_t> data);

// Returns the Adler-32 checksum of `data` (as zlib streams end with it).
std::uint32_t adler32(std::span<const std::uint8_t> data);

// Returns `image` resampled to `width` x `height`: each pixel the average
// of the source area it covers (fractional pixels weighted by their
// coverage), alpha-weighted. Meant for reductions; both sizes must be
// positive (throws `std::invalid_argument` otherwise).
RgbaBitmap resize_area(const RgbaBitmap& image, int width, int height);

// Returns the `width` x `height` part of `image` at (`x`, `y`); throws
// `std::invalid_argument` unless it lies inside `image`.
RgbaBitmap crop(const RgbaBitmap& image, int x, int y, int width, int height);

// Returns `image` with the pixels outside its inscribed circle made
// transparent (anti-aliased edge).
RgbaBitmap round_mask(const RgbaBitmap& image);

// Decodes a PAM file (`P7`, RGB_ALPHA or RGB, MAXVAL 255) such as the one
// `android/icon/render_svg.py` writes; throws `DataError` naming `context`
// otherwise.
RgbaBitmap decode_pam(std::span<const std::uint8_t> data, std::string_view context);

// The full-bleed square cut from the install's ART/RTNPORT.PCX (the fleet
// boat "305" at its pier): 600 x 600 pixels, wider than the 480 pixel high
// picture, the conning tower in the centre; the sky above is extended
// (the masts continue, the blue deepens) and the hull below (its streaks
// fading). Throws `DataError` when the file is missing or malformed.
RgbaBitmap game_art_foreground(const Install& install);

// What the monochrome (themed) icon layer keeps opaque.
enum class MonochromeSource : std::uint8_t {
  kDarkness,   // the picture's dark parts (the game art's hull and masts)
  kLightness,  // its light parts (the brass of the SH plate)
};

// Returns the monochrome layer of `image`: white, its alpha `source`
// taken from the luminance and levelled.
RgbaBitmap monochrome(const RgbaBitmap& image, MonochromeSource source);

// Writes the launcher icon PNGs of the square full-bleed adaptive
// foreground `foreground` (108 dp, of which launchers show the central
// 72 dp) into `res_dir`/mipmap-{mdpi,hdpi,xhdpi,xxhdpi,xxxhdpi}/: the
// foreground and the monochrome layer (108 dp: 108..432 px), and the
// legacy icons of API 24-25 (the central 72 dp at 48 dp: 48..192 px),
// square (ic_launcher) and round (ic_launcher_round). Throws
// `std::invalid_argument` for a foreground that is not square or smaller
// than 432 px, `std::runtime_error` when a file cannot be written.
void write_launcher_icons(const RgbaBitmap& foreground, MonochromeSource source,
                          const std::filesystem::path& res_dir);

}  // namespace sh
