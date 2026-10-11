#include "sim/radar_emissions.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/fixed_math.h"
#include "sim/ai_ship.h"
#include "sim/aircraft.h"
#include "sim/enemy_sub.h"
#include "sim/line_of_sight.h"
#include "sim/player_sub.h"
#include "sim/radar.h"
#include "sim/ship.h"
#include "sim/simulation.h"

namespace sh {

namespace {

// The crew message "Radar emissions detected on APR-1" (speech APR1.RAW).
constexpr std::int32_t kMsgApr1 = 61;
constexpr std::int32_t kMinute = 0x3c0000;     // 16.16 s
constexpr std::int32_t kHalfMinute = 0x1e0000;
// The sets' own ranges (yards): the ships' metric and 10 cm sets, the
// aircraft's; heard up to 3/2 of them.
constexpr std::int32_t kShipMetricRange = 20000;
constexpr std::int32_t kShipCentimetricRange = 16000;
constexpr std::int32_t kAircraftRange = 24000;
// The boat's sets as the escorts' detectors hear them.
constexpr std::int32_t kSdRange = 17600;
// The light and fog that make a ship transmit.
constexpr std::int32_t kNightLightPct = 50;
constexpr std::int32_t kFogPct = 30;
// The detectors' bearing error (tenths of a degree): the widest on the
// first intercept, the narrowest after `kDetectorSettle` (16.16 s, 3 min)
// of radar heard without a break.
constexpr std::int32_t kDetectorWidest = 450;
constexpr std::int32_t kDetectorNarrowest = 100;
constexpr std::int32_t kDetectorSettle = 180 * 0x10000;
// The side the AI observers ignore.
constexpr std::int32_t kNeutralSide = 2;

constexpr std::int32_t month_index(std::int32_t month, std::int32_t year) { return year * 12 + month; }

// Returns the integer square root of `v`.
std::int64_t isqrt(std::int64_t v) {
  std::int64_t r = 0;
  for (std::int64_t bit = std::int64_t{1} << 40; bit != 0; bit >>= 2) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
  }
  return r;
}

std::int32_t distance_yd(const fx::Vec3& a, const fx::Vec3& b) {
  return static_cast<std::int32_t>(fx::approx_distance(a.x - b.x, a.z - b.z) / kUnitsPerYard);
}

// The height of the boat's mast top above the sea (feet, at most 1 under).
std::int32_t boat_mast_ft(const PlayerSub& player) {
  return std::max(1, (player.position().y + player.sub().periscope_height_ft * kUnitsPerFoot) / kUnitsPerFoot);
}

bool impaired(const PlayerSub& player, PlayerSystem system) {
  return player.systems()[static_cast<std::size_t>(system)].impaired();
}

// A Japanese set transmitting now: its bands, own range and antenna height.
struct Transmitter {
  RadarBands bands;
  std::int32_t range_yd = 0;
  std::int32_t height_ft = 1;
};

bool ship_transmits(std::int32_t alert_level, const SensorEnvironment& env) {
  return alert_level != 0 || env.light_pct < kNightLightPct || env.fog_pct >= kFogPct;
}

std::int32_t ship_range(const RadarBands& bands) {
  return bands.metric ? kShipMetricRange : kShipCentimetricRange;
}

// Returns what `v` transmits now (no bands: nothing).
Transmitter transmitter(const Vehicle& v, std::int32_t month, std::int32_t year, const SensorEnvironment& env) {
  Transmitter t;
  if (v.sinking() || v.removed()) {
    return t;
  }
  const std::int32_t top_ft = std::max(1, v.visual_height() / kUnitsPerFoot);
  switch (v.class_id()) {
    case VehicleClass::kShip: {
      const auto& ship = static_cast<const Ship&>(v);
      if (ship_transmits(ship.alert().level, env)) {
        t.bands = ship_radar_bands(ship.ai().cls.type, month, year);
        t.range_yd = ship_range(t.bands);
        t.height_ft = top_ft;
      }
      break;
    }
    case VehicleClass::kEnemySub: {
      const auto& sub = static_cast<const EnemySub&>(v);
      // Only with its mast above the water.
      if (sub.radar_cross_section(sub.position(), Angle10(0)) != 0 && ship_transmits(sub.ai().alert.level, env)) {
        t.bands = ship_radar_bands(ShipType::kSubmarine, month, year);
        t.range_yd = ship_range(t.bands);
        t.height_ft = top_ft;
      }
      break;
    }
    case VehicleClass::kAircraft: {
      const auto& plane = static_cast<const Aircraft&>(v);
      if (plane.airborne() && !plane.crashing() && plane.setup().type != nullptr) {
        t.bands = aircraft_radar_bands(plane.setup().type->role, month, year);
        t.range_yd = kAircraftRange;
        t.height_ft = std::max(1, plane.position().y / kUnitsPerFoot);
      }
      break;
    }
    default:
      break;
  }
  return t;
}

// Returns a fixed hash of `ship` (its start point and type) and `salt`.
std::uint32_t ship_hash(const Ship& ship, std::uint32_t salt) {
  const Route& route = ship.setup().route;
  std::uint32_t h = static_cast<std::uint32_t>(route.x[0]) * 73856093u;
  h ^= static_cast<std::uint32_t>(route.z[0]) * 19349663u;
  h ^= static_cast<std::uint32_t>(ship.ai().cls.type) * 83492791u;
  h ^= salt * 2654435761u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return h;
}

bool land_between(const SensorEnvironment& env, const fx::Vec3& a, const fx::Vec3& b) {
  return env.grid != nullptr && land_on_line(*env.grid, {a.x, a.z}, {b.x, b.z}).land;
}


}  // namespace

