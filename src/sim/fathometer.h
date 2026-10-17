// The fathometer soundings (rule `fathometer_soundings`, not in the
// original; a rule and simulation change that draws nothing from the game's
// random generator). Every US boat carried an echo sounder, but its ping can
// be heard: the gauges' fathometer then shows only the depth under the keel
// of the last sounding the player takes, and each sounding is a ping that
// the escorts' sonar operators may hear.
//
// An escort with sonar hears the ping within its echo-ranging range
// (`sonar_active_range_yd`, 2400 to 3600 yd in a calm sea by its crew, less
// in a rougher sea or over 12 kn; assumed: the sounder sends straight down,
// so its ping carries sideways about as far as an escort's own echoes come
// back), unless the boat is under the thermal layer, the escort hears it in
// its baffles, or its sonar is deafened (`sonar_deafened`: with the
// corrections and without UNDEAFENED ESCORTS, the noise of a depth charge's
// blast within 2400 yd of the escort for the 3 minutes it lasts). A ping
// taken in the noise of the blasts is therefore not heard: the tactic of the
// boats sounding while being depth charged.
//
// An escort that hears it becomes alerted with a contact on the true bearing
// at half its hearing range (a ping gives a bearing but no range), as the
// radar detectors' (`Ship::receive_radar_intercept`).
#pragma once

#include <cstdint>
#include <optional>

#include "sim/context.h"

namespace sh {

class PlayerSub;
class Ship;

class Fathometer {
 public:
  struct State {
    // Whether a sounding was taken, and its keel clearance (world units,
    // positive aground).
    bool sounded = false;
    std::int32_t keel_clearance = 0;
    // The soundings taken in the mission (the gauge redraws on a new one).
    std::int32_t soundings = 0;

    friend bool operator==(const State&, const State&) = default;
  };

  // Asks for a sounding, taken at the next update.
  void request() { requested_ = true; }
  // Takes the sounding asked for when the rule is on: holds the keel
  // clearance, plays the ping (sfx 0x32) and alerts the escorts that hear
  // it. Without the rule a request is dropped.
  void update(SimContext& ctx, PlayerSub& player);

  // The keel clearance of the last sounding, none before the first.
  std::optional<std::int32_t> sounded_keel_clearance() const {
    return state_.sounded ? std::optional<std::int32_t>(state_.keel_clearance) : std::nullopt;
  }

  State& state() { return state_; }
  const State& state() const { return state_; }

 private:
  bool requested_ = false;
  State state_;
};

// Returns whether escort `ship` hears a sounding of `player` now (above).
bool hears_sounding(const Ship& ship, const PlayerSub& player, const SimContext& ctx);

}  // namespace sh
