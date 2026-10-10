#include "sim/terrain.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "core/fixed_math.h"
#include "sim/geometry.h"

namespace sh {

namespace {

// Cell corners as (column, row) offsets from the cell's top-left (NW) grid
// value: 0 SW, 1 NW, 2 NE, 3 SE (tables 0x10fd10 / 0x10fd20).
constexpr std::array<std::int32_t, 4> kCornerColumn = {0, 0, 1, 1};
constexpr std::array<std::int32_t, 4> kCornerRow = {1, 0, 0, 1};
// The two triangles, middle corner second (table 0x10fd30).
constexpr std::array<std::array<std::int32_t, 3>, 2> kTriangles = {{{0, 1, 2}, {2, 3, 0}}};

}  // namespace

std::int32_t WorldSeaFloor::height_at(std::int32_t x, std::int32_t z) const {
  // world_terrain_height_at (0x9449b)
  const IndexedRaster& grid = world_.grid;
  const std::int32_t cell = world_.cell_size();
  const std::int32_t down = grid.height * cell - z;
  const std::int32_t column = x / cell;
  const std::int32_t row = down / cell;
  if (column < 0 || column > grid.width - 1 || row < 0 || row > grid.height - 1) {
    return kOutsideHeight;
  }
  const std::int32_t mask = cell - 1;
  const std::int32_t east = x & mask;
  const std::int32_t south = down & mask;
  const std::int32_t west_rest = mask - east;
  const std::int32_t south_rest = mask - south;
  const bool lower_right = south_rest < east;

  std::array<std::int32_t, 3> level{};
  for (std::size_t i = 0; i < level.size(); ++i) {
    const auto corner = static_cast<std::size_t>(kTriangles[lower_right ? 1 : 0][i]);
    // The original reads past the grid for the southernmost row; the read
    // is clamped to the last value here.
    const std::int64_t index = static_cast<std::int64_t>(kCornerRow[corner] + row) * grid.width +
                               kCornerColumn[corner] + column;
    const std::size_t at = std::min(static_cast<std::size_t>(index), grid.pixels.size() - 1);
    level[i] = static_cast<std::int32_t>(grid.pixels[at]) - kCoastCell;
  }
  const std::int32_t base = terrain_level_to_height(level[1], world_.scale_shift);
  if (level[0] == level[1] && level[0] == level[2]) {
    return base;
  }
  for (std::int32_t& l : level) {
    l = terrain_level_to_height(l, world_.scale_shift) - base;
  }
  while (fx::abs(level[2]) > 0x7fff || fx::abs(level[0]) > 0x7fff) {
    level[0] >>= 1;
    level[2] >>= 1;
  }
  fx::Vec3 a;
  fx::Vec3 b;
  std::int32_t u = 0;
  std::int32_t v = 0;
  if (!lower_right) {
    a = {fx::kOne, level[2], 0};
    b = {0, level[0], -fx::kOne};
    u = east;
    v = -south;
  } else {
    a = {-fx::kOne, level[2], 0};
    b = {0, level[0], fx::kOne};
    u = -west_rest;
    v = south_rest;
  }
  const fx::Vec3 n = fx::normalize(fx::cross(a, b));
  // 32-bit wrapping sum, as the original's.
  const auto offset = static_cast<std::int32_t>(
      0u - (static_cast<std::uint32_t>(fx::imul(n.x, u)) + static_cast<std::uint32_t>(fx::imul(n.z, v))));
  return offset / n.y + base;
}

}  // namespace sh
