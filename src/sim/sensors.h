// The sensors of a mission: their environment (sensor_environment.h), the
// player's radar sets and crew contact reports, the AI frame counter and
// the quiet time. The simulation owns one (`Simulation::sensors()`); the
// mission fills its environment.
//
// Per frame, in the original order: the player boat runs
// `update_player_reports` at the start of its update (`player_update`),
// then `update_player_radar` after its ramming check, the ships think in their updates, and the simulation calls every
// vehicle's hydrophone slot after the passes and `end_frame` last (the
// mission loop's 0..7 frame counter).
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "core/random.h"
#include "sim/contacts.h"
#include "sim/context.h"
#include "sim/fathometer.h"
#include "sim/radar.h"
#include "sim/radar_emissions.h"
#include "sim/sensor_environment.h"
#include "sim/sighting.h"

namespace sh {

class PlayerSub;

class Sensors {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  SensorEnvironment& environment() { return env_; }
  const SensorEnvironment& environment() const { return env_; }
  RadarSets& radar() { return radar_; }
  const RadarSets& radar() const { return radar_; }
  ContactReports& reports() { return reports_; }
  const ContactReports& reports() const { return reports_; }
  // Not in the original: the extended APR-1's operator.
  Apr1Receiver& apr1() { return apr1_; }
  const Apr1Receiver& apr1() const { return apr1_; }
  // Not in the original: the extended escorts' radar detectors.
  EscortDetectors& escort_detectors() { return escort_detectors_; }
  const EscortDetectors& escort_detectors() const { return escort_detectors_; }
  // Not in the original: the extended fathometer soundings.
  Fathometer& fathometer() { return fathometer_; }
  const Fathometer& fathometer() const { return fathometer_; }

  // The first sensor part of `player_update` (0x5bf89) for `dt` (16.16 s):
  // the quiet time grows, the crew reports contacts.
  void update_player_reports(SimContext& ctx, PlayerSub& player, Fixed16 dt);
  // Its second part, after the ramming check: the radars sweep; then (not
  // in the original, by the extended rules) the APR-1 listens, the
  // escorts' detectors hear the boat's radars and the fathometer takes the
  // sounding asked for.
  void update_player_radar(SimContext& ctx, PlayerSub& player, Fixed16 dt);
  // Drops the pointers to `removed`, about to be deleted.
  void forget(const Vehicle& removed);
  // Advances the frame counter (once per mission frame).
  void end_frame() { frame_counter_ = frame_counter_ == 7 ? 0 : frame_counter_ + 1; }

  // 0x11a89c: the frame of eight the ships think on at 1x (id % 8).
  std::int32_t frame_counter() const { return frame_counter_; }
  // 0x11a8a8: 16.16 s since any contact was heard or seen (the mission ends
  // an encounter after a minute of it).
  std::int32_t quiet_time() const { return quiet_time_; }
  void reset_quiet_time() { quiet_time_ = 0; }
  // 0x11ac00: a hostile ship's course meets the boat within 30 s.
  bool collision_warning() const { return collision_warning_; }
  void set_collision_warning(bool on) { collision_warning_ = on; }

 private:
  SensorEnvironment env_;
  RadarSets radar_;
  ContactReports reports_;
  Apr1Receiver apr1_;
  EscortDetectors escort_detectors_;
  Fathometer fathometer_;
  std::int32_t frame_counter_ = 0;
  std::int32_t quiet_time_ = 0;
  bool collision_warning_ = false;
};

// Returns the player boat's lookout state (heights from its class).
PlayerLookoutState player_lookout_state(const PlayerSub& player, std::int32_t crew);

// Returns the sensors of the context's simulation, or null.
Sensors* context_sensors(SimContext& ctx);

// Returns the thermal layer depth (ft): 10000 (none) for a 0 parameter,
// else parameter x 100 + rand(100) (`init_thermal_layer`, 0x4fba9).
std::int32_t init_thermal_layer(WatcomRandom& random, std::int32_t parameter);

}  // namespace sh