RadarBands ship_radar_bands(ShipType type, std::int32_t month, std::int32_t year) {
  const std::int32_t now = month_index(month, year);
  const auto from = [now](std::int32_t m, std::int32_t y) { return now >= month_index(m, y); };
  switch (type) {
    case ShipType::kCarrier:
    case ShipType::kEscortCarrier:
    case ShipType::kSeaplaneCarrier:
    case ShipType::kBattleship:
    case ShipType::kHybridBattleship:
    case ShipType::kHeavyCruiser:
    case ShipType::kLightCruiser:
      // Type 21 from August 1942, Type 22 from January 1944.
      return {from(7, 12), from(0, 14)};
    case ShipType::kDestroyer:
      // Type 13 from January 1944, Type 22 from June 1944.
      return {from(0, 14), from(5, 14)};
    case ShipType::kDestroyerEscort:
    case ShipType::kKaibokan:
      return {from(5, 14), from(8, 14)};
    case ShipType::kSubmarine:
      return {from(6, 14), from(6, 14)};
    default:
      return {};
  }
}

RadarBands aircraft_radar_bands(AircraftRole role, std::int32_t month, std::int32_t year) {
  // The H-6 on the patrol bombers from January 1944.
  const bool fitted = (role == AircraftRole::kLevelBomber || role == AircraftRole::kTorpedoBomber) &&
                      month_index(month, year) >= month_index(0, 14);
  return {fitted, false};
}

std::int32_t radio_horizon_yd(std::int32_t a_ft, std::int32_t b_ft) {
  // sqrt in hundredths.
  const std::int64_t a = isqrt(std::int64_t{std::max(a_ft, 1)} * 10000);
  const std::int64_t b = isqrt(std::int64_t{std::max(b_ft, 1)} * 10000);
  return static_cast<std::int32_t>(2490 * (a + b) / 100);
}

Apr1Signal apr1_listen(const SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env) {
  Apr1Signal signal;
  if (player.depth_ft() > player.sub().radar_depth_ft ||
      ((ctx.env.options & sensor_option::kSdRadar) != 0 && impaired(player, PlayerSystem::kSdRadar)) ||
      ctx.sim == nullptr) {
    return signal;
  }
  const std::int32_t month = ctx.clock.month();
  const std::int32_t year = ctx.clock.year();
  const fx::Vec3 own = player.position();
  const std::int32_t mast_ft = boat_mast_ft(player);
  for (const std::unique_ptr<Vehicle>& v : ctx.sim->vehicles()) {
    if (v.get() == &player || v->side() == player.side() || v->side() == kNeutralSide) {
      continue;
    }
    const Transmitter t = transmitter(*v, month, year, env);
    if (!t.bands.any()) {
      continue;
    }
    const fx::Vec3 at = v->position();
    const std::int32_t limit = std::min(t.range_yd * 3 / 2, radio_horizon_yd(t.height_ft, mast_ft));
    const std::int32_t d = distance_yd(own, at);
    if (d > limit || land_between(env, own, at)) {
      continue;
    }
    signal.bands.metric = signal.bands.metric || t.bands.metric;
    signal.bands.centimetric = signal.bands.centimetric || t.bands.centimetric;
    signal.strength = std::max(signal.strength, std::min(3, 1 + 3 * (limit - d) / std::max(limit, 1)));
  }
  return signal;
}

std::string apr1_report_suffix(const Apr1Signal& signal) {
  static constexpr const char* kWords[] = {"", ": faint", ": fair", ": strong"};
  return kWords[std::clamp(signal.strength, 0, 3)];
}

