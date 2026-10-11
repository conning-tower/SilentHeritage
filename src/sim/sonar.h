// The sonars (docs/structures.md "Behaviour models"): the player's hydrophones hearing
// ship screws and escort pings (`ship_hydrophone_check` 0x6fe2c), the
// player's own noise (`player_noise_level` 0x5c1c9). The escorts' sonar
// search is `ai_ship_sonar_search` (ai_ship.h), implemented in sonar.cpp.
#pragma once

#include <cstdint>

#include "sim/context.h"

namespace sh {

class PlayerSub;
class Ship;

// Returns the player boat's noise in %: its shaft power as % of the motors'
// power x the class noise factor (SUBS.DAT +184) / 100, a quarter when
// rigged for silent running (`player_noise_level`).
std::int32_t player_noise_level(const PlayerSub& player);

// Listens to `ship` from the player boat, submerged only: screws are heard
// out to (power % of the class) x 60 yd less 80 x the boat's noise (x5 in
// its baffles, x4 below the thermal layer), a pinging escort out to 10000 yd
// less a tenth of that noise. A first hearing posts the sound man's #41
// (high-speed) or #42 (low-speed screws) with the relative bearing, again
// after 3 minutes (`ship_hydrophone_check`, the ship's slot +0x58).
void ship_hydrophone_check(Ship& ship, SimContext& ctx);

// Returns whether a noise `noise` at `p` cancels a ship's sonar estimate at
// `estimate` (`ai_ship_sonar_search` 0x705f4): the ship at `ship` heading
// `heading`, its beam `beam` (tenths off the bow).
bool sonar_noise_cancels(const fx::Vec3& ship, Angle10 heading, std::int32_t beam, const fx::Vec3& estimate,
                         const fx::Vec3& p, std::int32_t noise, const MissionRules& rules);

}  // namespace sh
