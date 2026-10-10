// The chart view (`chart_view_*`, 0x2a5c8..0x2c531 and 0x94dfa..0x9555e):
// the painted `.WLD` chart scrolled and zoomed into a screen viewport, its
// coordinate transforms between world positions, chart (map) pixels and
// screen pixels with the original's 16.16 arithmetic, and the picture of
// the map: the chart scaled by nearest neighbour, or above 6x the coast
// tiles of the map's icon sprites over the terrain grid.
//
// The view object is the original's 0x11a864: +0x08 scale, +0x14/+0x18
// centre (chart pixels, 16.16), +0x1c/+0x20 half extents (chart pixels),
// +0x24 zoom, +0x28 step (chart pixels per screen pixel). The map is the
// chart's declared size (`0x11a794`: the rows less their last column), its
// edges the printed neat line and graduated border of the chart picture.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "assets/shp.h"
#include "assets/world.h"
#include "core/image.h"
#include "gfx/viewport.h"

namespace sh {

// A screen position of the view: whole pixels (truncated toward zero) and
// the 16.16 values they come from.
struct ChartPoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t x16 = 0;
  std::int32_t y16 = 0;
};

// The largest zoom box (`0x10375c`): 2048x.
inline constexpr std::int32_t kChartMaxZoom = 0x800;

class ChartView {
 public:
  // The view of `world`'s chart in the inclusive screen rectangle
  // (`x0`, `y0`)-(`x1`, `y1`), at scale 1.0 and zoom 1x, centred on the
  // chart's top-left corner (the static view object and `chart_reset_view`).
  ChartView(const World& world, int x0, int y0, int x1, int y1);

  // `chart_view_set_zoom` (0x94eb0): zooms below 1x (0x10000) are ignored;
  // recomputes the half extents and the step and clamps the centre.
  void set_zoom(std::int32_t zoom);
  // `chart_view_set_center` (0x95001): the centre (16.16 chart pixels),
  // clamped so that the view stays on the chart.
  void set_center(std::int32_t x, std::int32_t y);

  std::int32_t zoom() const { return zoom_; }
  // The zoom's whole part (`0x11a888 >> 16`, truncated).
  std::int32_t zoom_int() const;
  std::int32_t center_x() const { return cx_; }
  std::int32_t center_y() const { return cy_; }
  std::int32_t half_width() const { return half_w_; }
  std::int32_t half_height() const { return half_h_; }
  std::int32_t step() const { return step_; }
  int x0() const { return x0_; }
  int y0() const { return y0_; }
  int x1() const { return x1_; }
  int y1() const { return y1_; }

  // `world_to_chart_xy` (0x9514c): the screen position (relative to the
  // viewport) of world point (`x` east, `z` north).
  ChartPoint world_to_screen(std::int32_t x, std::int32_t z) const;
  // `chart_view_world_to_screen` (0x950cd): the chart position (16.16) of a
  // world point, as a centre for `set_center`.
  std::array<std::int32_t, 2> world_to_chart(std::int32_t x, std::int32_t z) const;
  // `chart_view_map_to_screen` (0x9505e): the chart position (16.16) under
  // screen pixel (`x`, `y`) of the viewport.
  std::array<std::int32_t, 2> screen_to_chart(std::int32_t x, std::int32_t y) const;
  // `chart_view_screen_to_world` (0x953ca): the world point (x, z) under
  // screen pixel (`x`, `y`) of the viewport.
  std::array<std::int32_t, 2> screen_to_world(std::int32_t x, std::int32_t y) const;
  // `chart_point_on_screen` (0x2c4e0): the screen position of a world point
  // and whether it is within 32000 pixels (drawable).
  bool point_on_screen(std::int32_t x, std::int32_t z, std::int32_t& sx, std::int32_t& sy) const;

  // Whether `chart_view_draw` uses the coast tiles (the map has icons and
  // the step is at most 0x2aab, above 6x).
  bool tiled() const { return !tiles_.empty() && step_ <= 0x2aab; }
  // `chart_view_draw` (0x94dfa): draws the map into `screen` within the
  // viewport.
  void draw(Image8& screen);

 private:
  void draw_scaled(Image8& screen) const;  // 0x9775b
  void draw_tiles(Image8& screen);         // chart_view_draw_zoomed (0x9555e)

  const World& world_;
  std::vector<SpriteFrame> tiles_;  // the map's icons (frames 0..15)
  std::array<Image8, 16> scratch_;  // 0x12a75c: one render buffer per tile
  int x0_, y0_, x1_, y1_;
  std::int32_t map_w_;  // 0x11a798 + 1
  std::int32_t map_h_;  // 0x11a79c + 1
  std::int32_t scale_ = 0x10000;  // +0x08
  std::int32_t cx_ = 0;           // +0x14
  std::int32_t cy_ = 0;           // +0x18
  std::int32_t half_w_ = 0;       // +0x1c
  std::int32_t half_h_ = 0;       // +0x20
  std::int32_t zoom_ = 0;         // +0x24
  std::int32_t step_ = 0;         // +0x28
};

}  // namespace sh
