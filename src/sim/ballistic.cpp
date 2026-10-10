#include "sim/ballistic.h"

#include "sim/geometry.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kHalfGravity = 0x280;      // World units per s^2 (40 ft/s^2).
constexpr std::int32_t kGravityFps16 = 0x500000;  // 80 ft/s^2 in 16.16.

}  // namespace

Ballistic ballistic_init(std::int32_t speed_fps, const fx::Vec3& position, Angle10 heading, Angle10 pitch) {
  const fx::Mat3 m = fx::mat3_from_euler(Angle10(-heading.tenths()), pitch, Angle10(0));
  Ballistic b;
  b.velocity = fx::transform(m, {0, 0, speed_fps << 4});
  b.time = 0;
  b.origin = position;
  b.position = position;
  return b;
}

void ballistic_step(Ballistic& b, std::int32_t dt) {
  b.time += dt;
  const std::int64_t t = b.time;
  const std::int64_t x = static_cast<std::int64_t>(b.velocity.x) * t;
  const std::int64_t z = static_cast<std::int64_t>(b.velocity.z) * t;
  const std::int64_t fall = static_cast<std::int64_t>(fx::mul(b.time, b.time)) * kHalfGravity;
  const std::int64_t y = static_cast<std::int64_t>(b.velocity.y) * t - fall;
  b.position = {b.origin.x + static_cast<std::int32_t>(x >> 16), b.origin.y + static_cast<std::int32_t>(y >> 16),
                b.origin.z + static_cast<std::int32_t>(z >> 16)};
}

std::int32_t ballistic_elevation_for_range(std::int32_t velocity_fps, std::int32_t range) {
  const std::int64_t num = static_cast<std::int64_t>(range / kUnitsPerFoot) * kGravityFps16;
  const std::int32_t sin2a = fx::int64_div(num, fx::imul(velocity_fps, velocity_fps));
  return fx::fixed_asin(sin2a) / 2;
}

std::int32_t ballistic_range_for_elevation(std::int32_t velocity_fps, Angle10 elevation) {
  const fx::SinCos sc = fx::angle_sincos(Angle10(elevation.tenths() * 2));
  const std::int64_t num = static_cast<std::int64_t>(fx::imul(velocity_fps, velocity_fps)) * sc.sin;
  return fx::int64_div(num, kGravityFps16);
}

}  // namespace sh
