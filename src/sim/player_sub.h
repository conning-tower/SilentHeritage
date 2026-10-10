// The player's submarine (class 0, the static object 0x11a8f0): helm and
// course, engine telegraph and engines, diving, fuel, battery and air
// (`player_update_helm_and_dive` 0x5c4f2 and its helpers). Sensors, torpedo
// handling, damage repair and flooding come with their subsystems; the
// damage state they maintain is held in `systems()` and read here.
#pragma once

#include <array>
#include <cstdint>

#include "core/fixed.h"
#include "sim/dive.h"
#include "sim/motion.h"
#include "sim/vehicle.h"

namespace sh {

// The 24 damageable systems (state `0x11ae00`).
enum class PlayerSystem : std::int32_t {
  kBowPlanes = 0,
  kSternPlanes = 1,
  kSoundHeads = 2,
  kRadio = 3,
  kPeriscope = 4,
  kPressureHull = 5,
  kSteeringGear = 6,
  kSdRadar = 7,
  kSjRadar = 8,
  kFwdBattery = 9,
  kAftBattery = 10,
  kFwdTubes = 11,
  kAftTubes = 12,
  kFuelTanks = 13,
  kDeckGun1 = 14,
  kDeckGun2 = 15,
  kEngine1 = 16,
  kMotor1 = 20,
};

// One system's damage (`0x11ae00` + 12 x id).
struct SystemState {
  std::int32_t severity = 0;   // %; above 85 destroys.
  std::uint8_t flags = 0;      // 1 damaged, 2 destroyed, 4 under repair, 8 waiting to surface.
  std::int32_t repair_time = 0;  // Seconds left (the damage code counts it down).
  bool impaired() const { return (flags & 3) != 0; }
};

// One engine or motor of an engine room (8 bytes of `0x11ac48`).
struct EnginePart {
  static constexpr std::uint8_t kRunning = 1;
  static constexpr std::uint8_t kCharging = 2;
  static constexpr std::uint8_t kDamaged = 4;
  static constexpr std::uint8_t kDestroyed = 8;
  std::uint8_t flags = 0;
  bool fitted = false;         // Byte 1 bit 2 clear.
  std::int32_t power_pct = 0;  // Signed: negative astern.
};

// Engine rooms 1-4: [0] the electric motor, [1] the diesel.
using EngineRoom = std::array<EnginePart, 2>;

class PlayerSub : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  static constexpr std::int32_t kTelegraphStop = 0;
  static constexpr std::int32_t kTelegraphSilent = 10;
  static constexpr std::int32_t kBatteryCapacity = 600;

  // A boat of class `sub` at `position` (y clamped to the surface by init),
  // `heading`, telegraph step 0..10, and fuel (-1: a full tank).
  PlayerSub(const SubClassParams& sub, fx::Vec3 position, Angle10 heading, std::int32_t telegraph,
            std::int32_t fuel = -1);

  VehicleClass class_id() const override { return VehicleClass::kPlayer; }
  // Fits the engines, fills battery and air, sets the dive block, engine
  // power and the speed it sustains (the player init, 0x5bb60).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 1; }
  // Air, then helm and dive with fuel and batteries (`player_update` order).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return motion_.position; }
  // `player_update_collision_box` (0x5c247).
  void update_bounds() override;
  // `player_take_damage` (player_damage.h), through the mission's
  // PlayerCombat; ignored without one.
  bool take_damage(SimContext& ctx, Vehicle* attacker, DamageKind kind, std::int32_t distance,
                   std::int32_t amount) override;

  // Sensor slots (sensors.cpp).
  Angle10 heading() const override { return motion_.heading; }
  Fixed16 speed() const override { return motion_.speed; }
  // `player_noise_level` (sonar.h).
  std::int32_t noise_level() const override;
  bool is_contact() const override { return true; }
  bool is_submarine() const override { return true; }
  // The top of its box: the bridge, or with the bridge under the raised
  // periscope or the radar mast (`player_update_collision_box`).
  std::int32_t visual_height() const override;

