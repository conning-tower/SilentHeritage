// The simulation core of a mission: the clock, the vehicle list and the
// per-frame stepping of `mission_main_loop` (clock_advance, then
// `veh_simulate_frame` running the update passes in sub-steps). It is
// deterministic: a frame is driven by the number of 100 Hz timer ticks it
// lasted.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "core/random.h"
#include "sim/clock.h"
#include "sim/context.h"
#include "sim/motion.h"
#include "sim/sensors.h"
#include "sim/vehicle.h"

namespace sh {

class PlayerCombat;

class Simulation {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A mission in `env` over `floor` (which must outlive the simulation),
  // with the random sequence started from `seed`.
  Simulation(const MissionConditions& env, const SeaFloor& floor, std::uint32_t seed = 1);

  // Adds a vehicle and initializes it (the spawner's construct + slot 4);
  // returns it.
  template <typename T>
  T& add(std::unique_ptr<T> vehicle) {
    T& ref = *vehicle;
    ref.set_list_index(static_cast<std::int32_t>(vehicles_.size()));
    vehicles_.push_back(std::move(vehicle));
    SimContext ctx = context();
    ref.init(ctx);
    return ref;
  }

  // Runs one frame of `timer_ticks` 100 Hz ticks: advances the clock, then
  // the vehicles once at least 0.02 s of game time has gathered, and deletes
  // the removed vehicles. `station_opened` marks a station's first frame.
  void step(std::int32_t timer_ticks, bool station_opened = false);

  // Runs the update passes for `dt` (16.16 s) (`veh_simulate_frame`'s core):
  // each pass sub-steps its vehicles by at most 0.02 s (pass 0) or
  // (pass + 1) x 0.1 s, doubled for passes 2+ above 4x compression.
  void simulate(Fixed16 dt);

  GameClock& clock() { return clock_; }
  const GameClock& clock() const { return clock_; }
  WatcomRandom& random() { return random_; }
  const MissionConditions& conditions() const { return env_; }
  // The conditions a career patrol changes as its weather moves.
  MissionConditions& conditions() { return env_; }
  const std::vector<std::unique_ptr<Vehicle>>& vehicles() const { return vehicles_; }
  // Sets the receiver of the clock's minute and day events.
  void set_clock_observer(ClockObserver* observer) { clock_observer_ = observer; }
  // Returns the events raised since the last call and clears them.
  MissionEvents take_events();
  // Returns the context the vehicles update in, for orders given between
  // frames (e.g. the player's dive commands).
  SimContext context() { return {clock_, random_, floor_, env_, events_, this}; }

  // Sets the player's weapons and damage state (owned by the mission, must
  // outlive the simulation); null without a player boat.
  void set_player_combat(PlayerCombat* combat) { combat_ = combat; }
  PlayerCombat* player_combat() const { return combat_; }
  // Returns the player boat (class 0, the first vehicle of the original's
  // list), or null.
  Vehicle* player() const;
  // The sensors' state and environment (sensors.h).
  Sensors& sensors() { return sensors_; }
  const Sensors& sensors() const { return sensors_; }

 private:
  void simulate_pass(std::int32_t dt, std::int32_t pass, std::int32_t& cap);

  MissionConditions env_;
  const SeaFloor& floor_;
  GameClock clock_;
  WatcomRandom random_;
  MissionEvents events_;
  ClockObserver* clock_observer_ = nullptr;
  std::vector<std::unique_ptr<Vehicle>> vehicles_;
  std::int32_t pending_hundredths_ = 0;  // 0x129d04
  PlayerCombat* combat_ = nullptr;
  Sensors sensors_;
};

}  // namespace sh
