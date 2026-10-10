// The player's deck guns (deckgun.cpp, state 0x11b0e8.., 0x11c18c..): two
// mounts at +-length/4 (or the class's single mount) firing 2100 fps
// shells of the class caliber. The crew trains on the lead bearing and
// fires by itself at solution quality 80 and within 7000 yd; reloads take
// 2 s + rand(0.5 s), or (3 - crew) x 4 s + rand(25 %) with Realistic
// Reloads. A mount cannot train within 7 deg of the conning tower.
#pragma once

#include <array>
#include <cstdint>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/context.h"
#include "sim/player_sub.h"
#include "sim/tdc.h"

namespace sh {

class Vehicle;

// Muzzle velocity of the player's guns (ft/s, `0x834`).
inline constexpr std::int32_t kDeckGunVelocity = 2100;

// Returns the heading from the gun at (gx, gz) to where a target at
// (tx, tz) on `course` at `speed` will be when a `velocity_fps` shell
// arrives (`gun_lead_bearing`, 0x3855f).
Angle10 gun_lead_bearing(std::int32_t velocity_fps, Angle10 course, Fixed16 speed, std::int32_t tx, std::int32_t tz,
                         std::int32_t gx, std::int32_t gz);

// Returns `train` (relative bearing) kept out of the 7 deg either side of
// the conning tower for `mount` (0 forward, 1 aft) (`deckgun_clamp_train`).
Angle10 deck_gun_clamp_train(std::int32_t mount, Angle10 train);

class DeckGun {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Crew messages of `set_manned`.
  static constexpr std::int32_t kMsgUnmanned = 0x34;
  static constexpr std::int32_t kMsgManned = 0x35;
  static constexpr std::int32_t kMsgCannotMan = 0x36;

  struct Mount {
    Angle10 train;                   // 0x11b0e8, relative.
    Angle10 elevation;               // 0x11b0f0, the solution's.
    Angle10 elevation_offset;        // 0x11b0f8, the manual correction.
    std::int32_t reload = 0;         // 0x11b104, hundredths of a second.
    bool ready = false;              // 0x11c1a4
    std::int32_t x = 0;              // 0x11c1ac
    std::int32_t z = 0;              // 0x11c1b4
  };

  // The guns of `boat` (which must outlive them).
  explicit DeckGun(const SubClass& boat) : boat_(&boat) {}

  // Sets the start-of-mission state for `boat` with `ammo` rounds
  // (`deckgun_reset_state`, 0x37cdc, with the mounts of `player_init`):
  // no target, manual control, the aft mount trained astern; a one-gun boat marks the absent gun's system destroyed in `systems`.
  void reset(const SubClass& boat, std::int32_t ammo, std::array<SystemState, 24>& systems);
  // Toggles (`toggle`) or sets crew control (`deckgun_set_manned`): a
  // gun must be usable and the boat surfaced; reports #52..#54.
  void set_manned(SimContext& ctx, const PlayerSub& sub, bool manual, bool toggle);
  // Picks the crew's target (`deckgun_crew_set_target`): only when the
  // crew controls the guns.
  void crew_set_target(Vehicle* target);
  // Per-frame update (`deckgun_sim_update`, 0x37d6a), `frame_ticks` game
  // hundredths: mount positions, reloads, the solution from the crew's
  // estimate and the crew's fire.
  void update(SimContext& ctx, PlayerSub& sub, const OwnBoat& own, std::int32_t frame_ticks,
              const TargetSensingFn& sensing);
  // Fires mount `m` when ready (`deckgun_fire`, 0x37a89); `crew` sets the
  // realistic reload time.
  void fire(SimContext& ctx, PlayerSub& sub, std::int32_t m, std::int32_t crew);
  // Drops the target if removed.
  void forget(const Vehicle& removed);

  const Mount& mount(std::int32_t m) const { return mounts_.at(static_cast<std::size_t>(m)); }
  Mount& mount(std::int32_t m) { return mounts_.at(static_cast<std::size_t>(m)); }
  std::int32_t ammo() const { return ammo_; }
  Vehicle* target() const { return target_; }
  void set_target(Vehicle* target) { target_ = target; }
  bool manual() const { return manual_; }
  std::int32_t range_yd() const { return range_yd_; }
  std::int32_t quality() const { return quality_; }
  std::int32_t selected() const { return selected_; }
  void select(std::int32_t m) { selected_ = m; }
  void set_sighting_range(std::int32_t yards) { sighting_range_ = yards; }
  void set_follow(bool on) { follow_ = on; }
  // 0x11c1cc / 0x11c1c8: the guns follow the target's bearing; the target
  // is locked.
  bool following() const { return follow_; }
  bool locked() const { return locked_; }
  void set_locked(bool on) { locked_ = on; }
  // 0x11c194: the target marker's blink phase (hidden while set and unlocked).
  bool blink() const { return blink_; }
  // Sets manual control (`deckgun_toggle_manual`: no check, no report).
  void set_manual(bool on) { manual_ = on; }
  // Unless locked, picks the nearest live, visible surface target within
  // 2.6 deg of the selected mount's train; a new target resets that mount's
  // elevation offset (`deckgun_pick_target_on_bearing`, 0x38115).
  void pick_target_on_bearing(const OwnBoat& own, const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                              const TargetSensingFn& sensing);
  // Unlocks, or picks the target on the train and locks it
  // (`deckgun_toggle_target_lock`, 0x384be).
  void toggle_target_lock(const OwnBoat& own, const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                          const TargetSensingFn& sensing);
  // Toggles the bearing follow while locked; `off` forces it off
  // (`deckgun_toggle_bearing_follow`, 0x384eb).
  void toggle_bearing_follow(bool off);

  // The deck roll the elevation picks up at high realism (`0x124ef4`,
  // 30 = level), from the swell.
  std::int32_t swell = 30;

 private:
  bool usable(const PlayerSub& sub) const;

  const SubClass* boat_;
  std::array<Mount, 2> mounts_{};
  std::int32_t ammo_ = 0;            // 0x11b100
  Vehicle* target_ = nullptr;        // 0x11c1c4
  bool locked_ = false;              // 0x11c1c8
  bool follow_ = false;              // 0x11c1cc
  std::int32_t sighting_range_ = 0;  // 0x11c1d4
  std::int32_t selected_ = 0;        // 0x11c1d8
  bool manual_ = true;               // 0x11c1dc
  std::int32_t range_yd_ = 0;        // 0x11c19c
  std::int32_t quality_ = 0;         // 0x11c1a0
  std::int32_t ticks_ = 0;           // 0x11c18c
  std::int32_t blink_ticks_ = 0;     // 0x11c190
  bool blink_ = false;               // 0x11c194
};

}  // namespace sh
