// A surface ship (class 1, vtable 0x101b2c), for now its motion only:
// `ship_init` sets its power and speed from the class drag model and
// `ship_update_motion` steps it. The AI (orders, navigation, convoys),
// sighting, damage and weapons drive it through `motion()` once they exist.
#pragma once

#include <cstdint>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/ai_ship.h"
#include "sim/motion.h"
#include "sim/ship_combat.h"
#include "sim/vehicle.h"

namespace sh {

// The `.SCN` orders of a ship (+0x3a8).
enum class ShipOrder : std::int32_t {
  kNone = 0,
  kEscort = 1,
  kPatrol = 2,
  kConvoyLeader = 3,
  kConvoyMember = 4,
  kIntercept = 5,
  kFollowPath = 6,
  kFishing = 7,
};

// What `scen_spawn_ship` (0x53d81) sets besides the motion, for the AI,
// damage and sensor code. Offsets are the original object's.
struct ShipSetup {
  const ShipClass* ship_class = nullptr;  // +0x224 (must outlive the ship)
  std::int32_t crew = 0;                  // +0x3b0, 0 green .. 2 elite
  std::int32_t hit_points = 0;            // +0x3a4, hull strength less the damage
  std::int32_t damage = 0;                // +0x4dc, hull strength x .SCN damage %
  Route route;                            // +0x3b4 x / +0x418 z / +0x480 count
  std::int32_t route_index = 1;           // +0x47c, point 0 is the start position
  std::int32_t convoy_group = 0;          // +0x484
  std::int32_t zigzag = 0;                // +0x488
};

// The alert state (`ship_set_alert`, slot +0x98).
struct ShipAlert {
  std::int32_t level = 0;     // +0x1d0
  std::int32_t previous = 0;  // +0x1d4
  Fixed16 timer;              // +0x1d8, seconds
};

// The values `ship_init` rolls (in this draw order) that shape how the ship
// reacts to an attack in `ai_ship_think`.
struct ShipReactionRolls {
  // +0x1e8: the course of an alerted leader or straggler, as an offset from
  // the bearing to the contact, in tenths of a degree: (135 + rand(90)) x 10
  // (away from the contact), re-rolled while the alert level is steady and
  // set to (90 + rand(180)) x 10 when a merchant breaks away from its convoy.
  std::int32_t flee_angle = 0;
  // +0x4ac: on a convoy leader, how many more of its merchants may break
  // away on its route when the convoy is alerted (1 + rand(3)); each one
  // that does decrements it.
  std::int32_t breakaways_left = 0;
  // +0x1dc: 2 + rand(10) minutes; written by `ship_init` and the state
  // files, never read; kept for the original's save layout.
  Fixed16 preserved_timer_1dc;
  // +0x1e0: true when the scenario roll is below 30 / 10 / 2 % by crew
  // (table 0x10fa8c): an escort that has dropped charges and sees debris
  // then takes the submarine as sunk and gives up the hunt (clearing the
  // flag); without it, new debris is hunted as a contact.
  bool fooled_by_debris = false;
};

class Ship : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A ship of class `params` at `position` (world units), `heading` and
  // ordered `speed` (16.16 kn), as the scenario spawner fills it.
  Ship(const ShipMotionParams& params, fx::Vec3 position, Angle10 heading, Fixed16 speed, ShipOrder orders);

  VehicleClass class_id() const override { return VehicleClass::kShip; }
  // Rolls the AI values, then sets the ordered power for the ordered speed
  // (capped at the class maximum), the speed that power sustains in the
  // current sea, and the motion block (`ship_init`, 0x80e13).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 1; }
  // 64x, or unrestricted once sinking (`ship_max_time_compression`).
  std::int32_t max_time_compression() const override { return sinking() ? kUnrestrictedCompression : 0x40; }
  // The damage state, the AI and the motion with the convoy formation, the
  // effects (`ship_update`, 0x81170).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return motion_.position; }
  // The hull box from the keel to SHIPS.DAT +188 above the waterline
  // (`ship_update_bbox`, 0x82116).
  void update_bounds() override;
  // `ship_take_damage` (ship_combat.h).
  bool take_damage(SimContext& ctx, Vehicle* attacker, DamageKind kind, std::int32_t distance,
                   std::int32_t amount) override;
  // Drops a removed attacker and the AI's pointers to it.
  void forget(const Vehicle& removed) override;

