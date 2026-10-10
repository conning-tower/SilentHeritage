// Damage, fire and sinking of surface ships (`ship_take_damage`,
// `ship_update_damage_state`, `veh_fx_state_*`): hits take hit points
// (`ShipSetup::hit_points`, from SHIPS.DAT `hull_strength`); below half the
// ship burns, at a twentieth it sinks; damage also caps the engine power
// in proportion. A sinking ship goes down 4..9 world units every second
// and is removed once its depth reaches SHIPS.DAT +188.
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "sim/context.h"
#include "sim/motion.h"
#include "sim/vehicle.h"

namespace sh {

class Ship;

// The effects block of a ship (+0x36c, also the enemy sub's +0x9f4): the
// animation state the renderer draws and the sinking motion.
struct ShipEffects {
  std::int32_t bow_wave_frame = 0;    // [0] 0..2
  std::int32_t bow_wave_timer = 0;    // [1] 16.16 s
  bool burning = false;               // [2]
  std::int32_t fire_frame = 0;        // [3] FIRE-0.SHP
  std::int32_t fire_timer = 0;        // [4]
  bool sinking = false;               // [5]
  std::int32_t sink_rate = 0;         // [6] world units per second, 4..9.
  std::int32_t sink_timer = 0;        // [7]
  std::int32_t explosion_frame = 0;   // [8] -1 once the blast is over.
  std::int32_t explosion_sprite = 0;  // [9] 0 EXPL-0, 1 EXPL-1.
};

// The damage state of a ship besides its hit points.
struct ShipDamageState {
  bool hit = false;            // +0xd0: hit since the last update.
  Vehicle* attacker = nullptr; // +0xd4
  ShipEffects effects;         // +0x36c
};

// Crew message and sounds.
inline constexpr std::int32_t kMsgShipSinking = 0x42;  // #66

// Rolls the sink rate and blast sprite (`veh_fx_state_init`, 0x8be3c).
void ship_effects_init(SimContext& ctx, ShipEffects& fx);

// Slot +0x50 of ships (`ship_take_damage`, 0x82383): the player's hits
// count (3 - combat level) times; others add to the scenario damage. A
// convoy leader (handing the lead over) or member (taking the leader's
// route) hit below half strength leaves the convoy (orders 6). Returns true.
bool ship_take_damage(SimContext& ctx, Ship& ship, Vehicle* attacker, std::int32_t amount);

// `ship_update_damage_state` (0x817e3): after a hit, sets the ship burning
// below half its strength or sinking at a twentieth (engines stopped,
// orders none, crew report #66 and the sinking log entry for the
// player's kills); then caps the ordered power by the hit points left.
void ship_update_damage_state(SimContext& ctx, Ship& ship);

// `veh_fx_state_update` (0x8be83): bow wave and fire animation, the
// sinking motion and the removal at the class's sinking depth.
void ship_update_effects(SimContext& ctx, Ship& ship, Fixed16 dt);

// The body of `veh_fx_state_update` for any hull with an effects block
// (ships, enemy submarines): `visual_size_ft` (SHIPS.DAT +188) is the
// sinking depth that removes `vehicle`, `draft_ft` sets the keel clearance.
void vehicle_update_effects(SimContext& ctx, Vehicle& vehicle, ShipEffects& fx, MotionState& m,
                            std::int32_t visual_size_ft, std::int32_t draft_ft, Fixed16 dt);

}  // namespace sh
