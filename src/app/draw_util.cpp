#include "app/draw_util.h"

#include <algorithm>

namespace sh {

void blit_pixels(const Viewport& vp, std::span<const std::uint8_t> pixels, int width, int height, int x, int y) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  const int left = vp.x0 + x;
  const int top = vp.y0 + y;
  const int x_begin = std::max(left, clip.left);
  const int x_end = std::min(left + width - 1, clip.right);
  if (x_begin > x_end) {
    return;
  }
  for (int row = std::max(top, clip.top); row <= std::min(top + height - 1, clip.bottom); ++row) {
    const auto src = pixels.subspan(static_cast<std::size_t>(row - top) * static_cast<std::size_t>(width) +
                                        static_cast<std::size_t>(x_begin - left),
                                    static_cast<std::size_t>(x_end - x_begin + 1));
    std::ranges::copy(src, vp.bitmap->row(row).begin() + x_begin);
  }
}

void fill_rect(const Viewport& vp, int x, int y, int w, int h, std::uint8_t colour) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  const int x0 = std::max(vp.x0 + x, clip.left);
  const int y0 = std::max(vp.y0 + y, clip.top);
  const int x1 = std::min(vp.x0 + x + w - 1, clip.right);
  const int y1 = std::min(vp.y0 + y + h - 1, clip.bottom);
  if (x0 <= x1 && y0 <= y1) {
    vp.bitmap->fill_rect(x0, y0, x1 - x0 + 1, y1 - y0 + 1, colour);
  }
}

void draw_palette_swatches(const Viewport& vp, int x, int y, int cell) {
  for (int i = 0; i < 256; ++i) {
    fill_rect(vp, x + (i % 16) * cell, y + (i / 16) * cell, cell - 1, cell - 1, static_cast<std::uint8_t>(i));
  }
}

}  // namespace sh
