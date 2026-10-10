// The convex polygon rasterizers of the 3D view: flat fill
// (`gfx_fill_polygon`, 0xaf760), colour remapping (`gfx_remap_polygon`,
// 0xb0d05, the wakes) and affine texture mapping through a shade table
// (`gfx_texture_polygon` mode 1, 0xb1b29, the terrain). All scan-convert
// from the top vertex with 16.16 edge steppers, vertices inclusive, clipped
// to the viewport.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "core/image.h"
#include "gfx/viewport.h"

namespace sh {

// A polygon vertex (24 bytes in the original): integer pixel position
// relative to the viewport origin, a 16.16 colour (only the first vertex's
// is used) and 16.16 texture coordinates.
struct PolyVertex {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t colour = 0;
  std::int32_t u = 0;
  std::int32_t v = 0;
};

// Fills the convex polygon `v` with colour `(v[0].colour + 0x8000) >> 16`.
// Polygons wholly outside one side of the clip rectangle or with no height
// draw nothing.
void fill_polygon(const Viewport& vp, std::span<const PolyVertex> v);

// Replaces every pixel `p` covered by the convex polygon `v` with
// `table[p]` (`gfx_remap_polygon`, 0xb0d05).
void remap_polygon(const Viewport& vp, std::span<const PolyVertex> v, const std::array<std::uint8_t, 256>& table);

// Maps `texture` (row-major, width x height) on the convex polygon `v`,
// drawing `shade[texel]` (mode 1). Texel addresses are computed linearly
// like the original's and wrapped to the texture.
void texture_polygon_shaded(const Viewport& vp, std::span<const PolyVertex> v, const Image8& texture,
                            const std::array<std::uint8_t, 256>& shade);

}  // namespace sh
