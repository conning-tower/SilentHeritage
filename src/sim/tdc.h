// The Torpedo Data Computer (`tdc_*` in tdc.cpp, state 0x129a20..0x129b9c):
// the target solution (bearing, range, course and speed), estimated from a
// marked target in auto mode or dialled in and dead-reckoned in manual
// mode, and from it the gyro angle and run time of the bow and stern tubes.
//
// Angles are tenths of a degree, bearings relative to the own heading,
// ranges in yards, speeds in knots (16.16, and tenths of a knot for the
// dials), positions in world units.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "core/fixed_math.h"
#include "sim/motion.h"

namespace sh {

class Vehicle;
class TorpedoVehicle;

// One of the three torpedo types the TDC offers (`0x10f550`: name,
// available, weapon id), filled for the mission by
// `tdc_upgrade_mark18_by_date`.
struct TorpedoChoice {
  std::string name;            // "MARK10", "MARK14", "MARK18-1"
  bool available = false;
  std::uint32_t weapon_id = 0;  // WEAPONS.DAT `Torpedo::id`.
};

// What the sighting code knows of a target (the vehicle's slots +0x80
// spotted, +0x8c visual height, +0x84 tracking time, and the land check).
struct TargetSensing {
  bool spotted = true;
  std::int32_t visual_height = 1;  // < 1: out of sight.
  Fixed16 track_time;              // Seconds the crew has tracked it.
  bool line_of_sight = true;       // No land between the boat and it.
};

// Returns the sensing of a vehicle (provided by the sighting code).
using TargetSensingFn = std::function<TargetSensing(const Vehicle&)>;

// The kinematics of a target (slots +0x18 position, +0x1c heading, +0x20
// speed, +0x24 maximum speed) and its tracking time.
struct TargetState {
  fx::Vec3 position;
  Angle10 heading;
  Fixed16 speed;                   // Knots.
  std::int32_t max_speed_kts = 0;  // SHIPS.DAT `top_speed_kts` (atoi).
  Fixed16 track_time;
};

// Returns the target state of `target` from its slots; the maximum speed
// is a ship class's (uncapped otherwise).
TargetState target_state(const Vehicle& target, const TargetSensing& sensing);

// The own boat as the fire-control code sees it.
struct OwnBoat {
  const MotionState& motion;      // Position, heading, orientation.
  std::int32_t length_ft = 0;     // SUBS.DAT +148 (`0x11b1b4`).
  std::uint32_t options = 0;      // `0x11a85c`
  std::int32_t crew = 0;          // `0x11ac2b`, 0..2 (2 best).
};

// The output of `tdc_estimate_solution` (0x7bd2e).
struct SolutionEstimate {
  std::int32_t quality = 0;        // %, from the range against the sighting
                                   // range, the realism floor and tracking.
  Angle10 bearing;                 // Relative, exact.
  std::int32_t range = 0;          // World units, skewed by the error.
  Angle10 course;                  // Skewed by up to 4.5 deg per error point.
  Fixed16 speed;                   // Skewed, capped at the maximum speed.
  std::int32_t speed_tenths = 0;
  std::int32_t x = 0;              // The estimated target position.
  std::int32_t z = 0;
};

// Returns the crew's estimate of `target` seen from the own position moved
// by (offset_x, offset_z) (a deck-gun mount), with the target's sighting
// range `sighting_range_yd` (`tdc_estimate_solution`, 0x7bd2e).
SolutionEstimate tdc_estimate_solution(const TargetState& target, std::int32_t sighting_range_yd,
                                       std::int32_t offset_x, std::int32_t offset_z, const OwnBoat& own);

class Tdc {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  enum class Mode : std::int32_t {
    kOff = 0,
    kManual = 1,  // Dialled solution, dead-reckoned.
    kAuto = 2,    // Estimated from the marked target.
  };
  static constexpr std::int32_t kBow = 0;
  static constexpr std::int32_t kStern = 1;
  static constexpr std::size_t kTrackedTorpedoes = 28;

  // Sets the start-of-mission state (`tdc_reset_state`, 0x7b047): auto
  // mode, no target, run depth 10 ft, high speed, no spread.
  void reset();

