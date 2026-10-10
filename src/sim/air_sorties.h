// The aircraft take-offs (`air_sorties_tick` 0x15ed1 and
// `air_sorties_relaunch` 0x15f58), run by the mission frame after the
// vehicles: once more than 4 hundredths of game time have gathered, each
// landed aircraft with sorties left counts its relaunch timer down and, at
// 0, takes off at the reference range from the player (the clear-day
// sighting range of a 5000 ft target, `sighting_reference_range`) on the
// bearing of its station +- rand(150) tenths; an aircraft without sorties
// is removed. Nothing takes off unless the light is at least 50 %, the fog
// under 21 % and neither got worse since the previous tick.
#pragma once

#include <cstdint>

#include "sim/context.h"

namespace sh {

class AirSorties {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Sets the take-off range (yd) and the light/fog snapshot to 200/200
  // (`sighting_compute_max_range`, 0x15e14, run once the units exist).
  void reset(std::int32_t reference_range_yd);
  // `air_sorties_tick`: gathers the frame's game time and relaunches.
  void tick(SimContext& ctx);

  std::int32_t reference_range_yd() const { return range_yd_; }

 private:
  // `air_sorties_relaunch` for `dt` (16.16 s).
  void relaunch(SimContext& ctx, std::int32_t dt, std::int32_t light, std::int32_t fog);

  std::int32_t range_yd_ = 0;     // 0x116230
  std::int32_t light_ = 200;      // 0x116234
  std::int32_t fog_ = 200;        // 0x116238
  std::int32_t hundredths_ = 0;   // 0x11623c
};

}  // namespace sh