void Apr1Receiver::update(SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env, std::int32_t dt) {
  if (!ctx.env.rules.apr1 || (ctx.env.options & sensor_option::kApr1Detector) == 0) {
    return;
  }
  state_.since_report = std::min(state_.since_report + dt, kMinute);
  const Apr1Signal signal = apr1_listen(ctx, player, env);
  if (signal.strength == 0) {
    state_.silence = std::min(state_.silence + dt, kMinute);
    if (state_.silence >= kMinute) {
      state_.reported = 0;
    }
    return;
  }
  const bool fresh = state_.silence >= kMinute;
  state_.silence = 0;
  if (fresh || (signal.strength > state_.reported && state_.since_report >= kHalfMinute)) {
    ctx.crew_message(kMsgApr1, apr1_report_suffix(signal));
    ctx.clock.reset_compression();
    state_.reported = signal.strength;
    state_.since_report = 0;
  }
}

bool escort_has_radar_detector(const Ship& ship, std::int32_t month, std::int32_t year) {
  const std::int32_t now = month_index(month, year);
  std::uint32_t share = 0;
  if (now >= month_index(0, 15)) {
    share = 75;
  } else if (now >= month_index(6, 14)) {
    share = 66;
  } else if (now >= month_index(0, 14)) {
    share = 33;
  }
  if (share == 0 || !ship.is_escort()) {
    return false;
  }
  return ship_hash(ship, 0) % 100 < share;
}

std::int32_t detector_error_limit(std::int32_t heard) {
  const std::int32_t t = std::clamp(heard, 0, kDetectorSettle);
  return kDetectorWidest - static_cast<std::int32_t>(std::int64_t{kDetectorWidest - kDetectorNarrowest} * t /
                                                      kDetectorSettle);
}

std::int32_t detector_bearing_error(const Ship& ship, std::int32_t minutes_elapsed, std::int32_t heard) {
  const auto share =
      static_cast<std::int32_t>(ship_hash(ship, static_cast<std::uint32_t>(minutes_elapsed / 5) + 1) % 2001) - 1000;
  return share * detector_error_limit(heard) / 1000;
}

void EscortDetectors::update(SimContext& ctx, PlayerSub& player, const RadarSets& radar,
                             const SensorEnvironment& env, std::int32_t dt) {
  if (!ctx.env.rules.detectable_radar || ctx.sim == nullptr) {
    return;
  }
  const std::uint32_t options = ctx.env.options;
  const std::int32_t month = ctx.clock.month();
  const std::int32_t year = ctx.clock.year();
  // The farthest the boat's transmissions are heard.
  std::int32_t heard_yd = 0;
  if (radar.sd().on && (options & sensor_option::kSdRadar) != 0 && !impaired(player, PlayerSystem::kSdRadar) &&
      player.depth_ft() <= player.sub().radar_depth_ft) {
    heard_yd = kSdRange * 3 / 2;
  }
  if (radar.sj().mode != SjMode::kOff && !impaired(player, PlayerSystem::kSjRadar) &&
      !sj_through_periscope(player, options) && month_index(month, year) >= month_index(6, 14)) {
    heard_yd = std::max(heard_yd, sj_fitted_range(options) * 3 / 2);
  }
  if (heard_yd == 0) {
    state_.heard = 0;
    return;
  }
  bool heard = false;
  const fx::Vec3 own = player.position();
  const std::int32_t mast_ft = boat_mast_ft(player);
  for (const std::unique_ptr<Vehicle>& v : ctx.sim->vehicles()) {
    if (v->class_id() != VehicleClass::kShip || v->sinking() || v->removed() || v->side() == player.side() ||
        v->side() == kNeutralSide) {
      continue;
    }
    auto& ship = static_cast<Ship&>(*v);
    if (!escort_has_radar_detector(ship, month, year)) {
      continue;
    }
    const fx::Vec3 at = ship.position();
    const std::int32_t limit =
        std::min(heard_yd, radio_horizon_yd(std::max(1, ship.visual_height() / kUnitsPerFoot), mast_ft));
    if (distance_yd(own, at) > limit || land_between(env, own, at)) {
      continue;
    }
    heard = true;
    // A point on the coarse bearing, at half the hearing range.
    const std::int32_t error = detector_bearing_error(ship, ctx.clock.minutes_elapsed(), state_.heard);
    const Angle10 bearing = fx::angle_add(fx::heading_from_delta(own.x - at.x, own.z - at.z), Angle10(error + 3600));
    const fx::SinCos sc = fx::angle_sincos(bearing);
    const std::int32_t reach = heard_yd / 2 * kUnitsPerYard;
    ship.receive_radar_intercept({at.x + fx::mul(sc.sin, reach), 0, at.z + fx::mul(sc.cos, reach)}, &player);
  }
  state_.heard = heard ? std::min(state_.heard + dt, kDetectorSettle) : 0;
}

}  // namespace sh
