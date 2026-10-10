// The sprite blitters: RLE frames drawn transparently at their hotspot,
// clipped to a viewport, optionally colour-remapped, rotated and scaled.
#pragma once

#include <array>
#include <cstdint>

#include "assets/pal.h"
#include "assets/shp.h"
#include "core/fixed.h"
#include "gfx/viewport.h"

namespace sh {

// A 256-entry colour table applied to sprite pixels (the original's global
// table 0x11301c, set by `sprite_set_remap_table`).
using ColourRemap = std::array<std::uint8_t, 256>;

// Draws `frame` with its hotspot at (x, y) of `vp` (`draw_sprite_clipped`,
// 0xaaa9c): the frame box x0..x1, y0..y1 lands at x + x0.., y + y0..;
// transparent pixels (RLE skips) keep the target; everything is clipped to
// the viewport. Returns the original's status code.
DrawResult draw_sprite_clipped(const Viewport& vp, const SpriteFrame& frame, int x, int y);

// Same as `draw_sprite_clipped` with every pixel `c` drawn as `remap[c]`
// (`sprite_draw_remapped`, 0xaaf86). No value is transparent here.
DrawResult draw_sprite_remapped(const Viewport& vp, const SpriteFrame& frame, int x, int y,
                                const ColourRemap& remap);

// Flags of `sprite_draw_transformed`.
enum SpriteTransformFlags : unsigned {
  kSpriteRemap = 1,         // draw through `remap`
  kSpriteScratchReady = 2,  // `scratch` already holds this frame (from a previous call)
};

// Draws `frame` rotated by `angle` (clockwise on screen) and scaled by
// (`scale_x`, `scale_y`) about its hotspot at (x, y) of `vp`
// (`sprite_draw_transformed`, 0xab591). With no rotation and a 1.0 scale it
// is `draw_sprite_clipped` (or `draw_sprite_remapped` with `kSpriteRemap`).
// Otherwise the frame is first rendered into `scratch` (resized to the
// frame size, 0xff = transparent; skipped with `kSpriteScratchReady`, which
// requires `scratch` to have the frame's size), whose corners are then
// mapped as a textured quad: a negative scale mirrors. `remap` is required
// with `kSpriteRemap`. Throws `std::invalid_argument` on a misuse of these
// two flags.
DrawResult sprite_draw_transformed(const Viewport& vp, const SpriteFrame& frame, int x, int y, Image8& scratch,
                                   Angle10 angle, Fixed16 scale_x, Fixed16 scale_y, unsigned flags,
                                   const ColourRemap* remap = nullptr);

// Copies the frame's own colours into `palette` (`sprite_apply_palette`,
// 0xaf0a6); a frame without a palette leaves it unchanged.
void sprite_apply_palette(const SpriteFrame& frame, VgaPalette& palette);

}  // namespace sh
