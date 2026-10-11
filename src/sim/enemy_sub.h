// An enemy submarine (class 2, vtable 0x101c18): a SHIPS.DAT submarine
// class moved by the submarine dive model (`sub_dive_update` with a
// parameter block built from the SHIPS.DAT record), its AI
// (`ai_sub_think` 0x13471: lookouts, periscope approach, Type 95 torpedo
// salvos, evasion, deep search), damage and sinking (the ships' rules and
// effects block), and the sensor slots the player's lookouts, radars and
// hydrophones use.
#pragma once

#include <cstdint>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/dive.h"
#include "sim/motion.h"
#include "sim/ship.h"
#include "sim/ship_combat.h"
#include "sim/vehicle.h"

namespace sh {

// The AI states (+0xa34).
enum class EnemySubMode : std::int32_t {
  kCruise = 0,          // Orders; an alert sends it after the contact.
  kCloseOnContact = 1,  // At 64 ft toward the reported contact.
  kEvadeStart = 2,      // A friendly on a collision course: go to 150 ft, hard rudder, stop.
  kEvading = 3,
  kApproachStart = 16,  // Periscope depth, slow, 10-13 min to find the contact.
  kApproach = 18,
  kAgroundStart = 19,   // Touching bottom: full astern.
  kBackingOff = 20,
  kFindWater = 21,
  kToWater = 22,
  kAttackStart = 23,    // Contact seen: roll the firing range.
  kAttack = 24,         // At periscope depth to the firing range, then a salvo.
  kGoDeep = 25,         // 150 ft until the 300 s cool-down ends.
  kSearchDeep = 26,     // 100 ft on its orders for (crew + 1) x 30 min.
  kListen = 27,         // 60 ft, dead slow, until it hears nothing.
};

// The scenario fields, set by the spawner before init (`scen_spawn_enemy_sub`).
struct EnemySubSetup {
  const ShipClass* ship_class = nullptr;  // +0x1e0 (must outlive the sub)
  const Torpedo* torpedo = nullptr;       // +0x1c4, TYPE95_1 (must outlive it); null: no salvos
  std::int32_t crew = 0;                  // +0xa38
  std::int32_t hit_points = 0;            // +0xa2c
  std::int32_t damage = 0;                // +0xb3c
  Route route;                            // +0xa64 / +0xac8 / +0xb30
  std::int32_t route_index = 1;           // +0xb2c
  std::int32_t target_x = -1;             // +0xb34, intercept point; -1: the player's position
  std::int32_t target_z = -1;             // +0xb38
};

// The AI state (+0x130..+0x1c8 and the mode +0xa34).
struct EnemySubAi {
  EnemySubMode mode = EnemySubMode::kCruise;
  ShipAiContact contact;                   // +0x130..+0x148
  ShipAlert alert;                         // +0x14c..+0x154
  std::int32_t think_time = 0;             // +0x158
  bool attacked_player = false;            // +0x15c
  bool friendly_near_contact = false;      // +0x160
  std::int32_t sighting_range_yd = 0;      // +0x168, from the player's eye.
  std::int32_t previous_clearance = 0;     // +0x16c
  std::int32_t fire_range_yd = 0;          // +0x170
  std::int32_t salvo = 0;                  // +0x174
  std::int32_t aground_power = 0;          // +0x178
  std::int32_t closing_count = 0;          // +0x17c, thinks with the range opening.
  EnemySubMode water_mode = EnemySubMode::kCruise;   // +0x180
  EnemySubMode aground_mode = EnemySubMode::kCruise; // +0x184
  bool aground_steering = false;           // +0x188
  std::int32_t aground_rudder = 0;         // +0x18c
  std::int32_t evade_power = 0;            // +0x190
  std::int32_t last_range = 0;             // +0x194
  std::int32_t min_contact_y = 0;          // +0x198, never set: fires at surface contacts only.
  bool saved_steering = false;             // +0x19c
  std::int32_t saved_rudder = 0;           // +0x1a0, or the ordered heading (find water).
  std::int32_t cruise_power = 0;           // +0x1a4, restored after the search.
  std::int32_t timer = 0;                  // +0x1a8, 16.16 s.
  std::int32_t opening_time = 0;           // +0x1ac
  std::int32_t timer2 = 0;                 // +0x1b0
  std::int32_t periscope_time = 0;         // +0x1b4, also the saved power (find water).
  std::int32_t reload_timer = 0;           // +0x1b8
  std::int32_t cooldown = 0;               // +0x1bc, after a salvo.
  std::int32_t torpedoes = 5;              // +0x1c0, one reloaded every (crew + 1) x 300 s.
  std::int32_t speed_setting = 0;          // +0x1c8
};

class EnemySub : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A submarine at `position` (y = -16 x depth), `heading`, ordered `speed`
  // (16.16 kn) and `orders`, on `side`.
  EnemySub(fx::Vec3 position, Angle10 heading, Fixed16 speed, ShipOrder orders, std::int32_t side);

