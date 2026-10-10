// The motion model shared by ships, submarines and torpedoes (`motion_*`,
// 0x8cdf7..0x8e4a9 in veh.cpp): engine power against a cubic drag law,
// sideslip damping, rudder-driven yaw, integration in 48.16 world units,
// keel clearance, the nautical-mile log and the wake trail.
//
// Units: positions in world units (16 = 1 ft, 48 = 1 yd), velocities in
// 16.16 world units per second, speeds in 16.16 knots (1 kn = 27.008 world
// units/s), angles in tenths of a degree, power in the classes' 16.16 "hp"
// (thrust is power x 550).
#pragma once

#include <array>
#include <cstdint>

#include "core/fixed.h"
#include "core/fixed_math.h"

namespace sh {

// World units in one nautical mile (6077 ft), the original's knot base.
inline constexpr std::int32_t kUnitsPerNauticalMile = 0x17bd0;

// The hull block `motion_step` reads: a SHIPS.DAT record from +176, or the
// block a submarine assembles from SUBS.DAT (see sub_hull_params in dive.h).
struct HullParams {
  std::int32_t length_ft = 0;          // SHIPS.DAT +176; half of it is the yaw arm.
  std::int32_t beam_ft = 0;            // SHIPS.DAT +180.
  std::int32_t draft_ft = 0;           // SHIPS.DAT +184; sets the keel clearance.
  std::int32_t displacement = 0;       // SHIPS.DAT +196 (tons); mass = x 2200 lb.
  Fixed16 drag_cubic;                  // SHIPS.DAT +208 (`c` in D = (c v)^3 + b v).
  Fixed16 drag_linear;                 // SHIPS.DAT +216 (`b`).
  Fixed16 lateral_drag;                // SHIPS.DAT +224, sideslip damping.
  Fixed16 astern_drag_factor;          // SHIPS.DAT +228, drag multiplier going astern.
  Fixed16 turn_factor;                 // SHIPS.DAT +232, yaw-rate divisor and waypoint radius.
};

// The motion parameters of a ship class.
struct ShipMotionParams {
  HullParams hull;
  std::int32_t max_power = 0;          // SHIPS.DAT +112, the engine power cap (16.16 raw "hp").
};

// The sea floor under the vehicles (the world terrain grid).
class SeaFloor {
 public:
  virtual ~SeaFloor() = default;
  // Returns the world y (world units, up positive) of the terrain at (x, z)
  // (`world_terrain_height_at`).
  virtual std::int32_t height_at(std::int32_t x, std::int32_t z) const = 0;
};

// A sea floor at a constant height everywhere (open ocean of known depth).
class FlatSeaFloor : public SeaFloor {
 public:
  explicit FlatSeaFloor(std::int32_t height) : height_(height) {}
  std::int32_t height_at(std::int32_t, std::int32_t) const override { return height_; }

 private:
  std::int32_t height_;
};

// The motion block embedded in each vehicle (ship +0x228, enemy sub +0x1e4,
// player 0x11a9c4). Field comments give the offsets within the block.
struct MotionState {
  static constexpr int kMaxWakePoints = 10;

