#include "sim/line_of_sight.h"

#include <array>
#include <cstddef>

namespace sh {

std::uint8_t WorldTerrainGrid::value_at(std::int64_t index) const {
  if (index < 0 || static_cast<std::uint64_t>(index) >= world_.grid.pixels.size()) {
    return 0;
  }
  return world_.grid.pixels[static_cast<std::size_t>(index)];
}

LandOnLine land_on_line(const TerrainGrid& grid, geom::WorldXZ from, geom::WorldXZ to) {
  // los_blocked_by_land (0x2bed0): the line between the grid cells of the
  // two points, clipped to the grid, each pixel handed to the callback.
  // los_plot_terrain_cell (0x2bfc8) samples the 2x2 block of cells at the
  // pixel (tables 0x10370c / 0x10371c).
  static constexpr std::array<std::int32_t, 4> kColumn = {0, 0, 1, 1};
  static constexpr std::array<std::int32_t, 4> kRow = {1, 0, 0, 1};
  LandOnLine result;
  walk_line(grid.column_of(from.x), grid.row_of(from.z), grid.column_of(to.x), grid.row_of(to.z), grid.width() - 1,
            grid.height() - 1, [&](std::int32_t column, std::int32_t row) {
              std::int32_t coast = 0;
              for (std::size_t k = 0; k < kColumn.size(); ++k) {
                const std::int32_t v = grid.value(column + kColumn[k], row + kRow[k]);
                if (v > kCoastCell) {
                  result.land = true;
                  if (result.max_level < v - kCoastCell) {
                    result.max_level = v - kCoastCell;
                  }
                  break;
                }
                if (v == kCoastCell) {
                  ++coast;
                }
              }
              if (coast > 3) {
                result.land = true;
              }
            });
  return result;
}

bool los_blocked_by_land(const TerrainGrid& grid, geom::WorldXZ from, geom::WorldXZ to, std::int32_t target_height) {
  const LandOnLine line = land_on_line(grid, from, to);
  if (!line.land) {
    return false;
  }
  // A land level is 350 ft (0x15e0 world units) at scale 0.
  const std::int32_t land_height = (0x15e0 >> (grid.scale_shift() & 0x1f)) * line.max_level;
  return land_height > target_height;
}

}  // namespace sh
