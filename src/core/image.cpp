#include "core/image.h"

#include <algorithm>

#include "core/error.h"

namespace sh {

Palette Palette::from_vga6(std::span<const std::uint8_t> rgb6) {
  if (rgb6.size() < 768) {
    throw DataError("VGA palette shorter than 768 bytes");
  }
  Palette p;
  for (std::size_t i = 0; i < 256; ++i) {
    auto scale = [](std::uint8_t v) { return static_cast<std::uint8_t>((v & 0x3f) * 255 / 63); };
    p.colors[i] = {scale(rgb6[i * 3]), scale(rgb6[i * 3 + 1]), scale(rgb6[i * 3 + 2])};
  }
  return p;
}

Palette Palette::from_rgb8(std::span<const std::uint8_t> rgb8) {
  if (rgb8.size() < 768) {
    throw DataError("RGB palette shorter than 768 bytes");
  }
  Palette p;
  for (std::size_t i = 0; i < 256; ++i) {
    p.colors[i] = {rgb8[i * 3], rgb8[i * 3 + 1], rgb8[i * 3 + 2]};
  }
  return p;
}

Image8::Image8(int width, int height, std::uint8_t fill)
    : width_(width), height_(height), pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill) {}

void Image8::fill(std::uint8_t c) { std::ranges::fill(pixels_, c); }

void Image8::fill_rect(int x, int y, int w, int h, std::uint8_t c) {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + w, width_);
  const int y1 = std::min(y + h, height_);
  for (int yy = y0; yy < y1; ++yy) {
    auto r = row(yy);
    std::fill(r.begin() + x0, r.begin() + std::max(x0, x1), c);
  }
}

void Image8::blit(const Image8& src, int x, int y, int transparent) {
  const int x0 = std::max(x, 0);
  const int y0 = std::max(y, 0);
  const int x1 = std::min(x + src.width(), width_);
  const int y1 = std::min(y + src.height(), height_);
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) {
      const std::uint8_t c = src.at(xx - x, yy - y);
      if (c != transparent) {
        set(xx, yy, c);
      }
    }
  }
}

}  // namespace sh
