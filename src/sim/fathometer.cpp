#include "sim/fathometer.h"

#include <memory>

#include "core/fixed_math.h"
#include "sim/ai_ship.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/sonar.h"

namespace sh {

namespace {

// The ping of an escort's sonar heard loud on the hydrophones, played for
// the sounder's.
constexpr std::int32_t kSfxPing = 0x32;
// The side the AI observers ignore.
constexpr std::int32_t kNeutralSide = 2;

}  // namespace

bool hears_sounding(const Ship& ship, const PlayerSub& player, const SimContext& ctx) {
  if (!ship.ai().cls.has_sonar || ship.sinking() || ship.removed() || ship.side() == player.side() ||
      ship.side() == kNeutralSide || player.depth_ft() >= ctx.env.thermal_layer_ft) {
    return false;
  }
  const fx::Vec3 at = ship.position();
  const fx::Vec3 own = player.position();
  const auto d = static_cast<std::int32_t>(fx::approx_distance(own.x - at.x, own.z - at.z) / kUnitsPerYard);
  if (d > sonar_active_range_yd(ship, ctx.env.sea_state)) {
    return false;
  }
  // Its own screws' arc astern, as in its sonar search.
  const std::int32_t relative =
      fx::angle_sub(fx::heading_from_delta(own.x - at.x, own.z - at.z), ship.motion().heading).tenths();
  if (relative >= 1701 && relative <= 1899) {
    return false;
  }
  return !sonar_deafened(ship, ctx);
}

void Fathometer::update(SimContext& ctx, PlayerSub& player) {
  if (!requested_) {
    return;
  }
  requested_ = false;
  if (!ctx.env.rules.fathometer_soundings) {
    return;
  }
  state_.sounded = true;
  state_.keel_clearance = player.motion().keel_clearance;
  ++state_.soundings;
  ctx.sfx(kSfxPing);
  if (ctx.sim == nullptr) {
    return;
  }
  const fx::Vec3 own = player.position();
  for (const std::unique_ptr<Vehicle>& v : ctx.sim->vehicles()) {
    if (v->class_id() != VehicleClass::kShip) {
      continue;
    }
    auto& ship = static_cast<Ship&>(*v);
    if (!hears_sounding(ship, player, ctx)) {
      continue;
    }
    // A point on the true bearing at half the hearing range.
    const fx::Vec3 at = ship.position();
    const fx::SinCos sc = fx::angle_sincos(fx::heading_from_delta(own.x - at.x, own.z - at.z));
    const std::int32_t reach = sonar_active_range_yd(ship, ctx.env.sea_state) / 2 * kUnitsPerYard;
    ship.receive_radar_intercept({at.x + fx::mul(sc.sin, reach), 0, at.z + fx::mul(sc.cos, reach)},
                                 &player);
  }
}

}  // namespace sh
