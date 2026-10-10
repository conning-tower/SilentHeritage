// Damage to the player's boat (player.cpp): hits by kind and how close
// they were (`player_take_damage`, `player_apply_system_damage`), the 24
// systems and 20 compartments they damage, and the per-second update of
// repairs, spontaneous failures, flooding, pumps, fires, chlorine and the
// crush depth (`player_damage_update`). The systems' severity and flags live
// in `PlayerSub::systems()`, the torpedo tubes' in `TorpedoTubes`.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/context.h"
#include "sim/vehicle.h"

namespace sh {

class PlayerSub;
class TorpedoTubes;

// Static data of a system (`0x10e544`, 16 bytes each).
struct SystemInfo {
  std::uint8_t flags = 0;         // +0: 1 can be damaged (else hits destroy it), 2 can be destroyed.
  bool repair_surfaced = false;   // +1 bit 0x10: repaired only on the surface.
  std::int32_t compartment = 0;   // +4: where it is (its fires and flooding stop the repair).
  std::string name;               // +8
};
// Returns the 24 systems, read from SH.EXE (`game_exe`) on first use.
const std::array<SystemInfo, 24>& system_info();

// Compartment flags (`0x10e6c8`).
namespace compartment_flag {
inline constexpr std::uint16_t kCanBurn = 0x80;      // Spontaneous fires.
inline constexpr std::uint16_t kChlorine = 0x100;    // A battery: chlorine when flooded.
inline constexpr std::uint16_t kHull = 0x200;        // On the pressure hull: hits breach it.
inline constexpr std::uint16_t kInner = 0x8000;      // Out of reach of gunfire.
}  // namespace compartment_flag

// Static data of a compartment (`0x10e6c4`, 0x1c bytes each; index 0 is
// unused). Water from a breach goes to the neighbours, else stays.
struct CompartmentInfo {
  std::string name;   // +0
  std::uint16_t flags = 0;  // +4
  std::array<std::int32_t, 3> neighbours{-1, -1, -1};  // +8, -1 terminated.
};
// Returns the 21 compartments, read from SH.EXE (`game_exe`) on first use.
const std::array<CompartmentInfo, 21>& compartment_info();

// A compartment's state (`0x11af20` + 20 x index).
struct CompartmentState {
  // Flags.
  static constexpr std::uint16_t kBreached = 1;
  static constexpr std::uint16_t kRepairing = 4;
  static constexpr std::uint16_t kFire = 0x80;
  static constexpr std::uint16_t kChlorineGas = 0x100;
  static constexpr std::uint16_t kWater1 = 0x200;   // Water above 1.
  static constexpr std::uint16_t kWater15 = 0x400;  // Above 15.
  static constexpr std::uint16_t kWater30 = 0x800;  // Above 30.

  std::int32_t water = 0;         // +0x00 16.16, at most 40.
  std::int32_t fire_timer = 0;    // +0x04 seconds.
  std::int32_t leak = 0;          // +0x08 breach severity %.
  std::int32_t repair_timer = 0;  // +0x0c seconds.
  std::uint16_t flags = 0;        // +0x10
};

// The game-over codes of `0x11a894` set by the damage code.
namespace end_code {
inline constexpr std::int32_t kSunkByTorpedo = 4;   // Kind 1.
inline constexpr std::int32_t kDepthCharged = 1;    // Kind 2.
inline constexpr std::int32_t kCrushed = 3;         // Kind 5 and below crush depth.
inline constexpr std::int32_t kSunkByGunfire = 5;   // Kind 0.
inline constexpr std::int32_t kBombed = 6;          // Kind 6.
inline constexpr std::int32_t kRammed = 0;          // Kind 3.
}  // namespace end_code

class PlayerDamage {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  static constexpr std::size_t kCompartments = 21;

  // The damage of a boat of class `boat` (which must outlive it).
  explicit PlayerDamage(const SubClass& boat) : boat_(&boat) {}

