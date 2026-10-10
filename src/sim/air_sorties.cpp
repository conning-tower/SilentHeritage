#include "sim/air_sorties.h"

#include "sim/aircraft.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

void AirSorties::reset(std::int32_t reference_range_yd) {
  range_yd_ = reference_range_yd;
  light_ = 200;
  fog_ = 200;
  hundredths_ = 0;
}

void AirSorties::tick(SimContext& ctx) {
  // air_sorties_tick (0x15ed1)
  hundredths_ += ctx.clock.frame_ticks();
  if (hundredths_ <= 4) {
    return;
  }
  const std::int32_t dt = fx::div(hundredths_, 100);
  hundredths_ = 0;
  const Sensors* sensors = context_sensors(ctx);
  const std::int32_t light = sensors != nullptr ? sensors->environment().light_pct : 100;
  const std::int32_t fog = sensors != nullptr ? sensors->environment().fog_pct : 0;
  relaunch(ctx, dt, light, fog);
  light_ = light;
  fog_ = fog;
}

void AirSorties::relaunch(SimContext& ctx, std::int32_t dt, std::int32_t light, std::int32_t fog) {
  // air_sorties_relaunch (0x15f58)
  if (light < light_ || fog_ < fog || light < 50 || fog > 20 || ctx.sim == nullptr) {
    return;
  }
  const PlayerSub* player = context_player(ctx);
  const fx::Vec3 own = player != nullptr ? player->position() : fx::Vec3{};
  const auto& vehicles = ctx.sim->vehicles();
  const std::size_t count = vehicles.size();  // Not the ones spawned meanwhile.
  for (std::size_t i = 0; i < count; ++i) {
    auto* aircraft = dynamic_cast<Aircraft*>(vehicles[i].get());
    if (aircraft == nullptr || aircraft->airborne()) {
      continue;
    }
    AircraftSetup& s = aircraft->setup();
    if (s.sorties < 1) {
      aircraft->remove();
      continue;
    }
    aircraft->set_relaunch_timer(aircraft->relaunch_timer() - dt);
    if (aircraft->relaunch_timer() >= 1) {
      continue;
    }
    const bool path = s.mission == AircraftMissionCode::kFollowPath;
    const std::int32_t sx = path ? s.route.x[0] : s.orbit_x;
    const std::int32_t sz = path ? s.route.z[0] : s.orbit_z;
    const Angle10 bearing = fx::heading_from_delta(sx - own.x, sz - own.z);
    aircraft->set_relaunch_timer(0);
    // The range is halved into 15 bits for the rotation, then doubled back.
    std::int32_t range = range_yd_;
    std::int32_t shifts = 0;
    while (fx::abs(range) >= 0x8000) {
      range >>= 1;
      ++shifts;
    }
    Angle10 angle;
    if (ctx.random.random_int(2) == 0) {
      angle = fx::angle_sub(bearing, Angle10(ctx.random.random_int(150)));
    } else {
      angle = fx::angle_add(bearing, Angle10(ctx.random.random_int(150)));
    }
    fx::Point2 p = fx::point_rotate_scale({0, -range}, {0, 0}, angle);
    for (; shifts > 0; --shifts) {
      p.x <<= 1;
      p.y <<= 1;
    }
    const std::int32_t x = own.x + p.x * kUnitsPerYard;
    const std::int32_t z = own.z - p.y * kUnitsPerYard;
    const std::int32_t ground = std::max(ctx.floor.height_at(x, z), 0);
    aircraft->take_off(ctx, {x, ground + 0x12c0, z}, range_yd_);
  }
}

}  // namespace sh
