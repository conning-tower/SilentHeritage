// Visual sighting (docs/structures.md "Behaviour models"): the horizon range between
// an observer and a target reduced by light, weather and fog
// (`sighting_compute_range` 0x2c105, `sighting_check` 0x2c034), the player's
// lookouts (`player_sighting_check` 0x5e1b8) and the AI's
// (`ai_detect_target` 0x14a78, `ai_aircraft_detect_target` 0x15c85).
//
// Heights are world y values (16 units = 1 ft, up positive), positions
// world units, ranges and distances yards.
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "core/fixed_math.h"
#include "sim/line_of_sight.h"

namespace sh {

// What dims the lookouts' view.
struct SightingConditions {
  std::int32_t weather = 0;     // 0x125f60, 0..3: -15 % each.
  std::int32_t fog_pct = 0;     // 0x170f88: -fog %.
  std::int32_t light_pct = 100; // 0x170f84, 100 full day: -(100 - light) x 0.8 %.
};

// The sighting range for an observer and a target height.
struct SightingRange {
  std::int32_t range_yd = 0;  // The reduced, averaged horizon range.
  std::int32_t horizon_yd = 0;  // The observer's own horizon, unreduced.
};

// Returns the range at which a target `target_ft` tall is seen from
// `observer_ft` (both feet, 0 giving no range): the mean of the two
// horizons 1.145 x 2027 x sqrt(h) yd, less (100 - light) x 0.8 %, then
// weather x 15 %, then fog % (`sighting_compute_range`).
SightingRange sighting_compute_range(std::int32_t observer_ft, std::int32_t target_ft,
                                     const SightingConditions& conditions);

// `sighting_check`'s result: the distance (yd) when the target is seen,
// kOutOfRange or kBlockedByLand.
struct SightingCheck {
  static constexpr std::int32_t kOutOfRange = -1;
  static constexpr std::int32_t kBlockedByLand = -2;

  std::int32_t distance_yd = kOutOfRange;
  SightingRange range;

  bool seen() const { return distance_yd >= 0; }
};

// Returns whether a target at `target` (y = its top) is seen from
// `observer` (y = the eye): heights are taken in whole feet (at least 1;
// a non-positive height sees or shows nothing), the distance must be within
// the range and, when `grid` is given, land must not hide the target.
SightingCheck sighting_check(const SightingConditions& conditions, const TerrainGrid* grid, const fx::Vec3& observer,
                             const fx::Vec3& target);

// The player boat as an observer or a target of the lookouts (the SUBS.DAT
// heights and the boat's depth).
struct PlayerLookoutState {
  fx::Vec3 position;                  // 0x11a9c4..
  std::int32_t depth_ft = 0;          // 0x11ab0c
  bool periscope_raised = false;      // 0x11ac33
  std::int32_t periscope_eye_ft = 0;  // SUBS.DAT +172
  std::int32_t bridge_eye_ft = 0;     // SUBS.DAT +176, also the depth the bridge goes under (0x11b1d0)
  std::int32_t radar_depth_ft = 0;    // SUBS.DAT +168 (0x11b1c8)
  std::int32_t awash_depth_ft = 0;    // SUBS.DAT +180 (0x11b1d4)
  std::int32_t crew = 1;              // 0x11ac2b, 0 green .. 2 elite
};

// Returns the player's lookouts' sighting of a target at `target_x`/`target_z`
// with its top at `target_height` and `exposure` (vehicle slots +0x8c and
// +0x90): the eye is the periscope when raised, else the bridge, and
// nothing is seen with the bridge under and the periscope down. The range
// is divided by the exposure and cut by (2 - crew) x 10 %; a distance beyond
// it is not seen (`player_sighting_check`).
SightingCheck player_sighting_check(const PlayerLookoutState& player, const SightingConditions& conditions,
                                    const TerrainGrid* grid, std::int32_t target_x, std::int32_t target_height,
                                    std::int32_t target_z, std::int32_t exposure);

// An AI observer's sighting of a target (`ai_detect_target`).
struct AiSighting {
  std::int32_t distance_yd = SightingCheck::kOutOfRange;  // -1 when not seen.
  // The aspect term applied: false for the player boat below its bridge and
  // for torpedoes (what the ship AI treats as a submerged contact).
  bool aspect_applied = true;

  bool seen() const { return distance_yd >= 0; }
};

// The target side of an AI sighting.
struct AiSightingTarget {
  fx::Vec3 position;               // y = the top (slot +0x8c).
  Angle10 heading;
  const PlayerLookoutState* player = nullptr;  // Set when the target is the player boat.
  bool torpedo = false;
};

// Returns what a ship's (or submarine's) lookouts at `observer` see:
// the sighting range less 20 %, 2 x sea state % and (2 - crew) x 20 %
// (30 % for merchant crews); the player boat is /10 below radar depth, /5
// with the bridge under, /2 awash; a torpedo's range is crew / 12 of it
// (at least 50 yd); otherwise the aspect cuts up to 30 % bow-on
// (`ai_detect_target`).
AiSighting ai_detect_target(const SightingConditions& conditions, std::int32_t sea_state, const TerrainGrid* grid,
                            const fx::Vec3& observer, const AiSightingTarget& target, std::int32_t crew,
                            bool merchant_crew);

// Returns what an aircraft's crew at `observer` sees: less 30 %, 2 x sea
// state %, (2 - crew) x 20 %, the same player depth cuts, else an aspect
// term of up to 60 % (`ai_aircraft_detect_target`).
AiSighting ai_aircraft_detect_target(const SightingConditions& conditions, std::int32_t sea_state,
                                     const TerrainGrid* grid, const fx::Vec3& observer,
                                     const AiSightingTarget& target, std::int32_t crew);

// Returns the clear-day range (+5 %) from the bridge of a boat at
// `player_position` to a 5000 ft target, the distance at which aircraft
// sorties are launched (`sighting_compute_max_range` 0x15e14, 0x116230).
std::int32_t sighting_reference_range(const fx::Vec3& player_position, std::int32_t bridge_eye_ft);

}  // namespace sh
