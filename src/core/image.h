// Indexed 8-bit images and VGA palettes.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sh {

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  constexpr bool operator==(const Rgb&) const = default;
};

// 256 colors with 8-bit components (VGA's 6-bit DAC values are scaled on load).
struct Palette {
  std::array<Rgb, 256> colors{};

  // Builds a palette from 768 bytes of 6-bit VGA DAC values (0..63), scaled
  // to 8 bits as `v * 255 / 63`.
  static Palette from_vga6(std::span<const std::uint8_t> rgb6);
  // Builds a palette from 768 bytes of 8-bit RGB values.
  static Palette from_rgb8(std::span<const std::uint8_t> rgb8);
};

// An indexed-color image, row-major, one byte per pixel.
class Image8 {
 public:
  Image8() = default;
  Image8(int width, int height, std::uint8_t fill = 0);

  int width() const { return width_; }
  int height() const { return height_; }
  std::span<std::uint8_t> pixels() { return pixels_; }
  std::span<const std::uint8_t> pixels() const { return pixels_; }
  std::span<std::uint8_t> row(int y) { return std::span(pixels_).subspan(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_), static_cast<std::size_t>(width_)); }
  std::span<const std::uint8_t> row(int y) const { return std::span(pixels_).subspan(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_), static_cast<std::size_t>(width_)); }

  std::uint8_t at(int x, int y) const { return pixels_[index(x, y)]; }
  void set(int x, int y, std::uint8_t c) { pixels_[index(x, y)] = c; }
  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }

  void fill(std::uint8_t c);
  // Fills the rectangle (clipped to the image).
  void fill_rect(int x, int y, int w, int h, std::uint8_t c);
  // Copies `src` with its top-left corner at (x, y), clipped. Pixels equal
  // to `transparent` (when >= 0) are skipped.
  void blit(const Image8& src, int x, int y, int transparent = -1);

 private:
  std::size_t index(int x, int y) const { return static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x); }

  int width_ = 0;
  int height_ = 0;
  std::vector<std::uint8_t> pixels_;
};

// The original screen size.
inline constexpr int kScreenWidth = 640;
inline constexpr int kScreenHeight = 480;
// The widest screen of the wide-screen layouts (21:9; not in the original).
inline constexpr int kMaxScreenWidth = 1120;

}  // namespace sh
