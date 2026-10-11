// Radar emissions (not in the original): the extended variant's Japanese
// radars, the APR-1 radar detector that hears them (rule `apr1`) and the
// escorts' radar detectors that hear the boat's SD and SJ (rule
// `detectable_radar`). Both are rule and simulation changes; neither
// draws from the game's random generator.
//
// Japanese sets, by unit type and date (assumed from Friedman's and
// Parillo's dates; month 0-based, year since 1930):
// - metric band (Type 21 / Type 13, 1.5-2 m; the H-6 on aircraft);
// - 10 cm band (Type 22).
// A ship transmits at night, in fog or when alerted; an airborne aircraft
// always. The APR-1 hears a set up to 1.5 times its own range and the radio
// horizon between the two antennas, land permitting; it gives the bands and
// a strength of 1 (faint, the outer third) to 3 (strong, the inner third),
// no bearing.
#pragma once

#include <cstdint>
#include <string>

#include "assets/dat_tables.h"
#include "sim/context.h"
#include "sim/sensor_environment.h"

namespace sh {

class PlayerSub;
class RadarSets;
class Ship;
class Vehicle;

// The radar bands of a Japanese unit.
struct RadarBands {
  bool metric = false;
  bool centimetric = false;

  bool any() const { return metric || centimetric; }
  friend bool operator==(const RadarBands&, const RadarBands&) = default;
};

// Returns the radar of a ship type on `month` of `year`.
RadarBands ship_radar_bands(ShipType type, std::int32_t month, std::int32_t year);
// Returns the radar of an aircraft role on `month` of `year`.
RadarBands aircraft_radar_bands(AircraftRole role, std::int32_t month, std::int32_t year);

// Returns the radio horizon (yards) between antennas `a_ft` and `b_ft` above
// the sea: 2490 x (sqrt(a) + sqrt(b)), each height at least 1 ft.
std::int32_t radio_horizon_yd(std::int32_t a_ft, std::int32_t b_ft);

// What the APR-1 hears in one pass.
struct Apr1Signal {
  RadarBands bands;
  std::int32_t strength = 0;  // 0 nothing, 1 faint, 2 fair, 3 strong.

  friend bool operator==(const Apr1Signal&, const Apr1Signal&) = default;
};

// Returns the Japanese transmissions the boat's APR-1 hears now; nothing
// below radar depth or with the SD damaged (the two sets share their mast).
Apr1Signal apr1_listen(const SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env);

// Returns the text the extended reports append to message #61 ("Radar
// emissions detected on APR-1"): the strength in a word, ": faint",
// ": fair" or ": strong", short enough for the message strip of the
// periscope's command bar (347 px).
std::string apr1_report_suffix(const Apr1Signal& signal);

// The APR-1's operator: message #61 with the strength when
// transmissions are heard after a minute of silence, and again when they get
// stronger (at most every 30 s). Each report resets the time compression.
class Apr1Receiver {
 public:
  struct State {
    std::int32_t silence = 0x3c0000;      // 16.16 s since a transmission was heard.
    std::int32_t since_report = 0x3c0000; // 16.16 s since the last report.
    std::int32_t reported = 0;            // The strength last reported, 0 none.

    friend bool operator==(const State&, const State&) = default;
  };

  // Listens for `dt` (16.16 s) when the rule is on and the APR-1 fitted.
  void update(SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env, std::int32_t dt);

  State& state() { return state_; }
  const State& state() const { return state_; }

 private:
  State state_;
};

// Returns whether escort `ship` carries a radar detector on `month` of
// `year`: none before 1944, then a third, two thirds in the second half of
// 1944 and three quarters from 1945, picked by its start position and type.
bool escort_has_radar_detector(const Ship& ship, std::int32_t month, std::int32_t year);

// Returns the largest error (tenths of a degree) of the detectors' bearing
// after the boat's radar has been heard for `heard` (16.16 s) without a
// break: 45 deg at first, narrowing evenly to 10 deg after 3 minutes
// (assumed: the operators refine a rough bearing on a steady signal).
std::int32_t detector_error_limit(std::int32_t heard);

// Returns the error of an escort's detector bearing, within
// +-`detector_error_limit(heard)`, its share of the limit fixed by the
// escort's start point and type for each 5 minutes of game time (no random
// draws).
std::int32_t detector_bearing_error(const Ship& ship, std::int32_t minutes_elapsed, std::int32_t heard);

// The escorts with a detector that hear the boat's SD (or, from July 1944,
// its SJ; never the ST) within 1.5 times the set's range and the radio
// horizon become alerted and search along a coarse bearing: their contact
// (`Ship::receive_radar_intercept`) is the point at half that hearing range
// on the true bearing off by `detector_bearing_error`. A detector gives no
// range and at best a rough bearing, so never the boat's position.
class EscortDetectors {
 public:
  struct State {
    // 16.16 s the boat's radar has been heard by an escort without a break,
    // at most 3 minutes; 0 once no escort hears it.
    std::int32_t heard = 0;

    friend bool operator==(const State&, const State&) = default;
  };

  // Listens for `dt` (16.16 s) when the rule is on.
  void update(SimContext& ctx, PlayerSub& player, const RadarSets& radar, const SensorEnvironment& env,
              std::int32_t dt);

  State& state() { return state_; }
  const State& state() const { return state_; }

 private:
  State state_;
};

}  // namespace sh