  // Sensor and AI slots (ai_ship.cpp).
  Angle10 heading() const override;
  Fixed16 speed() const override;
  // Its sighting range from the player's periscope height (+0x1f4).
  std::int32_t comms_range_yd() const override;
  // `veh_side_is_hostile` (0x7ff32).
  bool is_hostile(std::int32_t player_side) const override { return side() != player_side; }
  // `ship_hydrophone_check` (sonar.h).
  void hydrophone_check(SimContext& ctx) override;
  bool is_radar_target() const override { return true; }
  // Its length x 100.
  std::int32_t radar_cross_section(const fx::Vec3& observer, Angle10 observer_heading) const override;
  // `ship_noise_level`.
  std::int32_t noise_level() const override;
  bool is_contact() const override { return true; }
  bool sighted() const override { return ai_.sighting.sighted; }
  void mark_sighted(const GameClock& clock) override;
  std::int32_t add_track_time(std::int32_t dt, bool selected) override;
  std::int32_t track_time() const override { return ai_.sighting.track_time; }
  void refresh_sighted() override;
  // The top of its hull box: SHIPS.DAT +188 above its y.
  std::int32_t visual_height() const override;
  // `ship_is_surface_target` (0x19d04).
  bool is_surface_target() const override { return visual_height() > 0; }
  // 2 once its lookout height (SHIPS.DAT +192) is under water, else 1.
  std::int32_t exposure() const override;
  bool is_escort() const override;
  bool dropping_charges() const override { return ai_.dropping; }
  std::int32_t convoy_group_by_role(bool members) const override;
  std::int32_t escorted_group() const override;
  bool is_small_craft() const override;
  bool has_attacked_player() const override { return ai_.attacked_player; }
  // `ai_ship_receive_contact` (0x12ac9).
  void receive_contact(SimContext& ctx, const fx::Vec3& position, Vehicle* target) override;
  // Not in the original (the extended escort radar detectors, and the
  // fathometer soundings it hears): takes `position` (an estimate on a
  // bearing) as its contact on `target`, alerted, at any range, unless it
  // has a contact under 30 s old.
  void receive_radar_intercept(const fx::Vec3& position, Vehicle* target);

  const ShipMotionParams& params() const { return params_; }
  ShipOrder orders() const { return orders_; }
  void set_orders(ShipOrder orders) { orders_ = orders; }
  // Hit, fire and sinking state (ship_combat.h).
  ShipDamageState& combat() { return combat_; }
  const ShipDamageState& combat() const { return combat_; }
  MotionState& motion() { return motion_; }
  const MotionState& motion() const { return motion_; }

  // Scenario fields, set by the spawner before init.
  ShipSetup& setup() { return setup_; }
  const ShipSetup& setup() const { return setup_; }
  // Sets the alert level, keeping the previous one, and arms the alert
  // timer to (crew + 1) x 30 minutes (`ship_set_alert`, 0x19cc4).
  void set_alert(std::int32_t level);
  const ShipAlert& alert() const { return alert_; }
  ShipAlert& alert() { return alert_; }
  const ShipReactionRolls& reaction_rolls() const { return reaction_rolls_; }
  ShipReactionRolls& reaction_rolls() { return reaction_rolls_; }
  // The AI state (ai_ship.h).
  ShipAi& ai() { return ai_; }
  const ShipAi& ai() const { return ai_; }

 private:
  ShipMotionParams params_;
  ShipOrder orders_;
  MotionState motion_;
  ShipSetup setup_;
  ShipAlert alert_;
  ShipReactionRolls reaction_rolls_;
  ShipDamageState combat_;
  ShipAi ai_;
};

}  // namespace sh
