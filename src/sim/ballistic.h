// Free-flight trajectories of depth charges, shells and bombs
// (`ballistic_*`, 0x8c029..0x8c1af): a launch velocity along a heading and
// pitch, then position = origin + v t with a fall of 640 units/s^2 x t^2
// (gravity of 80 ft/s^2, the value the deck-gun range tables also use).
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "core/fixed_math.h"

namespace sh {

// The ballistic block (DC +0xd0, shell +0xd8, bomb +0xd4).
struct Ballistic {
  fx::Vec3 position;     // +0x00 world units.
  fx::Vec3 origin;       // +0x0c
  fx::Vec3 velocity;     // +0x18 16.16 world units per second.
  std::int32_t time = 0; // +0x24 16.16 seconds since launch.
};

// Returns a block launched from `position` at `speed_fps` (feet per second)
// along `heading` and `pitch` (`ballistic_init`, 0x8c029). The position
// itself is left for the caller (the objects keep their own).
Ballistic ballistic_init(std::int32_t speed_fps, const fx::Vec3& position, Angle10 heading, Angle10 pitch);

// Advances the block by `dt` (16.16 s) and sets `position`
// (`ballistic_step`, 0x8c092).
void ballistic_step(Ballistic& b, std::int32_t dt);

// Returns the elevation (tenths of a degree) that carries a shell of muzzle
// velocity `velocity_fps` to `range` world units, 0 beyond the maximum
// range (`ballistic_elevation_for_range`, 0x8c175).
std::int32_t ballistic_elevation_for_range(std::int32_t velocity_fps, std::int32_t range);

// Returns the range in feet of a shell fired at `elevation` (tenths of a
// degree) (`ballistic_range_for_elevation`, 0x8c1af).
std::int32_t ballistic_range_for_elevation(std::int32_t velocity_fps, Angle10 elevation);

}  // namespace sh