  // Orders.
  // Sets the telegraph step: 0 stop, 1-5 ahead 1/3 .. flank, 6-9 back
  // emergency .. 1/3, 10 silent running.
  void set_telegraph(std::int32_t step) { telegraph_ = step; }
  // Sets the helm step -7..7 (5 deg each, right positive) and leaves course mode.
  void set_helm(std::int32_t step);
  // Orders a course: the rudder then steers to it.
  void set_course(Angle10 course);
  // Sets the ordered depth (-1: none), raised to the keel depth
  // (`player_set_ordered_depth`).
  void set_ordered_depth(std::int32_t depth_ft);
  // Dive commands (crew orders and keys). Dive: full planes down, 200 ft
  // (none when already deeper), ahead flank (`cmd_dive`).
  void order_dive(SimContext& ctx);
  // Planes down to periscope depth (`cmd_periscope_depth`).
  void order_periscope_depth(SimContext& ctx);
  // Planes up, no ordered depth, when submerged or heavy (`cmd_surface`).
  void order_surface(SimContext& ctx);
  // Blows the ballast with compressed air at ahead flank, when submerged or
  // heavy and air is left (`cmd_blow_ballast`).
  void order_blow_ballast(SimContext& ctx);
  // Replaces the chart route and starts following it.
  void follow_route(const Route& route);
  // Stops following the chart route (0x11ac2f cleared by the helm orders).
  void stop_following_route() { follow_route_ = false; }
  // Follows the chart route again from the current waypoint (the nav
  // panel's engage button, `gauges_compass_engage`).
  void resume_route() { follow_route_ = true; }

  // State.
  const SubClassParams& sub() const { return sub_; }
  const MotionState& motion() const { return motion_; }
  MotionState& motion() { return motion_; }
  const DiveState& dive() const { return dive_; }
  // The dive block, for the damage code (flood weight).
  DiveState& dive() { return dive_; }
  std::int32_t telegraph() const { return telegraph_; }
  std::int32_t helm() const { return helm_; }
  bool silent_running() const { return telegraph_ == kTelegraphSilent; }
  bool diesel_mode() const { return diesel_mode_; }
  // Shaft power (16.16 "hp") of the running engines (`0x11aa54`).
  std::int32_t shaft_power() const { return motion_.ordered_power; }
  const std::array<EngineRoom, 4>& engines() const { return engines_; }
  // The engines, for the damage code (damaged/destroyed flags).
  std::array<EngineRoom, 4>& engines() { return engines_; }
  std::int32_t fuel() const { return fuel_; }
  std::int32_t battery() const { return battery_; }
  // Restores a battery charge (0..600), e.g. from a saved mission.
  void set_battery(std::int32_t charge) {
    battery_ = charge;
    battery_fraction_ = 0;
  }
  std::int32_t air() const { return air_; }
  std::int32_t air_max() const { return air_max_; }
  // Removes `fraction` (16.16 units) of air, as fires and chlorine do; the
  // oxygen check borrows from the units.
  void consume_air(std::int32_t fraction) { air_fraction_ -= fraction; }
  std::int32_t depth_ft() const { return dive_.depth_ft; }
  // 0x11ac33: the periscope raised (the scope station).
  bool periscope_raised() const { return periscope_raised_; }
  void set_periscope_raised(bool raised) { periscope_raised_ = raised; }
  // `player_can_raise_periscope` (0x60784): the periscope's top is above
  // water and the Periscope system works.
  bool can_raise_periscope() const;
  // `player_toggle_periscope` (0x6073e): raises the periscope when it can,
  // or lowers it; crew message #3 / #4 unless the periscope is damaged.
  void toggle_periscope(SimContext& ctx);
  // `player_pilot_rescued(0)` (0x60c9e, also the 'E' key and the 1MC):
  // takes aboard the downed pilot the boat last touched, when surfaced
  // (depth at most the draft) at 5 kn or less: the raft goes, the crew
  // reports #67 and the patrol log gets the rescue
  // (`MissionEvents::pilots_rescued`). Returns whether he was taken.
  bool rescue_pilot(SimContext& ctx);
  // `player_pilot_rescued(-1)`: a pilot is alongside and the boat surfaced.
  bool can_rescue_pilot() const;
  void forget(const Vehicle& removed) override;
  // The swell offset of the outside views (0x124ef4: 30 is level), moved
  // by `player_update_swell` (0x5e93e) on a moderate or heavy sea.
  std::int32_t swell() const { return swell_.bob; }
  bool following_route() const { return follow_route_; }
  // The chart route (0x11ab2c / 0x11ab90, count 0x11abf8).
  const Route& route() const { return route_; }
  std::int32_t route_index() const { return route_index_; }
  std::array<SystemState, 24>& systems() { return systems_; }
  const std::array<SystemState, 24>& systems() const { return systems_; }