  // Controls.
  // Sets the mode, unlocking the target (`tdc_set_mode`, 0x7c76c); off
  // also zeroes the gyro angles.
  void set_mode(Mode mode);
  // The mark key (`tdc_mark_target`, 0x7c83e). Manual mode: takes the view
  // bearing (relative) as the solution bearing and places the target at
  // the dialled range on it, clearing the track. Auto mode: unlocks a
  // locked target, else picks the target on the view bearing and locks it.
  void mark_target(const OwnBoat& own, Angle10 view_bearing, const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                   const TargetSensingFn& sensing);
  // Picks the nearest live, visible surface target within 0.9 deg of the
  // view bearing when unlocked in auto mode (`tdc_pick_target_on_bearing`,
  // 0x7c4ac); the TBT calls it every frame.
  void pick_target_on_bearing(const OwnBoat& own, Angle10 view_bearing,
                              const std::vector<std::unique_ptr<Vehicle>>& vehicles, const TargetSensingFn& sensing);
  // Toggles the solution track (the TRACK button, 0x7d0b9).
  void toggle_track() { tracking_ = !tracking_; }
  // Toggles the bearing follow while locked; `off` forces it off
  // (`tdc_toggle_bearing_follow`, 0x7cc69).
  void toggle_bearing_follow(bool off);
  // Toggles the speed setting (`tdc_toggle_torpedo_speed`, 0x7ce07).
  void toggle_speed() { speed_setting_ ^= 1; }
  // Selects the next available torpedo type (`tdc_cycle_torpedo_type`,
  // 0x7ce18); at least one must be available.
  void cycle_type(const std::array<TorpedoChoice, 3>& choices);
  void set_type_index(std::int32_t index) { type_index_ = index; }
  // Manual dials; each change invalidates the track.
  void set_bearing(Angle10 bearing) { bearing_ = bearing; tracking_ = false; }
  void set_range_yd(std::int32_t range) { range_yd_ = range; tracking_ = false; }
  void set_course(Angle10 course) { course_ = course; tracking_ = false; }
  void set_speed_tenths(std::int32_t tenths) { speed_tenths_ = tenths; tracking_ = false; }
  // The manual gyro dial of the bow or stern tubes (the gyro panel's knob
  // writes 0x129a38 / 0x129a3c), which also invalidates the track.
  void set_gyro(std::int32_t tube_end, Angle10 gyro) {
    gyro_[static_cast<std::size_t>(tube_end)] = gyro;
    tracking_ = false;
  }
  // Spread offset 0..400 (above 200: negative), run depth in feet.
  void set_spread(std::int32_t spread) { spread_ = spread; }
  void set_depth_ft(std::int32_t depth) { depth_ft_ = depth; }
  // The sighting range of the target, from the contact reports
  // (`player_report_contacts` sets 0x129a24).
  void set_sighting_range(std::int32_t yards) { sighting_range_ = yards; }

  // Per-frame update (`tdc_sim_update`, 0x7b394): drops a sunk or lost
  // target, counts the run timers down, and at least every 0.05 s game
  // time recomputes the solution (auto) or dead-reckons it (manual) and the
  // tube solutions. `frame_ticks` is the frame's game hundredths and
  // `compression` the effective time compression. Returns the solution
  // bearing to slave the view to when following, else -1.
  std::int32_t update(std::int32_t frame_ticks, std::int32_t compression, const OwnBoat& own,
                      const GameTables& tables, const std::array<TorpedoChoice, 3>& choices,
                      const TargetSensingFn& sensing);
  // Recomputes the gyro angles and run times of the bow and stern tubes
  // from the solution (`tdc_update_tube_solutions`, 0x7b622).
  void update_tube_solutions(const OwnBoat& own, const GameTables& tables,
                             const std::array<TorpedoChoice, 3>& choices);

  // Fired torpedoes and their run timers (0x129b08 / 0x129a98).
  // Starts the timer of a torpedo fired from `tube_end` with the current
  // run time; a full table drops the oldest (`tdc_track_fired_torpedo`).
  void track_torpedo(std::int32_t tube_end, const TorpedoVehicle* torpedo);
  // Stops tracking a torpedo that detonated or ran out (`tdc_untrack_torpedo`).
  void untrack_torpedo(const TorpedoVehicle* torpedo);
  // Returns the shortest running timer (seconds left, negative once past
  // the expected hit), or nullopt.
  std::optional<std::int32_t> shortest_run_timer() const;
  // The run timers (seconds left, 0x129a98) of the tracked torpedoes and
  // the torpedoes (0x129b08, null: a free slot), for the TDC's run-time dial.
  const std::array<std::int32_t, kTrackedTorpedoes>& run_timers() const { return countdown_; }
  const std::array<const TorpedoVehicle*, kTrackedTorpedoes>& tracked_torpedoes() const { return tracked_; }
  // Drops the target and the tracked torpedo that are `removed`.
  void forget(const Vehicle& removed);

