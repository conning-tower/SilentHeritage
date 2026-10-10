#include "sim/dive.h"

#include "sim/geometry.h"

namespace sh {

using namespace geom;

namespace {

constexpr std::int32_t kBallastRate = 0x6bd0000;     // 1725 x 100 tons per second.
constexpr std::int32_t kBlowAirRate = 0x58000;       // 5.5 % of compressed air per second.
constexpr std::int32_t kAirRecharge = 0x382;         // % per second, surfaced.
constexpr std::int32_t kBuoyancyGain = 0x230;        // Vertical drift per ballast unit.

// Returns the depth in feet of world height `y`.
std::int32_t depth_of(std::int32_t y) { return -y / kUnitsPerFoot; }

bool submerged(const SubClassParams& sub, const DiveState& dive) { return sub.draft_ft < dive.depth_ft; }

// Moves the ballast toward `target`; on reaching it the mode becomes idle
// (`sub_ballast_move_toward`, 0x8ce2b).
void ballast_move_toward(DiveState& dive, std::int32_t target, std::int32_t dt, const PlayerDiveFactors* player) {
  if (dive.ballast == target) {
    dive.mode = DiveState::Mode::kIdle;
    return;
  }
  std::int32_t rate = kBallastRate;
  if (player != nullptr) {
    rate = (3 - player->sailing_model) * kBallastRate;
  }
  const std::int32_t step = fx::trunc(fx::mul(rate, dt));
  if (target < dive.ballast) {
    dive.ballast -= step;
    if (target < dive.ballast) {
      return;
    }
  } else {
    dive.ballast += step;
    if (dive.ballast < target) {
      return;
    }
  }
  dive.mode = DiveState::Mode::kIdle;
  dive.ballast = target;
}

// Spends compressed air at `rate` (16.16 % per second); the novice sailing
// model blows for free (`sub_use_compressed_air`, 0x8cf4b).
void use_compressed_air(DiveState& dive, std::int32_t rate, std::int32_t dt, const PlayerDiveFactors* player) {
  if (dive.air_pct == 0 || (player != nullptr && player->sailing_model == 0)) {
    return;
  }
  dive.air_fraction -= fx::mul(rate, dt);
  while (dive.air_fraction < 0) {
    dive.air_fraction += 0x10000;
    --dive.air_pct;
  }
  if (dive.air_pct < 0) {
    dive.air_pct = 0;
  }
}

}  // namespace

HullParams sub_hull_params(const SubClassParams& sub, const DiveState& dive) {
  const bool under = submerged(sub, dive);
  HullParams hull;
  hull.length_ft = sub.length_ft;
  hull.beam_ft = sub.beam_ft;
  hull.draft_ft = sub.draft_ft;
  hull.displacement = (dive.ballast + dive.flood) / 100;
  hull.drag_cubic = under ? sub.drag_cubic_submerged : sub.drag_cubic_surfaced;
  hull.drag_linear = under ? sub.drag_linear_submerged : sub.drag_linear_surfaced;
  hull.lateral_drag = sub.lateral_drag;
  hull.astern_drag_factor = sub.astern_drag_factor;
  hull.turn_factor = sub.turn_factor;
  return hull;
}

void sub_dive_state_init(DiveState& dive, const MotionState& motion, const SubClassParams& sub) {
  dive.depth_ft = depth_of(motion.position.y);
  if (dive.depth_ft < sub.draft_ft) {
    dive.depth_ft = sub.draft_ft;
  }
  dive.mode = DiveState::Mode::kIdle;
  dive.ballast = (submerged(sub, dive) ? sub.ballast_neutral : sub.displacement_surfaced) * 100;
  dive.flood = 0;
  dive.planes = 0;
  dive.planes_angle = 0;
  dive.air_pct = 100;
  dive.air_fraction = 0;
  dive.ordered_depth_ft = dive.depth_ft;
}

void sub_dive_update(DiveState& dive, const SubClassParams& sub, MotionState& motion, Fixed16 dt_s,
                     const PlayerDiveFactors* player, std::int32_t sea_state, const SeaFloor& floor) {
  using Mode = DiveState::Mode;
  const std::int32_t dt = dt_s.raw();
  const fx::Vec3 body = fx::transform(motion.inverse, motion.velocity);
  const std::int32_t span = sub.ballast_max - sub.ballast_neutral;
  const std::int32_t light_limit = (sub.ballast_neutral - 3 * span) * 100;
  std::int32_t pump_target = (sub.ballast_neutral - span) * 100 - dive.flood;
  if (pump_target < light_limit) {
    pump_target = light_limit;
  }
  std::int32_t trim_target = sub.ballast_neutral * 100 - dive.flood;
  if (trim_target < light_limit) {
    trim_target = light_limit;
  }

  // Planes toward the ordered depth, proportional beyond a 2 ft dead band.
  std::int32_t planes = dive.planes;
  if (dive.ordered_depth_ft != -1) {
    const std::int32_t error = dive.depth_ft - fx::abs(dive.ordered_depth_ft);
    const std::int32_t s = fx::sign(error);
    std::int32_t band = s * 2;
    if (fx::abs(error) < fx::abs(band)) {
      band = error;
    }
    std::int32_t excess = error - band;
    if (error == 0) {
      planes = 0;
    } else {
      if (fx::abs(excess) > 100) {
        excess = s * 100;
      }
      planes = fx::abs(planes) * excess / 100;
      if (planes == 0) {
        planes = s;
      }
      if (body.z < 0) {
        planes = -planes;
      }
    }
    dive.planes = planes;
  }
  if (player != nullptr && planes > 0) {
    if (player->damaged_planes == 1 && fx::abs(planes) > 20) {
      planes = fx::sign(planes) * 20;
    } else if (player->damaged_planes == 2) {
      planes = 0;
    }
  }

  const std::int32_t pitch = motion.pitch.tenths() < 1801 ? motion.pitch.tenths() : motion.pitch.tenths() - 3600;
  std::int32_t pitch_error = planes - pitch;
  dive.planes_angle = 0;
  if (pitch_error != 0 && body.z != 0) {
    if (body.z < 0) {
      pitch_error = -pitch_error;
    }
    dive.planes_angle = pitch_error * 250 / 60;
  }
  if (body.z < 0) {
    planes = -planes;
  }

  if (planes == 0) {
    if (dive.mode == Mode::kIdle && submerged(sub, dive)) {
      dive.mode = Mode::kTrim;
    }
  } else {
    if (planes < 0 && dive.ballast < sub.ballast_max * 100) {
      dive.mode = Mode::kFlood;
    }
    if (planes > 0 && dive.mode != Mode::kSurface && dive.mode != Mode::kBlow && dive.mode != Mode::kPump &&
        pump_target < dive.ballast) {
      dive.mode = Mode::kPump;
    }
  }
  if (planes >= 0 && !submerged(sub, dive) && sub.displacement_surfaced * 100 < dive.ballast) {
    dive.mode = Mode::kSurface;
    dive.ordered_depth_ft = -1;
    dive.planes = 0;
  }

  switch (dive.mode) {
    case Mode::kIdle:
      break;
    case Mode::kTrim:
      if (trim_target != dive.ballast) {
        ballast_move_toward(dive, trim_target, dt, player);
      }
      break;
    case Mode::kFlood:
      ballast_move_toward(dive, (sub.ballast_neutral + span) * 100, dt, player);
      break;
    case Mode::kPump:
      if (pump_target < dive.ballast) {
        ballast_move_toward(dive, pump_target, dt, player);
        // As in the original, rising stops when the compressed air is out.
        if (dive.mode != Mode::kIdle && dive.air_pct < 1) {
          dive.mode = Mode::kIdle;
        }
      }
      break;
    case Mode::kBlow: {
      const std::int32_t blow_target = (sub.ballast_neutral - 6 * span) * 100;
      if (blow_target <= dive.ballast && dive.air_pct >= 1) {
        ballast_move_toward(dive, blow_target, dt, player);
        use_compressed_air(dive, kBlowAirRate, dt, player);
        if (dive.mode != Mode::kIdle && dive.air_pct < 1) {
          dive.mode = Mode::kIdle;
        }
      }
      break;
    }
    case Mode::kSurface:
      ballast_move_toward(dive, sub.displacement_surfaced * 100, dt, player);
      break;
  }

  // Pitch from the planes, like the yaw from the rudder.
  std::int32_t pitch_rate = 0;
  if (motion.keel_clearance < 1) {
    const fx::Point2 turned = fx::point_rotate_scale({0, fx::trunc(-body.z)}, {0, 0}, Angle10(dive.planes_angle));
    const std::int32_t vertical = fx::mul(fx::imul(turned.x, 0x10000), dt);
    const std::int32_t arm = fx::mul(fx::imul(sub.length_ft, 0x10000) >> 1, sub.pitch_factor.raw());
    pitch_rate = fx::div(vertical, arm);
  } else {
    motion.pitch_rad = 0;
    motion.pitch = Angle10(0);
  }
  if (motion.position.y < (sub.draft_ft + 1) * -kUnitsPerFoot || planes < 0) {
    std::int32_t rad = pitch_rate + motion.pitch_rad;
    while (rad >= kTwoPi16) {
      rad -= kTwoPi16;
    }
    while (rad < 0) {
      rad += kTwoPi16;
    }
    motion.pitch_rad = rad;
    motion.pitch = radians16_to_tenths(rad);
  } else {
    motion.pitch_rad = 0;
    motion.pitch = Angle10(0);
  }
  if (fx::abs(motion.speed_tenths) < 10) {
    motion.pitch_rad = 0;
    motion.pitch = Angle10(0);
  }

  // Buoyancy: a vertical drift from the ballast and flood against neutral.
  const std::int32_t heavy_floor = (sub.ballast_neutral - 7 * span) * 100;
  const std::int32_t flood_weight = heavy_floor <= dive.flood ? dive.flood * 100 : dive.flood;
  const std::int32_t ballast_weight = heavy_floor <= dive.ballast ? dive.ballast : heavy_floor;
  std::int32_t drift =
      fx::mul(fx::imul(sub.ballast_neutral * 100 - (ballast_weight + flood_weight), kBuoyancyGain), dt);
  if (motion.keel_clearance > 0 && drift < 0) {
    drift = 0;
  }
  const std::int32_t surface_y = sub.draft_ft * -kUnitsPerFoot;
  if (surface_y <= motion.position.y && drift > 0) {
    drift = 0;
  }
  motion.pos_y += drift;

  HullParams hull = sub_hull_params(sub, dive);
  const std::int32_t wave_pct = sub_wave_effect_pct(sea_state, dive.depth_ft, sub.bridge_height_ft);
  std::int32_t felt_sea = sea_state;
  if (player != nullptr && player->sailing_model < 2) {
    // Easier sailing models turn faster and feel less of the sea.
    hull.turn_factor = Fixed16::from_raw(static_cast<std::int32_t>(
        static_cast<std::uint32_t>(hull.turn_factor.raw()) * 2 / static_cast<std::uint32_t>(4 - player->sailing_model)));
    if (player->sailing_model < felt_sea) {
      felt_sea = player->sailing_model;
    }
  }
  motion_step(motion, hull, felt_sea, dt_s, wave_pct, floor);

  if (motion.position.y < surface_y) {
    if (motion.keel_clearance > 0) {
      motion.velocity = {};
    }
  } else {
    motion.position.y = surface_y;
    motion.pos_y = static_cast<std::int64_t>(surface_y) << 16;
  }
  dive.depth_ft = depth_of(motion.position.y);
  if (dive.depth_ft <= sub.draft_ft && dive.air_pct < 100) {
    dive.air_fraction += fx::mul(dt, kAirRecharge);
    while (dive.air_fraction > 0xffff) {
      dive.air_fraction -= 0x10000;
      ++dive.air_pct;
    }
    if (dive.air_pct > 100) {
      dive.air_pct = 100;
    }
  }
}

}  // namespace sh
