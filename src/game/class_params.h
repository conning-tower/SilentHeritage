// The simulation's class parameter blocks filled from the `DATA/*.DAT`
// records. The original loaders copy the records as-is
// (`load_ships_dat` 0x50f72, `load_subs_dat` 0x50e53) and the motion code
// reads them by offset; these adapters pick the same fields.
#pragma once

#include "assets/dat_tables.h"
#include "sim/context.h"
#include "sim/dive.h"
#include "sim/motion.h"

namespace sh {

// Returns the hull block of a SHIPS.DAT class (record +176..+232), the one
// `motion_step` reads for ships, enemy submarines and their AI.
HullParams ship_hull_params(const ShipClass& ship);

// Returns the motion parameters of a SHIPS.DAT class: its hull and the
// engine power cap +112.
ShipMotionParams ship_motion_params(const ShipClass& ship);

// Returns the submarine parameters of a SUBS.DAT class (record +148..+364),
// read by the dive model and the own-boat systems.
SubClassParams sub_class_params(const SubClass& sub);

// Returns them under the extended `rules` (not in the original): with
// `realistic_depths`, the class's depths of `realistic_sub_depths`.
SubClassParams sub_class_params(const SubClass& sub, const MissionRules& rules);

// The depths of a boat class (SUBS.DAT +164..+176, feet).
struct SubDepths {
  std::int32_t periscope_depth_ft = 0;  // +164
  std::int32_t radar_depth_ft = 0;      // +168
  std::int32_t periscope_eye_ft = 0;    // +172
  std::int32_t bridge_eye_ft = 0;       // +176, also the lookouts' eye
};

// Returns the depths of class `sub` (by its index name) of the Historical
// Realism Patch (1999), the extended REALISTIC DEPTHS: the boats' periscope
// and radar depths shallower than SUBS.DAT's 65 and 48 ft, the bridge
// awash at 24 to 28 ft instead of 38 to 40. SUBS.DAT's own values where the
// patch keeps them, and for a class it does not know.
SubDepths realistic_sub_depths(const SubClass& sub);

// Returns the deck-gun rounds a SUBS.DAT class carries (+276, which the
// player init copies to 0x11b100).
std::int32_t sub_deck_gun_rounds(const SubClass& sub);

}  // namespace sh