  fx::Vec3 position;                   // +0x00 world units, y up.
  std::int64_t pos_x = 0;              // +0x0c..+0x20 position << 16, integrated.
  std::int64_t pos_y = 0;
  std::int64_t pos_z = 0;
  fx::Vec3 velocity;                   // +0x24 world frame, 16.16 units/s.
  fx::Mat3 orientation;                // +0x30 body -> world.
  fx::Mat3 inverse;                    // +0x54 world -> body.
  Angle10 heading;                     // +0x78
  std::int32_t heading_rad = 0;        // +0x7c 16.16 radians, integrated.
  Angle10 ordered_heading;             // +0x80 steered to when `steering`.
  Angle10 pitch;                       // +0x84 (submarines' dive angle).
  std::int32_t pitch_rad = 0;          // +0x88
  std::int32_t power = 0;              // +0x8c current thrust (power x 550, 16.16).
  std::int32_t ordered_power = 0;      // +0x90 ordered power (16.16 "hp").
  Fixed16 speed;                       // +0x94 knots (forward component).
  std::int32_t speed_tenths = 0;       // +0x98 tenths of a knot (>= -5 kn).
  std::int32_t rudder = 0;             // +0x9c tenths of a degree.
  bool steering = false;               // +0xa0 rudder follows ordered_heading.
  std::int32_t log_units = 0;          // +0xa4 distance since the last mile.
  std::int32_t log_miles = 0;          // +0xa8 nautical miles run.
  std::int32_t keel_clearance = 0;     // +0xac terrain + draft - y (> 0: aground).
  fx::Vec3 acceleration;               // +0xb0 body frame, 16.16 units/s^2.
  std::int32_t wake_timer = 0;         // +0xbc 16.16 s.
  std::int32_t wake_count = 0;         // +0xc0
  std::array<fx::Vec3, kMaxWakePoints> wake{};  // +0xc4 oldest first.
  std::int32_t wake_interval_s = 20;   // +0x13c
  std::int32_t rudder_max = 250;       // +0x140 rudder at 60 deg of heading error.
};

// Prepares a block whose position, heading, pitch, speed and power (16.16
// "hp", converted to thrust) are set (`motion_init`, 0x8cf92). As in the
// original, the keel clearance is left as the bare terrain height until the
// first step.
void motion_init(MotionState& m, const SeaFloor& floor);

// Advances the block by `dt` (16.16 s) (`motion_step`, 0x8d244). `sea_state`
// is the scenario's 0..2 and `wave_pct` the share of the sea-state drag
// applied (100 for surface ships, sub_wave_effect_pct for submarines).
void motion_step(MotionState& m, const HullParams& hull, std::int32_t sea_state, Fixed16 dt, std::int32_t wave_pct,
                 const SeaFloor& floor);

// Advances the wake trail by `dt` (16.16 s): every `wake_interval_s` a
// point is added where the vehicle is (the oldest dropped when full), or
// dropped when it is stopped (the tail of `motion_step`; convoy members
// moved with their leader run it alone).
void motion_update_wake(MotionState& m, std::int32_t dt);

// Returns the power (16.16 "hp") holding `speed` knots, capped at
// `max_power` (`motion_power_for_speed`, 0x8dc90).
std::int32_t motion_power_for_speed(Fixed16 speed, std::int32_t max_power, const HullParams& hull,
                                    std::int32_t sea_state, std::int32_t wave_pct);

// Returns the speed that `power` sustains, searched in 0.1 kn steps until
// the required power reaches it or 30000 (`motion_speed_for_power`, 0x8dde3).
Fixed16 motion_speed_for_power(std::int32_t power, const HullParams& hull, std::int32_t sea_state,
                               std::int32_t wave_pct);

// A route: up to 25 points; an x of -1 right after the last point makes the
// route loop back to the first.
struct Route {
  static constexpr int kCapacity = 25;
  std::array<std::int32_t, kCapacity> x{};
  std::array<std::int32_t, kCapacity> z{};
  std::int32_t count = 0;
};

// Steers toward waypoint `index` and advances `index` once within
// turn_factor x 34 x angle_off / 180 deg + 100 yd (`motion_steer_to_waypoint`,
// 0x8de53). Does nothing when the route is empty or finished.
void motion_steer_to_waypoint(MotionState& m, Fixed16 turn_factor, const Route& route, std::int32_t& index);

// Returns the % of sea-state drag felt at `depth_ft` by a submarine: 100 at
// or above `surface_depth_ft`, falling to 0 at 80 ft, 0 in a calm sea
// (`sub_wave_effect_pct`, 0x8cdf7). `sea_state` must be 0..2.
std::int32_t sub_wave_effect_pct(std::int32_t sea_state, std::int32_t depth_ft, std::int32_t surface_depth_ft);

}  // namespace sh
