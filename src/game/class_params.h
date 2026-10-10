// The simulation's class parameter blocks filled from the `DATA/*.DAT`
// records. The original loaders copy the records as-is
// (`load_ships_dat` 0x50f72, `load_subs_dat` 0x50e53) and the motion code
// reads them by offset; these adapters pick the same fields.
#pragma once

#include "assets/dat_tables.h"
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

// Returns the deck-gun rounds a SUBS.DAT class carries (+276, which the
// player init copies to 0x11b100).
std::int32_t sub_deck_gun_rounds(const SubClass& sub);

}  // namespace sh