 private:
  SystemState& system(PlayerSystem s) { return systems_[static_cast<std::size_t>(s)]; }
  std::int32_t engine_mode() const { return diesel_mode_ ? 1 : 0; }
  std::int32_t battery_capacity();
  bool has_energy() const;
  void stop_running_engines();
  void check_ramming(SimContext& ctx, class PlayerCombat* combat);

  void update_engines(bool at_init);          // player_update_engine_sounds (logic only)
  void compute_shaft_power();                 // player_compute_shaft_power
  void turn_back_at_map_edge(const SimContext& ctx);
  void follow_waypoints(SimContext& ctx);     // player_waypoint_reached
  void update_helm_and_dive(SimContext& ctx, class PlayerCombat* combat, Fixed16 dt);
  void check_oxygen(SimContext& ctx, Fixed16 dt);
  void check_fuel(SimContext& ctx, Fixed16 dt);
  void check_batteries(SimContext& ctx, Fixed16 dt);
  void charge_batteries(const SimContext& ctx, Fixed16 dt);
  void refresh_air(SimContext& ctx);
  void update_swell(SimContext& ctx, Fixed16 dt);  // player_update_swell (0x5e93e)

  SubClassParams sub_;
  MotionState motion_;
  DiveState dive_;
  std::array<SystemState, 24> systems_{};
  std::array<EngineRoom, 4> engines_{};
  std::int32_t telegraph_ = 0;               // 0x11ac43
  std::int32_t helm_ = 0;                    // 0x11ac3f
  bool diesel_mode_ = true;                  // 0x11ac47
  std::int32_t fuel_ = -1;                   // 0x11ac88
  std::int32_t fuel_fraction_ = 0;           // 0x11ac8c
  std::int32_t battery_ = kBatteryCapacity;  // 0x11ac90
  std::int32_t battery_fraction_ = 0;        // 0x11ac94
  std::int32_t air_ = 0;                     // 0x11b0d8
  std::int32_t air_max_ = 0;                 // 0x11b0d4
  std::int32_t air_fraction_ = 0;            // 0x11b0dc
  Route route_;                          // 0x11ab2c / 0x11ab90 / 0x11abf8
  std::int32_t route_index_ = 0;             // 0x11abf4
  bool follow_route_ = false;                // 0x11ac2f
  // Change detection of the engine orders (0x124e8c, 0x124ea8, 0x124e98).
  std::int32_t applied_telegraph_ = -1;
  std::int32_t applied_mode_ = 2;
  std::array<std::uint16_t, 4> applied_flags_{};
  std::int32_t announced_helm_ = 0;          // 0x124e90
  bool periscope_raised_ = false;            // 0x11ac33
  Vehicle* pilot_alongside_ = nullptr;       // 0x124edc, the last pilot touched.
  // The swell (0x124ef4..0x124f04), as the player init sets it.
  struct Swell {
    std::int32_t bob = 30;            // 0x124ef4
    std::int32_t direction = 1;       // 0x124ef8
    std::int32_t offset = 0x1e0000;   // 0x124efc, 16.16
    std::int32_t amplitude = 30;      // 0x124f00
    std::int32_t rate = 1000;         // 0x124f04
  };
  Swell swell_;
};

}  // namespace sh
