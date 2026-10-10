// The collision-course test of the AI and the torpedo warning
// (`motion_intercept_time`, 0x8dfcf).
#pragma once

#include <cstdint>

#include "core/fixed.h"

namespace sh {

// A mover on the sea plane: position (world units), heading and speed
// (16.16 kn).
struct Mover {
  std::int32_t x = 0;
  std::int32_t z = 0;
  Angle10 heading;
  Fixed16 speed;
};

// Returns the seconds until `a` meets `b` when `a` (faster than 0.5 kn,
// within `max_range_ft`) heads within `max_angle` tenths of the lead course
// that intercepts `b` on its course; -1 otherwise (or when it would have
// met in the past).
std::int32_t motion_intercept_time(const Mover& a, const Mover& b, std::int32_t max_range_ft,
                                   std::int32_t max_angle);

}  // namespace sh
