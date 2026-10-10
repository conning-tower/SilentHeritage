// Drawing helpers of the asset viewer.
#pragma once

#include <cstdint>
#include <span>

#include "core/image.h"
#include "gfx/viewport.h"

namespace sh {

// Copies the `width` x `height` row-major `pixels` with their top-left
// corner at (x, y) of `vp`, clipped to it.
void blit_pixels(const Viewport& vp, std::span<const std::uint8_t> pixels, int width, int height, int x, int y);

inline void blit_image(const Viewport& vp, const Image8& image, int x, int y) {
  blit_pixels(vp, image.pixels(), image.width(), image.height(), x, y);
}

// Fills the rectangle at (x, y) of `vp`, clipped to it.
void fill_rect(const Viewport& vp, int x, int y, int w, int h, std::uint8_t colour);

// Draws 256 swatches of the palette (16 x 16 cells of `cell` pixels) with
// their top-left corner at (x, y) of `vp`.
void draw_palette_swatches(const Viewport& vp, int x, int y, int cell);

}  // namespace sh