  // State.
  Mode mode() const { return mode_; }
  Vehicle* target() const { return target_; }
  void set_target(Vehicle* target) { target_ = target; }
  bool locked() const { return locked_; }
  bool following() const { return follow_bearing_; }
  bool tracking() const { return tracking_; }
  // The gyro angle of the bow or stern tubes (0..3599, relative to the tube
  // axis) and the run time to the target (s, -1 when the courses never meet).
  Angle10 gyro(std::int32_t tube_end) const { return gyro_[static_cast<std::size_t>(tube_end)]; }
  std::int32_t run_time(std::int32_t tube_end) const { return run_time_[static_cast<std::size_t>(tube_end)]; }
  std::int32_t spread() const { return spread_; }
  // Returns the spread as a signed angle (+-20 deg).
  Angle10 spread_angle() const;
  std::int32_t quality() const { return quality_; }
  Angle10 bearing() const { return bearing_; }
  Angle10 course() const { return course_; }
  std::int32_t range_yd() const { return range_yd_; }
  std::int32_t speed_tenths() const { return speed_tenths_; }
  Fixed16 speed() const { return speed_; }
  fx::Vec3 target_position() const { return {target_x_, 0, target_z_}; }
  std::int32_t depth_ft() const { return depth_ft_; }
  std::int32_t speed_setting() const { return speed_setting_; }
  std::int32_t type_index() const { return type_index_; }
  bool blink() const { return blink_; }

 private:
  void estimate_target(const OwnBoat& own, const TargetSensingFn& sensing);  // tdc_update_target_solution
  void advance_solution(std::int32_t dt, const OwnBoat& own, const GameTables& tables,
                        const std::array<TorpedoChoice, 3>& choices);  // tdc_advance_solution
  // tdc_tube_intercept_time (0x7bad7)
  void add_intercept_time(std::int32_t tube_end, std::int32_t tx, std::int32_t tz, std::int32_t lx, std::int32_t lz,
                          Angle10 tube_heading, std::int32_t torpedo_speed);
  void set_target_position(std::int32_t x, std::int32_t z);

  Vehicle* target_ = nullptr;          // 0x129a20
  std::int32_t sighting_range_ = 0;    // 0x129a24, yards.
  bool blink_ = false;                 // 0x129a28
  Mode mode_ = Mode::kAuto;            // 0x129a2c
  bool locked_ = false;                // 0x129a30
  bool follow_bearing_ = false;        // 0x129a34
  std::array<Angle10, 2> gyro_{};      // 0x129a38 / 0x129a3c
  std::int32_t spread_ = 0;            // 0x129a40
  std::int32_t quality_ = 0;           // 0x129a44
  bool tracking_ = false;              // 0x129a48
  Angle10 bearing_;                    // 0x129a4c
  Angle10 course_;                     // 0x129a50
  std::int32_t range_yd_ = 0;          // 0x129a54
  std::int32_t speed_tenths_ = 0;      // 0x129a58
  Fixed16 speed_;                      // 0x129a5c
  std::int32_t target_x_ = 0;          // 0x129a60
  std::int32_t target_z_ = 0;          // 0x129a64
  std::int64_t target_x16_ = 0;        // 0x129a68 (x << 16)
  std::int64_t target_z16_ = 0;        // 0x129a70
  std::int32_t depth_ft_ = 10;         // 0x129a78
  std::int32_t speed_setting_ = 1;     // 0x129a7c
  std::int32_t type_index_ = 0;        // 0x129a80
  std::int32_t solution_ticks_ = 0;    // 0x129a84, game hundredths.
  std::int32_t blink_ticks_ = 0;       // 0x129a88, real hundredths.
  std::int32_t timer_ticks_ = 0;       // 0x129a8c, game hundredths.
  std::array<std::int32_t, 2> run_time_{};  // 0x129a90
  std::array<std::int32_t, kTrackedTorpedoes> countdown_{};  // 0x129a98
  std::array<const TorpedoVehicle*, kTrackedTorpedoes> tracked_{};  // 0x129b08
};

}  // namespace sh
