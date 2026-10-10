// The sea floor of a `.WLD` map: the terrain height under any world point,
// interpolated over the grid's triangles (`world_terrain_height_at`,
// 0x9449b).
#pragma once

#include <cstdint>

#include "assets/world.h"
#include "sim/motion.h"

namespace sh {

// Heights in world units (16 = 1 ft, up positive) over the grid of `world`,
// which must outlive the floor.
class WorldSeaFloor : public SeaFloor {
 public:
  // Returned outside the grid.
  static constexpr std::int32_t kOutsideHeight = -160000;

  explicit WorldSeaFloor(const World& world) : world_(world) {}

  // Returns the height at (`x`, `z`), `z` counting north as in `.SCN`
  // positions: a cell is split along its SW-NE diagonal and the point is
  // placed on the plane of its triangle's three corner heights, with the
  // original's fixed-point normal (including its halving of steep slopes).
  std::int32_t height_at(std::int32_t x, std::int32_t z) const override;

 private:
  const World& world_;
};

}  // namespace sh
