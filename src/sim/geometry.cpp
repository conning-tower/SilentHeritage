#include "sim/geometry.h"

#include "core/fixed_math.h"

namespace sh::geom {

std::int32_t tenths_to_radians16(Angle10 tenths) {
  const std::int32_t t16 = static_cast<std::int16_t>(tenths.tenths());
  return fx::div(fx::imul(t16, 0x10000), kTenthsPerRadian16);
}

Angle10 radians16_to_tenths(std::int32_t radians) {
  return Angle10(fx::trunc(fx::mul(radians, kTenthsPerRadian16)));
}

Angle10 bearing_to(WorldXZ from, WorldXZ to) { return fx::heading_from_delta(to.x - from.x, to.z - from.z); }

std::int32_t range_yards(WorldXZ from, WorldXZ to) {
  return static_cast<std::int32_t>(fx::approx_distance(to.x - from.x, to.z - from.z) / kUnitsPerYard);
}

}  // namespace sh::geom
