// The simulation's angle and position conversions on top of the original's
// fixed-point helpers (core/fixed_math.h): radians accumulators, bearings
// and ranges between world positions.
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "core/fixed_math.h"

namespace sh::geom {

// 2pi in 16.16 radians, the range of the vehicles' heading accumulators.
inline constexpr std::int32_t kTwoPi16 = 0x6487e;
// Tenths of a degree per radian (572.958) in 16.16.
inline constexpr std::int32_t kTenthsPerRadian16 = 0x23cf54e;

// Returns `tenths` in 16.16 radians (as `motion_init`; the angle is read as a
// 16-bit value).
std::int32_t tenths_to_radians16(Angle10 tenths);

// Returns 16.16 radians in whole tenths of a degree, truncated toward zero.
Angle10 radians16_to_tenths(std::int32_t radians);

// A position on the world plane in world units (16 = 1 ft).
struct WorldXZ {
  std::int32_t x = 0;
  std::int32_t z = 0;
};

// Returns the true bearing from `from` to `to`, as the original computes it.
Angle10 bearing_to(WorldXZ from, WorldXZ to);

// Returns the range from `from` to `to` in yards (approx_distance / 48).
std::int32_t range_yards(WorldXZ from, WorldXZ to);

}  // namespace sh::geom