  // Slot +0x50 (`player_take_damage`, 0x5c144): turns a hit into the
  // damage margin of `apply` (gunfire 110 - 2 x amount and mostly missing
  // submerged, torpedoes 90 - amount clamped to 0..5, strafing none and
  // not below the bridge, blasts their distance in feet). Returns true.
  bool take_damage(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, DamageKind kind, std::int32_t distance,
                   std::int32_t amount, std::int32_t crew);
  // `player_apply_system_damage` (0x5fa94): the margin 0..300 (smaller is
  // worse) gives a severity; below 30 the boat may be lost outright.
  // Grounding goes to `run_aground`. Does nothing without Vulnerable Boat.
  void apply(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, DamageKind kind, std::int32_t margin,
             std::int32_t crew);
  // `player_run_aground` (0x60511): message, and with Vulnerable Boat and
  // above 1.9 kn damage to one of the sound heads, steering gear, fuel
  // tanks or bow tubes.
  void run_aground(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, std::int32_t crew);
  // `player_damage_update` (0x5ebe7), from the player update every
  // sub-step; works once per whole second gathered.
  void update(SimContext& ctx, PlayerSub& sub, TorpedoTubes& tubes, Fixed16 dt, std::int32_t crew);

  // Returns a repair time in seconds (`player_repair_time`, 0x5e8fc):
  // severity x (3 - crew) x 108 + 1800 - rand(3600), at least 60.
  static std::int32_t repair_time(SimContext& ctx, std::int32_t severity, std::int32_t crew);

  const std::array<CompartmentState, kCompartments>& compartments() const { return compartments_; }
  std::array<CompartmentState, kCompartments>& compartments() { return compartments_; }
  // Sum of the breach severities (`0x11b0c4`).
  std::int32_t hull_damage() const { return hull_damage_; }
  // Hull damage % (`0x11b0cc`) and overall damage % (`0x11b0d0`).
  std::int32_t hull_pct() const { return hull_pct_; }
  std::int32_t damage_pct() const { return damage_pct_; }
  // The depth the boat implodes at this dive (`0x11b0c8`, -1 surfaced).
  std::int32_t crush_depth() const { return crush_depth_; }
  // Seepage (`0x11b0e0`, 16.16).
  std::int32_t seepage() const { return seepage_; }
  // Returns whether compartment `c` exists on this class.
  bool has_compartment(std::size_t c) const;

 private:
  // The systems a hit may pick: fitted ones only (radars by option,
  // the guns, engines and motors the class has).
  bool system_fitted(const PlayerSub& sub, std::size_t s, std::uint32_t options) const;
  void damage_system(SimContext& ctx, PlayerSub& sub, std::size_t s, std::int32_t severity, std::int32_t crew);
  void damage_tubes(SimContext& ctx, TorpedoTubes& tubes, bool bow, std::int32_t severity, std::int32_t crew);
  void flood(PlayerSub& sub, std::size_t c, std::int32_t amount);                  // player_flood_compartment
  void pump(PlayerSub& sub, std::size_t c, std::int32_t amount);                   // player_pump_compartment
  void compute_damage_pct(const PlayerSub& sub, const TorpedoTubes& tubes, std::uint32_t options);
  std::int32_t compartment_count() const;

  const SubClass* boat_;
  std::array<CompartmentState, kCompartments> compartments_{};
  std::int32_t hull_damage_ = 0;    // 0x11b0c4
  std::int32_t crush_depth_ = -1;   // 0x11b0c8
  std::int32_t hull_pct_ = 0;       // 0x11b0cc
  std::int32_t damage_pct_ = 0;     // 0x11b0d0
  std::int32_t seepage_ = 0;        // 0x11b0e0
  std::int32_t elapsed_ = 0x10000;  // 0x124ed4, 16.16 s gathered (player init: a pass on the first step).
  std::int32_t last_depth_ = 0;     // 0x124ee0
};

}  // namespace sh
