// Class parameters of a few SHIPS.DAT / SUBS.DAT records for the
// simulation tests, as read from the original files (16.16 fields are
// raw).
#pragma once

#include "core/fixed.h"
#include "sim/dive.h"
#include "sim/motion.h"

namespace sh::test {

inline ShipMotionParams ship_params(std::int32_t max_power, std::int32_t length, std::int32_t beam,
                                    std::int32_t draft, std::int32_t displacement, std::int32_t c,
                                    std::int32_t lateral, std::int32_t astern, std::int32_t turn) {
  ShipMotionParams p;
  p.max_power = max_power;
  p.hull.length_ft = length;
  p.hull.beam_ft = beam;
  p.hull.draft_ft = draft;
  p.hull.displacement = displacement;
  p.hull.drag_cubic = Fixed16::from_raw(c);
  p.hull.drag_linear = Fixed16::from_raw(2097);
  p.hull.lateral_drag = Fixed16::from_raw(lateral);
  p.hull.astern_drag_factor = Fixed16::from_raw(astern);
  p.hull.turn_factor = Fixed16::from_raw(turn);
  return p;
}

// SHIPS.DAT FUSO: top_speed_kts "22.5".
inline ShipMotionParams fuso() { return ship_params(9000, 673, 94, 28, 35900, 426, 196608000, 1671168, 3879731); }
// SHIPS.DAT YAMATO: "27".
inline ShipMotionParams yamato() { return ship_params(14000, 862, 121, 32, 71659, 408, 196608000, 1671168, 3879731); }
// SHIPS.DAT FUBUKI: "38".
inline ShipMotionParams fubuki() { return ship_params(5900, 378, 34, 10, 2090, 164, 524288000, 688128, 1913651); }
// SHIPS.DAT LFREIGHT: "10".
inline ShipMotionParams lfreight() { return ship_params(1100, 375, 47, 15, 5180, 164, 524288000, 688128, 2569011); }

// SUBS.DAT GATO: surfaced "20.75" kn, submerged "8.75" kn.
inline SubClassParams gato() {
  SubClassParams s;
  s.length_ft = 312;
  s.beam_ft = 27;
  s.draft_ft = 15;
  s.test_depth_ft = 300;
  s.periscope_depth_ft = 65;
  s.radar_depth_ft = 48;
  s.periscope_height_ft = 68;
  s.bridge_height_ft = 40;
  s.awash_depth_ft = 30;
  s.noise_factor = 100;
  s.displacement_surfaced = 1825;
  s.ballast_neutral = 2410;
  s.ballast_max = 2420;
  s.diesel_hp = 5400;
  s.motor_hp = 2740;
  s.drag_cubic_surfaced = Fixed16::from_raw(351);
  s.drag_cubic_submerged = Fixed16::from_raw(688);
  s.drag_linear_surfaced = Fixed16::from_raw(2097);
  s.drag_linear_submerged = Fixed16::from_raw(2097);
  s.lateral_drag = Fixed16::from_raw(65536000);
  s.astern_drag_factor = Fixed16::from_raw(688128);
  s.turn_factor = Fixed16::from_raw(1389363);
  s.pitch_factor = Fixed16::from_raw(2503475);
  s.diesel_count = 4;
  s.motor_count = 4;
  s.fuel_capacity = 117000;
  s.fuel_use = Fixed16::from_raw(7209);
  s.power_surfaced = {0, 40, 93, 186, 320, 400, -372, -279, -186, -93, 10};
  s.power_submerged = {0, 50, 120, 190, 270, 400, -372, -279, -186, -93, 10};
  return s;
}

}  // namespace sh::test
