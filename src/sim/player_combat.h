// The player boat's weapons and damage for a mission: the TDC, the
// torpedo tubes, the deck gun and the damage model, with the mission
// settings they read (crew quality, command, torpedo choices). The mission
// owns it and hands it to the simulation (`Simulation::set_player_combat`);
// the player boat calls its per-step hooks and the main loop its per-frame
// update.
//
// Usage: build it with the tables and the boat's SUBS.DAT class, fill the
// tubes, racks and `torpedo_types` from the loadout, `tdc.reset()`,
// `deck_gun.reset(boat, ammo, systems)`, then each frame call `update_frame` after
// `Simulation::step`, and `fire_tube` on the fire keys.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/context.h"
#include "sim/deck_gun.h"
#include "sim/effects.h"
#include "sim/player_damage.h"
#include "sim/tdc.h"
#include "sim/torpedo_tubes.h"
#include "sim/vehicle.h"

namespace sh {

class PlayerSub;

// `player_update_aa_guns`'s elevation (0x5ea5d) to a target `dy` above the
// guns and (`dx`, `dz`) away: over |dx| alone (|dz| when `dx` is 0) as the
// original, over the horizontal range with `rules.fix_obvious_errors` (not
// in the original).
Angle10 aa_gun_elevation(std::int32_t dx, std::int32_t dy, std::int32_t dz, const MissionRules& rules);
// Returns whether the AA guns fire at the elevation `aim`: above 1.9
// degrees, read unsigned as the original (a target below wraps high and is
// fired at), signed with `rules.fix_obvious_errors` (not in the original).
bool aa_guns_fire_at(Angle10 aim, const MissionRules& rules);

// The boat's AA guns: manned from the 1MC menu, they fire at the nearest
// aircraft seen.
struct AaGuns {
  bool manned = false;          // 0x124e94
  std::int32_t fire_timer = 0;  // 0x124ed0, 16.16 s to the next round.
};

class PlayerCombat {
 public:
  // Crew messages of fire_tube.
  static constexpr std::int32_t kMsgFire = 0x11;          // #17
  static constexpr std::int32_t kMsgGyroLimit = 0x15;     // #21: gyro angle over 130 deg.
  static constexpr std::int32_t kMsgNotLoaded = 0x16;     // #22
  static constexpr std::int32_t kMsgStillLoading = 0x19;  // #25 (arg minutes left).
  static constexpr std::int32_t kMsgTubeDamaged = 0x1b;   // #27
  // Not in the original: no firing solution (assets/crew_messages.h).
  static constexpr std::int32_t kMsgNoSolution = 0x80;
  static constexpr std::int32_t kSfxFire = 0x14;
  // The largest gyro angle off the tube axis (130 deg).
  static constexpr std::int32_t kGyroLimit = 1300;

  // `tables` and `boat` (the player's SUBS.DAT class) must outlive it.
  PlayerCombat(const GameTables& tables, const SubClass& boat);

  // Fires tube `tube` (0-based) at the TDC solution (`fire_tube`,
  // 0x7cd70): refuses an empty, loading or damaged tube or a gyro angle
  // over 130 deg; the torpedo leaves turned at most 10 deg and then turns
  // to the gyro course plus the spread, and the tube starts reloading. In
  // auto mode the TDC first switches to the tube's type (when its table
  // lists it) and recomputes the gyros of a locked target.
  //
  // With the extended variant's Mark 27 (not in the original), in auto
  // mode: a straight runner in a tube while the TDC is bearing-only and
  // has no lock is not fired (crew message `kMsgNoSolution`; the TDC stays
  // on the Mark 27); a Mark 27 in a tube without a lock is aimed at the
  // loudest high-speed screws heard (else along the current gyros). In
  // manual mode either fires on the set gyros.
  // Returns the torpedo, or null when not fired.
  TorpedoVehicle* fire_tube(SimContext& ctx, PlayerSub& sub, std::size_t tube);

  // Hooks of the player update (`player_update`): damage and repairs after
  // the oxygen check, reloads after the helm and dive.
  void update_damage(SimContext& ctx, PlayerSub& sub, Fixed16 dt);
  void update_reloads(SimContext& ctx, PlayerSub& sub, Fixed16 dt);
  // `player_update_aa_guns` (0x5ea5d), after the reloads: the guns are
  // secured (crew message #63) when the bridge goes under; manned, they fire
  // an AA round at the nearest aircraft seen within 800 yd every 1.5 / 1 /
  // 0.5 s by crew plus a random part, with an aim error by the combat
  // realism.
  void update_aa_guns(SimContext& ctx, PlayerSub& sub, Fixed16 dt);
  // The player's slot +0x50.
  bool take_damage(SimContext& ctx, PlayerSub& sub, DamageKind kind, std::int32_t distance, std::int32_t amount);
  // The per-frame work of the mission loop: `tdc_sim_update` (which may
  // slave the view bearing) and `deckgun_sim_update`.
  void update_frame(SimContext& ctx, PlayerSub& sub);

  // Returns the fire-control view of the boat.
  OwnBoat own_boat(const PlayerSub& sub, const SimContext& ctx) const;
  // Drops the pointers to a removed vehicle.
  void forget(const Vehicle& removed);

  const GameTables& tables() const { return *tables_; }
  const SubClass& boat() const { return *boat_; }

  // Mission settings.
  std::int32_t crew = 0;     // `0x11ac2b`, 0..2 (2 best).
  std::int32_t command = 0;  // `0x11b872`, 0 ComSubsAsiatic .. 3 Task Force 42.
  std::array<TorpedoChoice, 3> torpedo_types{};  // `0x10f550`
  EffectFrames effects;
  // The sighting code's view of the targets (default: all in sight).
  TargetSensingFn sensing;
  // The high-speed screws the sonar hears, loudest first, refreshed by
  // `update_frame` while the type table lists the Mark 27 (not in the
  // original).
  std::vector<HeardScrews> heard;
  // The bearing the periscope and TBT look along (`0x11ac37`, relative).
  Angle10 view_bearing;

  Tdc tdc;
  TorpedoTubes tubes;
  PlayerDamage damage;
  DeckGun deck_gun;
  AaGuns aa_guns;

 private:
  const GameTables* tables_;
  const SubClass* boat_;
};

}  // namespace sh
