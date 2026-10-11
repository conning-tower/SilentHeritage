// Aircraft (class 3, vtable 0x101d04) and their smoke trail (class 15).
//
// An aircraft starts on the ground: `init` (also rerun at every take-off)
// leaves it landed and gone, and `AirSorties` (air_sorties.h) launches it
// near the player when the weather allows. Airborne, it flies the original
// flight model (`aircraft_update_flight`: bank toward the wanted heading,
// pitch toward the ordered altitude, speed from AIRCRAFT.DAT +124 and the
// throttle) under `ai_aircraft_think` (0x14d8b): orbit its station (mission
// 1 escorts a convoy group, 2 patrols, 6 follows its path), dive-bomb what
// its crew sees, strafe once the bombs are gone, or shadow without guns;
// home in bad light or fog, landing out of sight of the player. Shot down,
// it trails smoke and crashes in a splash or an explosion.
#pragma once

#include <array>
#include <cstdint>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/motion.h"
#include "sim/vehicle.h"

namespace sh {

class Ship;

// The scenario missions (+0x344).
enum class AircraftMissionCode : std::int32_t {
  kEscort = 1,      // Orbits a convoy group, moving with its leader.
  kPatrol = 2,      // Orbits its station.
  kFollowPath = 6,
};

// The AI states (+0x24c).
enum class AircraftState : std::int32_t {
  kMission = 0,      // Back to the mission's orbit or path.
  kBombApproach = 1, // Open out to a mile from the contact.
  kBombRun = 2,
  kBombAway = 3,     // Climb away for 30 s.
  kStrafeApproach = 4,
  kStrafeRun = 5,
  kStrafeAway = 6,
  kShadowStart = 7,  // No bombs, no guns: circle the contact.
  kShadowLeg = 8,
  kShadowing = 9,
  kOrbitStart = 10,
  kOrbitReturn = 11, // Back to the orbit centre.
  kOrbiting = 12,
  kOrbitTurn = 13,
  kHomeStart = 14,   // Bad light or fog: climb to 1000 ft and leave.
  kHome = 15,
  kHoldStart = 16,   // Wait for a wingman's run (never reached: see aircraft.cpp).
  kHold = 17,
};

// The strafing run's contact bearing (0..3599) folded past 180 degrees as
// the original folds it, by 900, or by 3600 with `rules.fix_obvious_errors`
// (not in the original).
std::int32_t strafe_bearing(std::int32_t contact_bearing, const MissionRules& rules);

// The AIRCRAFT.DAT fields of the flight model and the AI.
struct AircraftParams {
  std::int32_t max_speed_kn = 0;  // +124 (knots, despite the docs' mph)
  std::int32_t length_ft = 0;     // +132
  std::int32_t span_ft = 0;       // +136, also the radar cross-section base
  std::int32_t height_ft = 0;     // +140
  std::int32_t guns = 0;          // +148
  std::int32_t roll_rate = 0;     // +160, tenths
  static AircraftParams from(const AircraftType& type);
};

// The scenario fields, set by the spawner before init (`scen_spawn_aircraft`).
struct AircraftSetup {
  const AircraftType* type = nullptr;  // +0x20c (must outlive the aircraft)
  AircraftMissionCode mission = AircraftMissionCode::kPatrol;  // +0x344
  std::int32_t crew = 0;               // +0x22c
  std::int32_t bomb_load = 0;          // +0x230
  bool strafes = false;                // +0x238
  std::int32_t orbit_x = 0;            // +0x23c
  std::int32_t orbit_z = 0;            // +0x240
  std::int32_t orbit_radius = 0;       // +0x244 (world units)
  Route route;                         // +0x254 / +0x2b8 / +0x320
  std::int32_t route_index = 0;        // +0x31c
  std::int32_t escort_group = 0;       // +0x324
  std::int32_t sorties = 1;            // +0x364: 1, or 10000 when it relaunches
};

// The flight block (+0x158..+0x208).
struct AircraftFlight {
  fx::Vec3 position;                 // +0x158 (y the altitude in world units)
  std::int64_t pos_x = 0;            // +0x164.. << 16
  std::int64_t pos_y = 0;
  std::int64_t pos_z = 0;
  fx::Mat3 orientation;              // +0x188
  fx::Mat3 inverse;                  // +0x1ac
  Angle10 heading;                   // +0x1d0 (not folded above 3600)
  Angle10 pitch;                     // +0x1d4, positive nose down
  Angle10 roll;                      // +0x1d8
  std::int64_t heading_acc = 0;      // +0x1dc
  std::int64_t pitch_acc = 0;        // +0x1e4
  std::int64_t roll_acc = 0;         // +0x1ec
  std::int32_t throttle = 100;       // +0x1f4, %
  std::int32_t wanted_heading = -1;  // +0x1f8, -1: hold the bank
  std::int32_t ordered_altitude_ft = 0;  // +0x1fc
  std::int32_t bank_target = 0;      // +0x200
  Fixed16 speed;                     // +0x204 (kn)
  std::int32_t speed_tenths = 0;     // +0x208
};

// The AI fields.
struct AircraftAi {
  AircraftState state = AircraftState::kMission;  // +0x24c
  std::int32_t think_time = 0;       // +0x104
  fx::Vec3 contact;                  // +0xdc
  Angle10 contact_heading;           // +0xe8
  Fixed16 contact_speed;             // +0xec
  std::int32_t contact_age = -1;     // +0x358, 16.16 s; -1 none.
  Vehicle* target = nullptr;         // +0x354
  Vehicle* debris = nullptr;         // +0x100, the last debris seen.
  bool friendly_near_contact = false;  // +0xf4
  bool wingman_bombing = false;      // +0xf8
  bool in_release_window = false;    // +0x35c
  std::int32_t last_range = 0;       // +0xfc
  std::int32_t timer = 0;            // +0x108
  std::int32_t shadow_time = 0;      // +0x10c, (3 + rand(3)) min at init.
  std::int32_t opening_time = 0;     // +0x118, also the guns' reload.
  std::int32_t drops = 0;            // +0x11c
  std::int32_t param = 0;            // +0x120: orbit side, shadow radius (yd), home roll.
  std::int32_t shadow_side = 0;      // +0x124
  std::int32_t shadow_bearing = 0;   // +0x128
  std::int32_t counter = 0;          // +0x130: thinks opening, rounds fired.
  std::int32_t release_offset = 0;   // +0x134, yd, or the strafing range.
  std::int32_t shadow_x = 0;         // +0x13c
  std::int32_t shadow_z = 0;         // +0x140
  std::int32_t bombs = 0;            // +0x234, left.
  std::int32_t sighting_range_yd = 0;  // +0x248
  std::int32_t hit_points = 0;       // +0x250
};

class Aircraft : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  Aircraft() { set_side(1); }

