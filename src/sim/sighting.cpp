#include "sim/sighting.h"

#include <cmath>

#include "sim/geometry.h"

namespace sh {

namespace {

// Returns the horizon range of a height in feet as 16.16 yards: sqrt(h) in
// 16.16 rounded on the x87, then x 1.145 (0x1251f) x 2027 (0x7eb0000).
std::int32_t horizon16(std::int32_t feet) {
  const auto root = static_cast<std::int32_t>(std::floor(std::sqrt(static_cast<double>(feet)) * 65536.0 + 0.5));
  return fx::mul(fx::mul(0x1251f, root), 0x7eb0000);
}

// Returns a height in whole feet for the sighting range: h / 16 rounded
// toward zero, at least 1.
std::int32_t height_ft(std::int32_t height) {
  const std::int32_t ft = height / kUnitsPerFoot;
  return ft == 0 ? 1 : ft;
}

// The player-boat depth cuts shared by the AI observers; returns whether the
// boat is submerged enough to skip the aspect term.
bool apply_player_depth(const PlayerLookoutState& player, std::int32_t& range) {
  if (player.radar_depth_ft < player.depth_ft) {
    range /= 10;
    return true;
  }
  if (player.bridge_eye_ft < player.depth_ft) {
    range /= 5;
    return true;
  }
  if (player.awash_depth_ft < player.depth_ft) {
    range /= 2;
  }
  return false;
}

// The aspect term: |90 deg - the target's aspect to the observer| in tenths.
std::int32_t aspect_off_beam(const fx::Vec3& observer, const AiSightingTarget& target) {
  const Angle10 bearing = fx::heading_from_delta(observer.x - target.position.x, observer.z - target.position.z);
  std::int32_t aspect = fx::angle_sub(bearing, target.heading).tenths();
  if (aspect > 1800) {
    aspect = fx::angle_sub(Angle10(3600), Angle10(aspect)).tenths();
  }
  return fx::abs(900 - aspect);
}

}  // namespace

SightingRange sighting_compute_range(std::int32_t observer_ft, std::int32_t target_ft,
                                     const SightingConditions& conditions) {
  // sighting_compute_range (0x2c105)
  SightingRange result;
  if (observer_ft == 0) {
    return result;
  }
  const std::int32_t observer = horizon16(observer_ft);
  result.horizon_yd = static_cast<std::int32_t>(static_cast<std::uint32_t>(observer) >> 16);
  if (target_ft == 0) {
    return result;
  }
  const std::int32_t target = horizon16(target_ft);
  std::int32_t r = (static_cast<std::int32_t>(static_cast<std::uint32_t>(target) >> 16) + result.horizon_yd) / 2;
  r -= fx::imul(fx::imul(100 - conditions.light_pct, r), 0x50) / 10000;
  r -= fx::imul(fx::imul(r, conditions.weather), 0xf) / 100;
  r -= fx::imul(r, conditions.fog_pct) / 100;
  result.range_yd = r;
  return result;
}

SightingCheck sighting_check(const SightingConditions& conditions, const TerrainGrid* grid, const fx::Vec3& observer,
                             const fx::Vec3& target) {
  // sighting_check (0x2c034)
  SightingCheck result;
  if (observer.y <= 0 || target.y <= 0) {
    return result;
  }
  const std::int32_t target_ft = height_ft(target.y);
  result.range = sighting_compute_range(height_ft(observer.y), target_ft, conditions);
  const std::uint32_t distance = fx::approx_distance(target.x - observer.x, target.z - observer.z) / kUnitsPerYard;
  if (static_cast<std::int32_t>(distance) > result.range.range_yd) {
    return result;
  }
  if (grid != nullptr &&
      los_blocked_by_land(*grid, {observer.x, observer.z}, {target.x, target.z}, target_ft * kUnitsPerFoot)) {
    result.distance_yd = SightingCheck::kBlockedByLand;
    return result;
  }
  result.distance_yd = static_cast<std::int32_t>(distance);
  return result;
}

SightingCheck player_sighting_check(const PlayerLookoutState& player, const SightingConditions& conditions,
                                    const TerrainGrid* grid, std::int32_t target_x, std::int32_t target_height,
                                    std::int32_t target_z, std::int32_t exposure) {
  // player_sighting_check (0x5e1b8)
  const std::int32_t eye_ft = player.periscope_raised ? player.periscope_eye_ft : player.bridge_eye_ft;
  const fx::Vec3 eye{player.position.x, player.position.y + eye_ft * kUnitsPerFoot, player.position.z};
  SightingCheck result;
  if (player.depth_ft < player.bridge_eye_ft || player.periscope_raised) {
    result = sighting_check(conditions, grid, eye, {target_x, target_height, target_z});
  }
  result.range.range_yd /= exposure;
  if (result.seen()) {
    result.range.range_yd -= (2 - player.crew) * result.range.range_yd * 10 / 100;
    if (result.range.range_yd < result.distance_yd) {
      result.distance_yd = SightingCheck::kOutOfRange;
    }
  }
  return result;
}

AiSighting ai_detect_target(const SightingConditions& conditions, std::int32_t sea_state, const TerrainGrid* grid,
                            const fx::Vec3& observer, const AiSightingTarget& target, std::int32_t crew,
                            bool merchant_crew) {
  // ai_detect_target (0x14a78)
  AiSighting result;
  const SightingCheck check = sighting_check(conditions, grid, observer, target.position);
  if (!check.seen()) {
    return result;
  }
  std::int32_t range = check.range.range_yd;
  range -= range * 20 / 100;
  range -= range * sea_state * 2 / 100;
  range -= (2 - crew) * range * (merchant_crew ? 30 : 20) / 100;
  if (target.player != nullptr && apply_player_depth(*target.player, range)) {
    result.aspect_applied = false;
  }
  if (target.torpedo) {
    range = range * crew / 12;
    if (range == 0) {
      range = 50;
    }
    result.aspect_applied = false;
  }
  if (result.aspect_applied) {
    range -= range * 30 * (aspect_off_beam(observer, target) / 10 + 1) / 9000;
  }
  if (range > check.distance_yd) {
    result.distance_yd = check.distance_yd;
  }
  return result;
}

AiSighting ai_aircraft_detect_target(const SightingConditions& conditions, std::int32_t sea_state,
                                     const TerrainGrid* grid, const fx::Vec3& observer,
                                     const AiSightingTarget& target, std::int32_t crew) {
  // ai_aircraft_detect_target (0x15c85)
  AiSighting result;
  const SightingCheck check = sighting_check(conditions, grid, observer, target.position);
  if (!check.seen()) {
    return result;
  }
  std::int32_t range = check.range.range_yd;
  range -= range * 30 / 100;
  range -= range * sea_state * 2 / 100;
  range -= (2 - crew) * range * 20 / 100;
  if (target.player != nullptr && apply_player_depth(*target.player, range)) {
    result.aspect_applied = false;
  }
  if (result.aspect_applied) {
    range -= range * 60 * (aspect_off_beam(observer, target) / 10 + 1) / 9000;
  }
  if (range > check.distance_yd) {
    result.distance_yd = check.distance_yd;
  }
  return result;
}

std::int32_t sighting_reference_range(const fx::Vec3& player_position, std::int32_t bridge_eye_ft) {
  // sighting_compute_max_range (0x15e14): no weather or fog, full light,
  // a 5000 ft target at the map origin.
  const SightingConditions clear;
  const std::int32_t range =
      sighting_check(clear, nullptr, {player_position.x, bridge_eye_ft * kUnitsPerFoot, player_position.z},
                     {0, 80000, 0})
          .range.range_yd;
  return range + range / 20;
}

}  // namespace sh
