#include "sim/radar.h"

#include <cstddef>

#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"

namespace sh {

namespace {

// Ranges by SJ fit: [improved] {search, land echoes} (0x10e45c).
constexpr std::int32_t kSjRange[2] = {15000, 35000};
constexpr std::int32_t kSjLandRange[2] = {20000, 45000};
constexpr std::int32_t kSdRange = 17600;
constexpr std::int32_t kBlipSweeps = 8;
constexpr std::int32_t kHalfBeam = 76;  // +-7.6 deg

std::int32_t sj_level(std::uint32_t options) { return static_cast<std::int32_t>((options & 0x3ffff) >> 16); }
bool sj_improved(std::uint32_t options) { return (options & sensor_option::kSjRadarMask) > 0x10000; }
bool st_fitted(std::uint32_t options) {
  return (options & sensor_option::kSjRadarMask) == sensor_option::kStRadar;
}

// The depth the SJ works down to: the radar depth, the periscope depth for
// the ST.
std::int32_t sj_depth_limit(const PlayerSub& player, std::uint32_t options) {
  return st_fitted(options) ? player.sub().periscope_depth_ft : player.sub().radar_depth_ft;
}

// Returns the bearing difference folded into [-1800, 1800].
std::int32_t fold(std::int32_t d) {
  if (d > 1800) {
    d -= 3600;
  }
  if (d < -1800) {
    d += 3600;
  }
  return d;
}

// Returns the echo strength: rcs / (4 x range / range max) beyond a quarter
// of the range max.
std::int32_t echo_strength(std::int32_t rcs, std::int32_t range_yd, std::int32_t max_tenth) {
  const std::int32_t ratio = fx::mul(fx::div((range_yd / 10) << 16, max_tenth), 0x40000);
  return ratio > 0x10000 ? fx::div(rcs, ratio) : rcs;
}

// Finds the blip of `source`: a held beam drops a young one (seen fewer
// than 4 sweeps ago) so that it is added again, a turning beam keeps it one
// more sweep. Returns whether it is kept.
bool refresh_blip(std::vector<RadarBlip>& blips, std::int32_t source, bool hold) {
  for (std::size_t j = 0; j < blips.size(); ++j) {
    if (blips[j].source != source) {
      continue;
    }
    if (!hold) {
      ++blips[j].age;
      return true;
    }
    if (blips[j].age < 4) {
      blips.erase(blips.begin() + static_cast<std::ptrdiff_t>(j));
      return false;
    }
    return true;
  }
  return false;
}

}  // namespace

std::int32_t sj_fitted_range(std::uint32_t options) {
  const std::int32_t level = sj_level(options);
  return level == 0 ? 0 : kSjRange[level > 1 ? 1 : 0];
}

std::int32_t sd_fitted_range(std::uint32_t options) {
  return ((options & 0x7ffff) >> 18) != 0 ? kSdRange : 0;
}

void RadarSets::init(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env) {
  // radar_init (0x660a7)
  const std::uint32_t options = ctx.env.options;
  quiet_ = true;
  sj_ = Sj{};
  sj_.last_auto = env.auto_sj;
  sd_ = Sd{};
  sd_.last_auto = env.auto_sd;
  reports_ = Reports{};
  if ((options & sensor_option::kSjRadarMask) == 0) {
    player.systems()[static_cast<std::size_t>(PlayerSystem::kSjRadar)].flags = 2;
  }
  if ((options & sensor_option::kSdRadar) == 0) {
    player.systems()[static_cast<std::size_t>(PlayerSystem::kSdRadar)].flags = 2;
  }
  set_sj_power(ctx, player, true, true, false, false);
  set_sd_power(ctx, player, true, false);
}

void RadarSets::update(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env, std::int32_t dt) {
  // radar_update (0x661f5)
  sj_sweep(ctx, player, env, dt);
  sd_sweep(ctx, player, env);
  quiet_ = false;
}

void RadarSets::set_sj_power(SimContext& ctx, const PlayerSub& player, bool on, bool ppi, bool release_hold,
                             bool toggle) {
  // sj_radar_set_power (0x66b27)
  const std::uint32_t options = ctx.env.options;
  if ((options & sensor_option::kSjRadarMask) == 0) {
    return;
  }
  if (toggle) {
    on = sj_.mode == SjMode::kOff;
  }
  if (release_hold) {
    sj_.hold = false;
  }
  if ((options & sensor_option::kPpiScope) == 0) {
    ppi = false;
  }
  const SjMode mode = !on ? SjMode::kOff : (ppi ? SjMode::kPpi : SjMode::kAScope);
  if (mode == sj_.mode) {
    return;
  }
  if (player.depth_ft() <= sj_depth_limit(player, options) || mode == SjMode::kOff) {
    if ((sj_.mode == SjMode::kOff || mode == SjMode::kOff) && !quiet_) {
      const std::int32_t first = st_fitted(options) ? 47 : 43;
      ctx.crew_message(first + (mode == SjMode::kOff ? 1 : 0));
    }
    sj_.power_timer = 0xa0000;
    sj_.mode = mode;
    if (mode == SjMode::kOff) {
      sj_.beam = 0;
    }
  }
}

void RadarSets::set_sd_power(SimContext& ctx, const PlayerSub& player, bool on, bool toggle) {
  // sd_radar_set_power (0x67048)
  if ((ctx.env.options & sensor_option::kSdRadar) == 0) {
    return;
  }
  if (toggle) {
    on = !sd_.on;
  }
  if (player.depth_ft() <= player.sub().radar_depth_ft || !on) {
    sd_.power_timer = 0xa0000;
    sd_.on = on;
    if (!quiet_) {
      ctx.crew_message(on ? 45 : 46);
    }
  }
}

void RadarSets::sj_sweep(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env, std::int32_t dt) {
  // sj_radar_sweep (0x66210)
  sj_.nearest_yd = 0;
  const std::uint32_t options = ctx.env.options;
  if ((options & sensor_option::kSjRadarMask) == 0) {
    return;
  }
  sj_.power_timer -= dt;
  if (sj_.power_timer < 0) {
    sj_.power_timer = 0;
  }
  const std::int32_t limit = sj_depth_limit(player, options);
  if (env.auto_sj != sj_.last_auto) {
    sj_.auto_armed = true;
    sj_.last_auto = env.auto_sj;
  }
  if (limit < player.depth_ft()) {
    sj_.auto_armed = true;
  }
  if (env.auto_sj && sj_.auto_armed && sj_.mode == SjMode::kOff && player.depth_ft() <= limit) {
    sj_.auto_armed = false;
    quiet_ = sj_.power_timer > 0;
    set_sj_power(ctx, player, true, true, true, false);
    quiet_ = false;
  }
  if (sj_.mode == SjMode::kOff) {
    return;
  }
  if (limit < player.depth_ft()) {
    quiet_ = sj_.power_timer > 0;
    set_sj_power(ctx, player, false, false, false, false);
    quiet_ = false;
    return;
  }
  if (player.systems()[static_cast<std::size_t>(PlayerSystem::kSjRadar)].impaired()) {
    sj_.beam = 0;
    return;
  }
  reports_.sj_timer += dt;
  if (reports_.sj_timer > 0x3c0000) {
    reports_.sj_timer = 0x3c0000;
  }
  std::int32_t detections = 0;
  const std::int32_t range_max = kSjRange[sj_improved(options) ? 1 : 0];
  const std::int32_t max_tenth = (range_max / 10) << 16;
  const fx::Vec3 own = player.position();
  const Angle10 own_heading = player.motion().heading;
  static const std::vector<std::unique_ptr<Vehicle>> kNone;
  const auto& vehicles = ctx.sim != nullptr ? ctx.sim->vehicles() : kNone;
  std::int32_t remaining = dt;
  while (remaining != 0) {
    const std::int32_t step = remaining > 0x4000 ? 0x4000 : remaining;
    remaining -= step;
    if (!sj_.hold) {
      // 40 deg/s.
      sj_.beam += fx::mul(step, 0x1900000);
      while (sj_.beam > 0xe0fffff) {
        sj_.beam -= 0xe100000;
      }
    }
    // Ageing; the original skips the blip after a removed one.
    for (std::size_t i = 0; i < sj_.blips.size(); ++i) {
      if (--sj_.blips[i].age == 0) {
        sj_.blips.erase(sj_.blips.begin() + static_cast<std::ptrdiff_t>(i));
      }
    }
    std::int32_t nearest = range_max + 1;
    for (std::size_t i = 0; i < vehicles.size(); ++i) {
      Vehicle& v = *vehicles[i];
      if (!v.is_radar_target()) {
        continue;
      }
      const fx::Vec3 pos = v.position();
      if (pos.y >= 0x1f41) {
        continue;
      }
      if (env.grid != nullptr && land_on_line(*env.grid, {own.x, own.z}, {pos.x, pos.z}).land) {
        continue;
      }
      const std::int32_t dx = pos.x - own.x;
      const std::int32_t dz = pos.z - own.z;
      const auto range = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
      if (range > range_max) {
        continue;
      }
      const std::int32_t bearing = fx::angle_sub(fx::heading_from_delta(dx, dz), own_heading).tenths();
      const std::int32_t off = fold(bearing - fx::trunc(sj_.beam));
      if (off <= -kHalfBeam || off >= kHalfBeam) {
        continue;
      }
      const std::int32_t strength = echo_strength(v.radar_cross_section(own, own_heading), range, max_tenth);
      if (strength <= 0x1999) {
        continue;
      }
      const std::int32_t rounded = range / 10 * 10;
      if (rounded < nearest) {
        sj_.nearest_yd = rounded;
        nearest = rounded;
      }
      const auto source = static_cast<std::int32_t>(i);
      if (refresh_blip(sj_.blips, source, sj_.hold)) {
        continue;
      }
      if (!v.sinking()) {
        ++detections;
      }
      RadarBlip blip{source, Angle10(bearing), range, strength, kBlipSweeps, 0};
      blip.random = ctx.random.random_int(3600);
      sj_.blips.push_back(blip);
    }
  }
  sj_land_echoes(ctx, player, env);
  if (detections != 0) {
    if (Sensors* sensors = context_sensors(ctx)) {
      sensors->reset_quiet_time();
    }
    if (reports_.sj_timer > 0x1e0000) {
      reports_.sj_bearing = fx::trunc(sj_.beam / 10);
    }
    reports_.sj_timer = 0;
  }
}

void RadarSets::sj_land_echoes(SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env) {
  // sj_radar_land_echoes (0x6677d): on the radar station only, sampling
  // fewer cells far out (every (distance / step + 1)th) at low compression.
  const std::int32_t step = 6 - ctx.clock.effective_compression();
  if (env.station != SensorStation::kRadar || step <= 0 || env.grid == nullptr) {
    return;
  }
  const TerrainGrid& grid = *env.grid;
  const fx::Vec3 own = player.position();
  const std::int32_t cell = grid.cell_size();
  const std::int32_t column = own.x / cell;
  const std::int32_t row = (grid.height() * cell - own.z) / cell;
  if (column < 0 || column > grid.width() - 1 || row < 0 || row > grid.height() - 1) {
    return;
  }
  const std::int32_t echo_range = kSjLandRange[sj_improved(ctx.env.options) ? 1 : 0];
  const std::int32_t radius = (cell / 2 + echo_range * kUnitsPerYard) / cell;
  const std::int32_t column0 = column - radius < 0 ? 0 : column - radius;
  const std::int32_t row0 = row - radius < 0 ? 0 : row - radius;
  const std::int32_t column1 = column + radius > grid.width() ? grid.width() : column + radius;
  const std::int32_t row1 = row + radius > grid.height() ? grid.height() : row + radius;
  std::int32_t nearest = sj_.nearest_yd == 0 ? echo_range + 1 : sj_.nearest_yd;
  const Angle10 own_heading = player.motion().heading;
  std::int32_t row_off = row - row0;
  for (std::int32_t r = row0; r < row1; ++r, --row_off) {
    std::int32_t column_off = column - column0;
    for (std::int32_t c = column0; c < column1; ++c, --column_off) {
      const std::int32_t ac = fx::abs(column_off);
      const std::int32_t ar = fx::abs(row_off);
      if (ac % (ac / step + 1) != 0 || ar % (ar / step + 1) != 0 || grid.value(c, r) <= kCoastCell) {
        continue;
      }
      const std::int32_t dx = c * cell - own.x;
      const std::int32_t dz = cell * (grid.height() - r) - own.z;
      const auto range = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
      if (range > echo_range) {
        continue;
      }
      const std::int32_t bearing = fx::angle_sub(fx::heading_from_delta(dx, dz), own_heading).tenths();
      const std::int32_t off = fold(bearing - fx::trunc(sj_.beam));
      if (off <= -kHalfBeam || off >= kHalfBeam) {
        continue;
      }
      const std::int32_t rounded = range / 10 * 10;
      if (rounded < nearest) {
        sj_.nearest_yd = rounded;
        nearest = rounded;
      }
      const std::int32_t source = -(grid.width() * r + c);
      if (refresh_blip(sj_.blips, source, sj_.hold)) {
        continue;
      }
      std::int32_t strength = (ac > ar ? ac : ar) * 0x1a5e << ((4 - sj_.scale) & 0x1f);
      if (strength > 0x40000) {
        strength = 0x40000;
      }
      RadarBlip blip{source, Angle10(bearing), range, strength, 7, 0};
      blip.random = ctx.random.random_int(3600);
      sj_.blips.push_back(blip);
    }
  }
}

void RadarSets::sd_sweep(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env) {
  // sd_radar_sweep (0x66c5d): range only, aircraft and bomber groups above
  // 50 ft, one blip per plane of a group.
  sd_.nearest_yd = 0;
  const std::uint32_t options = ctx.env.options;
  if ((options & sensor_option::kSdRadar) == 0) {
    return;
  }
  sd_.blips.clear();
  if (sd_.power_timer < 0) {
    sd_.power_timer = 0;
  }
  const std::int32_t radar_depth = player.sub().radar_depth_ft;
  if (env.auto_sd != sd_.last_auto) {
    sd_.auto_armed = true;
    sd_.last_auto = env.auto_sd;
  }
  if (radar_depth < player.depth_ft()) {
    sd_.auto_armed = true;
  }
  if (env.auto_sd && sd_.auto_armed && !sd_.on && player.depth_ft() <= radar_depth) {
    sd_.auto_armed = false;
    quiet_ = sd_.power_timer > 0;
    set_sd_power(ctx, player, true, false);
    quiet_ = false;
  }
  if (!sd_.on) {
    return;
  }
  if (radar_depth < player.depth_ft()) {
    sd_.on = false;
    return;
  }
  if (player.systems()[static_cast<std::size_t>(PlayerSystem::kSdRadar)].impaired()) {
    return;
  }
  if (reports_.sd_timer > 0x3c0000) {
    reports_.sd_timer = 0x3c0000;
  }
  std::int32_t detections = 0;
  std::int32_t nearest = kSdRange + 1;
  const fx::Vec3 own = player.position();
  const Angle10 own_heading = player.motion().heading;
  if (ctx.sim == nullptr) {
    return;
  }
  const auto& vehicles = ctx.sim->vehicles();
  for (std::size_t i = 0; i < vehicles.size(); ++i) {
    Vehicle& v = *vehicles[i];
    const auto index = static_cast<std::int32_t>(i);
    if (!v.is_radar_target()) {
      continue;
    }
    const fx::Vec3 pos = v.position();
    if ((!v.is_aircraft() && v.plane_count() == 0) || pos.y <= 799) {
      continue;
    }
    const auto range = static_cast<std::int32_t>(fx::approx_distance(pos.x - own.x, pos.z - own.z) / kUnitsPerYard);
    if (range >= kSdRange + 1) {
      continue;
    }
    const std::int32_t strength =
        echo_strength(v.radar_cross_section(own, own_heading) * 5, range, (kSdRange / 10) << 16);
    if (strength <= 0x1999) {
      continue;
    }
    const std::int32_t rounded = range / 10 * 10;
    if (rounded < nearest) {
      sd_.nearest_yd = rounded;
      nearest = rounded;
    }
    sd_.blips.push_back({index, Angle10(0), range, strength, kBlipSweeps, 0});
    ++detections;
    const std::int32_t spread = strength / 5;
    std::int32_t source = 0;
    for (std::int32_t plane = 0; plane < v.plane_count() - 1; ++plane) {
      ++detections;
      RadarBlip blip{source + 100, Angle10(0), 0, 0, kBlipSweeps, 0};
      blip.range_yd = range + ctx.random.random_int(2000);
      blip.strength = strength + ctx.random.random_int(spread);
      sd_.blips.push_back(blip);
      source += index;
    }
  }
  if (detections != 0) {
    if (reports_.sd_timer > 0x1e0000) {
      reports_.sd_range = sd_.nearest_yd;
    }
    reports_.sd_timer = 0;
  }
}

}  // namespace sh
