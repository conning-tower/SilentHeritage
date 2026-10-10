// The chart terrain grid as the sensors and the AI query it, and the line
// of sight over it (`los_blocked_by_land` 0x2bed0 with its cell callback
// `los_plot_terrain_cell` 0x2bfc8, which rasterizes the line with the
// callback mode of `gfx_draw_line_clipped` 0xa9f61).
//
// Grid coordinates: a world position (x east, z north) is in column
// x / cell and row (height x cell - z) / cell, row 0 at the top (north).
// A value above 0x20 is land (value - 0x20 height levels of 350 ft at scale
// 0), 0x20 is the coastline, below is water.
#pragma once

#include <cstdint>

#include "assets/world.h"
#include "sim/geometry.h"

namespace sh {

// The grid the sensors read (`0x16e174`, width `0x16e16c`, height
// `0x16e170`, scale `0x170f24`).
class TerrainGrid {
 public:
  virtual ~TerrainGrid() = default;
  virtual std::int32_t width() const = 0;
  virtual std::int32_t height() const = 0;
  // The scale shift: a cell is 0x10000 >> shift world units wide.
  virtual std::uint32_t scale_shift() const = 0;
  // Returns the value at `index` = row x width + column, as the original
  // addresses the grid (a column past the east edge reads the next row);
  // 0 (water) outside the grid's bytes.
  virtual std::uint8_t value_at(std::int64_t index) const = 0;

  std::int32_t cell_size() const { return 0x10000 >> scale_shift(); }
  // Returns the column of world `x` and the row of world `z`.
  std::int32_t column_of(std::int32_t x) const { return x / cell_size(); }
  std::int32_t row_of(std::int32_t z) const { return (height() * cell_size() - z) / cell_size(); }
  // Returns the value of cell (`column`, `row`).
  std::uint8_t value(std::int32_t column, std::int32_t row) const {
    return value_at(static_cast<std::int64_t>(row) * width() + column);
  }
};

// The grid of a `.WLD` map (which must outlive it).
class WorldTerrainGrid : public TerrainGrid {
 public:
  explicit WorldTerrainGrid(const World& world) : world_(world) {}
  std::int32_t width() const override { return world_.grid.width; }
  std::int32_t height() const override { return world_.grid.height; }
  std::uint32_t scale_shift() const override { return world_.scale_shift; }
  std::uint8_t value_at(std::int64_t index) const override;

 private:
  const World& world_;
};

// Visits the pixels of the line from (`x0`, `y0`) to (`x1`, `y1`), both
// included, in order from the first point, keeping those inside
// [0, max_x] x [0, max_y]: the major axis steps by one, the minor axis by a
// 32-bit DDA started at one half (the callback loops of
// `gfx_draw_line_clipped`). `plot(x, y)` is called for each.
template <typename Plot>
void walk_line(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::int32_t max_x,
               std::int32_t max_y, Plot plot) {
  const std::int32_t dx = x1 - x0;
  const std::int32_t dy = y1 - y0;
  const std::uint32_t adx = static_cast<std::uint32_t>(dx < 0 ? -dx : dx);
  const std::uint32_t ady = static_cast<std::uint32_t>(dy < 0 ? -dy : dy);
  const std::int32_t sx = dx < 0 ? -1 : 1;
  const std::int32_t sy = dy < 0 ? -1 : 1;
  const bool x_major = adx >= ady;
  const std::uint32_t major = x_major ? adx : ady;
  const std::uint32_t minor = x_major ? ady : adx;
  std::uint32_t slope = 0xffffffffu;
  if (major != minor) {
    slope = static_cast<std::uint32_t>((static_cast<std::uint64_t>(minor) << 32) / major);
  }
  std::uint32_t acc = 0x80000000u;
  std::int32_t x = x0;
  std::int32_t y = y0;
  for (std::uint32_t i = 0; i <= major; ++i) {
    if (x >= 0 && x <= max_x && y >= 0 && y <= max_y) {
      plot(x, y);
    }
    const std::uint32_t before = acc;
    acc += slope;
    const bool carry = acc < before;
    if (x_major) {
      x += sx;
      y += carry ? sy : 0;
    } else {
      y += sy;
      x += carry ? sx : 0;
    }
  }
}

// What the line from one point to another crosses (`los_blocked_by_land`
// with its callback): `land` when a cell of the 2x2 block sampled at a pixel
// is land, or when all four are coast; `max_level` is the highest land level
// met.
struct LandOnLine {
  bool land = false;
  std::int32_t max_level = 0;
};

// Returns the land met on the grid line from `from` to `to` (world units).
LandOnLine land_on_line(const TerrainGrid& grid, geom::WorldXZ from, geom::WorldXZ to);

// Returns whether land hides a target `target_height` world units tall at
// `to` from `from`: land on the line, unless the target stands higher than
// the highest land met (`los_blocked_by_land` with a height >= 0).
bool los_blocked_by_land(const TerrainGrid& grid, geom::WorldXZ from, geom::WorldXZ to, std::int32_t target_height);

}  // namespace sh