  VehicleClass class_id() const override { return VehicleClass::kEnemySub; }
  // `enemy_sub_init` (0x7f12f): AI reset, route pruning, the dive block,
  // power for the ordered speed, the effects block (its draws), the motion.
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 2; }
  std::int32_t max_time_compression() const override { return 0x40; }
  // `enemy_sub_update` (0x7f772).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return motion_.position; }
  // `enemy_sub_update_bbox` (0x7f4dd): keel to SHIPS.DAT +192 above the hull.
  void update_bounds() override;
  // `enemy_sub_take_damage` (0x7f982).
  bool take_damage(SimContext& ctx, Vehicle* attacker, DamageKind kind, std::int32_t distance,
                   std::int32_t amount) override;
  // `enemy_sub_clear_dead_refs`.
  void forget(const Vehicle& removed) override;
  // `ai_sub_receive_contact` (0x1492d).
  void receive_contact(SimContext& ctx, const fx::Vec3& position, Vehicle* target) override;

  Angle10 heading() const override { return motion_.heading; }
  Fixed16 speed() const override { return motion_.speed; }
  std::int32_t comms_range_yd() const override { return ai_.sighting_range_yd; }
  // `enemy_sub_hydrophone_check` (0x7022b): heard only while surfaced.
  void hydrophone_check(SimContext& ctx) override;
  bool is_radar_target() const override { return true; }
  // 0 deeper than 54 ft, else its length x 100.
  std::int32_t radar_cross_section(const fx::Vec3& observer, Angle10 observer_heading) const override;
  // `enemy_sub_noise_level`: 20 + power % of SHIPS.DAT +112 (at most 100), 30 sinking.
  std::int32_t noise_level() const override;
  bool is_contact() const override { return true; }
  bool sighted() const override { return sighting_.sighted; }
  void mark_sighted(const GameClock& clock) override;
  std::int32_t add_track_time(std::int32_t dt, bool selected) override;
  std::int32_t track_time() const override { return sighting_.track_time; }
  void refresh_sighted() override;
  // Its y + SHIPS.DAT +188 x 16.
  std::int32_t visual_height() const override;
  // `enemy_sub_is_surface_target` (0x19b18): surfaced (or shallow enough).
  bool is_surface_target() const override { return visual_height() > 0; }
  // 5 deeper than 54 ft, 2 with SHIPS.DAT +192 under water, else 1.
  std::int32_t exposure() const override;
  bool is_submarine() const override { return true; }
  bool has_attacked_player() const override { return ai_.attacked_player; }

  // Slot +0x98: the alert level with the timer (crew + 1) x 30 min.
  void set_alert(std::int32_t level);

  ShipOrder orders() const { return orders_; }
  EnemySubSetup& setup() { return setup_; }
  const EnemySubSetup& setup() const { return setup_; }
  EnemySubAi& ai() { return ai_; }
  const EnemySubAi& ai() const { return ai_; }
  MotionState& motion() { return motion_; }
  const MotionState& motion() const { return motion_; }
  DiveState& dive() { return dive_; }
  const DiveState& dive() const { return dive_; }
  // The dive model's block built from the SHIPS.DAT class (+0x34c).
  const SubClassParams& dive_params() const { return params_; }
  ShipDamageState& combat() { return combat_; }
  const ShipDamageState& combat() const { return combat_; }
  const ShipSightingRecord& sighting() const { return sighting_; }
  // The player's lookout range to it and its distance when seen (-1).
  std::int32_t player_range_yd() const { return player_range_yd_; }
  std::int32_t player_distance_yd() const { return player_distance_yd_; }
  // The player's hydrophones hear it (+0xd8) on this relative bearing
  // (+0xdc): `enemy_sub_get_sound_contact` (0x7056d), the chart's sound
  // bearing.
  bool heard() const { return heard_; }
  Angle10 heard_bearing() const { return heard_bearing_; }

 private:
  void update_damage_state(SimContext& ctx);  // 0x7f9ff
  void think(SimContext& ctx, std::int32_t dt);  // ai_sub_think
  void navigate_orders(SimContext& ctx);  // ai_sub_navigate_orders (0x14378)
  std::int32_t fire_torpedoes(SimContext& ctx);  // ai_sub_fire_torpedoes (0x1444a)
  std::int32_t power_for_depth() const;  // SHIPS.DAT +112 surfaced, +116 submerged.

  ShipOrder orders_;
  MotionState motion_;
  DiveState dive_;
  SubClassParams params_;
  EnemySubSetup setup_;
  EnemySubAi ai_;
  ShipDamageState combat_;
  ShipSightingRecord sighting_;
  HullParams hull_;  // SHIPS.DAT +176.. (the power-for-speed hull).
  std::int32_t max_power_ = 0;          // SHIPS.DAT +112
  std::int32_t submerged_power_ = 0;    // SHIPS.DAT +116
  std::int32_t player_range_yd_ = 0;    // +0xa3c
  std::int32_t player_distance_yd_ = -1;  // +0xa44
  std::int32_t report_timer_ = 0;       // +0xf0, hundredths.
  bool heard_ = false;                  // +0xd8
  Angle10 heard_bearing_;               // +0xdc
};

// Returns the dive-model block of an enemy submarine class (`enemy_sub_init`
// fills +0x34c from SHIPS.DAT): length, beam, a draft of 0, the awash depth
// from +192, the displacements +196/+200/+204, the drag pairs +208..+220,
// +224..+232 and a pitch factor of 38.2.
SubClassParams enemy_sub_dive_params(const ShipClass& cls);

}  // namespace sh
