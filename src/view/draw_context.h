// Internal to the view module: the state shared by the passes of
// `render_world_view` (backdrop, terrain, object draws).
#pragma once

#include <optional>

#include "core/random.h"
#include "gfx/sprite.h"
#include "view/render_list.h"
#include "view/world_palette.h"
#include "view/world_view.h"

namespace sh::view3d {

struct DrawContext {
  Viewport frame;  // the whole 640 x 260 frame (wider for a wide view)
  Viewport view;   // the frame cut to the screen width (0x10fd7c)
  const ViewCamera& camera;
  const WorldViewAssets& assets;
  const WorldLighting& lighting;
  WatcomRandom& random;
  Image8 scratch;      // the rotate/scale buffer of `sprite_draw_transformed`
  ColourRemap gui;     // the GUI colour table 0x11dce0 (effects draw through it)
};

// Returns the width of the frame.
inline int frame_width(const DrawContext& ctx) { return ctx.frame.x1 - ctx.frame.x0 + 1; }

// Returns `vp` with its bottom clip row moved to `row` (frame-relative).
inline Viewport with_bottom(const Viewport& vp, int row) { return {vp.bitmap, vp.x0, vp.y0, vp.x1, vp.y0 + row}; }

// Writes `colour` at (x, y) of `vp` if inside its clip (`gfx_put_pixel`).
void put_pixel(const Viewport& vp, int x, int y, std::uint8_t colour);
// Returns the pixel at (x, y) of `vp`, or -1 outside its clip (`gfx_get_pixel`).
int get_pixel(const Viewport& vp, int x, int y);

// Draws the sky band (panorama or plain colour), the stars at night and
// the sun and moon (`world_draw_sky`).
void draw_sky(DrawContext& ctx, const ViewDetail& detail);
// Draws the sea below the horizon (`world_draw_sea_plane`), the texture
// shifted by `drift` (16.16 texels, 0x170f6c/70).
void draw_sea(DrawContext& ctx, const ViewDetail& detail, std::int32_t drift);
// Draws the terrain cells far to near with the render-list entries of each
// cell, after the entries outside the terrain window (`world_draw_terrain`).
void draw_terrain_and_objects(DrawContext& ctx, RenderList& list, const ViewDetail& detail);
// Draws one entry through its type's routine (0x10ff0c). `cell_band` is
// the distance band of the terrain cell drawing it (static objects remap
// through it), empty when drawn outside the terrain.
void draw_entry(DrawContext& ctx, const RenderEntry& entry, std::optional<std::int32_t> cell_band);

}  // namespace sh::view3d
