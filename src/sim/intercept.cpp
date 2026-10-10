#include "sim/intercept.h"

#include "core/fixed_math.h"
#include "sim/geometry.h"
#include "sim/weapon_math.h"

namespace sh {

std::int32_t motion_intercept_time(const Mover& a, const Mover& b, std::int32_t max_range_ft,
                                   std::int32_t max_angle) {
  // motion_intercept_time (0x8dfcf)
  const std::int32_t dx = b.x - a.x;
  const std::int32_t dz = b.z - a.z;
  if (a.speed.raw() <= 0x8000) {
    return -1;
  }
  if (static_cast<std::int32_t>(fx::approx_distance(dx, dz) >> 4) > max_range_ft) {
    return -1;
  }
  const Angle10 bearing = fx::heading_from_delta(dx, dz);
  const std::int32_t aspect = fx::angle_sub(b.heading, bearing).tenths();
  const std::int32_t cross = fx::mul(b.speed.raw(), fx::angle_sincos(Angle10(-aspect)).sin);
  // The lead angle off the bearing, then a's course error against it.
  const std::int32_t lead = fx::fixed_asin(fx::div(cross, a.speed.raw()));
  const std::int32_t error = fx::angle_sub(fx::angle_sub(bearing, a.heading), Angle10(lead)).tenths();
  const std::int32_t folded = error > 1800 ? error - 3600 : error;
  if (folded < -max_angle || folded > max_angle) {
    return -1;
  }
  const std::int32_t fps = fx::div(6077, 3600);
  const std::int32_t va = fx::mul(a.speed.raw(), fps);
  const std::int32_t vb = fx::mul(b.speed.raw(), fps);
  const fx::SinCos course_b = fx::angle_sincos(b.heading);
  const fx::SinCos course_a = fx::angle_sincos(fx::angle_add(a.heading, Angle10(error)));
  // Solved on x, or on z when both stand on the same x.
  std::int32_t pa = a.x;
  std::int32_t pb = b.x;
  std::int32_t sb = course_b.sin;
  std::int32_t sa = course_a.sin;
  if (b.x == a.x) {
    pa = a.z;
    pb = b.z;
    sb = course_b.cos;
    sa = course_a.cos;
  }
  const std::int32_t closing = fx::mul(vb, sb) - fx::mul(va, sa);
  if (closing == 0) {
    return -1;
  }
  const std::int32_t t = fx::div((pa - pb) / kUnitsPerFoot, closing);
  return t < 0 ? -1 : t;
}

}  // namespace sh