  VehicleClass class_id() const override { return VehicleClass::kAircraft; }
  // `aircraft_init` (0x839a4): one draw (rand(3)); landed and gone.
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 2; }
  std::int32_t max_time_compression() const override { return 0x40; }
  // `aircraft_update` (0x83f07), while airborne.
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return flight_.position; }
  // `aircraft_update_bbox` (0x83ca2), while airborne.
  void update_bounds() override;
  // `aircraft_take_damage` (0x84837): one point per machine-gun round.
  bool take_damage(SimContext& ctx, Vehicle* attacker, DamageKind kind, std::int32_t distance,
                   std::int32_t amount) override;
  void forget(const Vehicle& removed) override;
  // `aircraft_receive_contact` (0x15dab): taken whenever its own contact is
  // not fresh, at any range.
  void receive_contact(SimContext& ctx, const fx::Vec3& position, Vehicle* target) override;

  Angle10 heading() const override { return flight_.heading; }
  Fixed16 speed() const override { return flight_.speed; }
  // Its sighting range while airborne.
  std::int32_t comms_range_yd() const override { return airborne_ ? ai_.sighting_range_yd : 0; }
  bool is_radar_target() const override { return true; }
  // `aircraft_radar_cross_section` (0x84b1f): span x 100 .. x 200, the most
  // when the player's heading is abeam of the bearing to the player.
  std::int32_t radar_cross_section(const fx::Vec3& observer, Angle10 observer_heading) const override;
  std::int32_t noise_level() const override { return 0; }
  bool is_contact() const override { return true; }
  bool sighted() const override { return sighted_; }
  void mark_sighted(const GameClock& clock) override;
  void refresh_sighted() override { sighted_ = false; }
  std::int32_t visual_height() const override { return flight_.position.y; }
  bool is_aircraft() const override { return true; }
  // `aircraft_escorted_group`.
  std::int32_t escorted_group() const override {
    return setup_.mission == AircraftMissionCode::kEscort ? setup_.escort_group : 0;
  }

  // Takes off at `position` (`air_sorties_relaunch`): reruns init, then
  // airborne with its bombs, the sighting range `range_yd`, one sortie less.
  void take_off(SimContext& ctx, const fx::Vec3& position, std::int32_t range_yd);
  // Follows convoy `leader` (`convoy_regroup_on_leader`, escort missions).
  void set_escort_leader(const Ship* leader);

  bool airborne() const { return airborne_; }
  bool crashing() const { return crashing_; }
  // +0x350: 16.16 s until it may take off again.
  std::int32_t relaunch_timer() const { return relaunch_timer_; }
  void set_relaunch_timer(std::int32_t t) { relaunch_timer_ = t; }
  AircraftSetup& setup() { return setup_; }
  const AircraftSetup& setup() const { return setup_; }
  AircraftFlight& flight() { return flight_; }
  const AircraftFlight& flight() const { return flight_; }
  AircraftAi& ai() { return ai_; }
  const AircraftAi& ai() const { return ai_; }
  const AircraftParams& params() const { return params_; }

 private:
  // aircraft_update_flight (0x842de); `ground` the terrain height, at least 0.
  void fly(std::int32_t dt, std::int32_t ground);
  void think(SimContext& ctx, std::int32_t dt); // ai_aircraft_think
  std::int32_t range_to(std::int32_t x, std::int32_t z, std::int32_t ox = 0, std::int32_t oz = 0);
  bool drop_bomb(SimContext& ctx, std::int32_t dt);  // ai_aircraft_drop_bomb (0x15a75)
  void reset_flight();

  AircraftSetup setup_;
  AircraftParams params_;
  AircraftFlight flight_;
  AircraftAi ai_;
  Vehicle* attacker_ = nullptr;     // +0xd4
  const Ship* leader_ = nullptr;    // +0x328
  std::int64_t leader_dx_ = 0;      // +0x32c
  std::int64_t leader_dz_ = 0;      // +0x334
  bool sighted_ = false;            // +0x33c
  bool airborne_ = false;           // +0x340
  bool crashing_ = false;           // +0x348
  std::int32_t relaunch_timer_ = 0; // +0x350
  std::int32_t flyby_timer_ = 0;    // +0xd8
  std::int32_t smoke_timer_ = 0;    // +0x138
};

// A smoke puff of a crashing aircraft (class 15), 0.5 s.
class Smoke : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  explicit Smoke(const fx::Vec3& position) : position_(position) {}

  VehicleClass class_id() const override { return VehicleClass::kSmoke; }
  // `smoke_init` (0x8b7db).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  // `smoke_update` (0x8b838).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }
  // 16.16 s left (the drawn size grows as it falls).
  std::int32_t lifetime() const { return lifetime_; }

 private:
  fx::Vec3 position_;          // +0xd0
  std::int32_t lifetime_ = 0;  // +0xdc
};

}  // namespace sh
