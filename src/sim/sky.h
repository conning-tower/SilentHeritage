// The sky and the fog of a mission, driven by the game clock's hooks: the
// daily sky update (`clock_update_sky`, 0x7e08e: sunrise, sunset, moonrise,
// moonset, the moon phase and the day's fog schedule), the sun and moon
// positions refreshed every other minute (`sky_update_sun_or_moon`,
// 0x7e21c) and the fog drift of every game minute (`clock_advance`).
//
// The original keeps this state in globals: the clock's `+0x24..+0x44`
// fields, the fog schedule `0x11b7cc..0x11b7d8`, the fog density `0x170f88`,
// the ship-light flags `0x170f2c..` and the sun and moon handed to the 3D
// world (`world_set_sun` / `world_set_moon`, whose daylight palette update
// yields the light percentage: see `WorldLighting::update_light`).
//
// Usage: build the sky once the map is loaded, call `update_day` where
// `init_spawn_scenario_units` does and `clock_init` at `clock_init`, make it
// the clock's observer, and tell it the player boat's position before each
// frame (`set_observer`).
#pragma once

#include <cstdint>

#include "assets/ephemeris.h"
#include "assets/world.h"
#include "core/fixed.h"
#include "core/random.h"
#include "sim/astronomy.h"
#include "sim/clock.h"

namespace sh {

// The sun and the moon as `world_set_sun` / `world_set_moon` receive them:
// azimuth clockwise from north and elevation, both in [0, 3600) tenths of a
// degree (an elevation of 3500 is -10 degrees), and the moon phase.
struct SkyState {
  Angle10 sun_azimuth;
  Angle10 sun_elevation;
  Angle10 moon_azimuth;
  Angle10 moon_elevation;
  // The clock's `+0x24`: 0 full moon .. 4 new moon (no moon drawn).
  std::int32_t moon_phase = 4;
};

// The fog of the day (`0x11b7cc`).
enum class FogState : std::int32_t {
  // Clear until the return time; the original's drift also raises the fog
  // meanwhile, so it keeps coming back (a bug kept).
  kClearing = -1,
  kSteady = 0,     // No drift (no fog day, or the fog of `clock_init`).
  kFoggy = 1,      // Fog until the lift time.
};

// A time of day packed as the fog schedule keeps it: hour << 16 | minute.
struct FogTime {
  std::int32_t hour = 0;
  std::int32_t minute = 0;
};

// How a generated mission picks its start time (`.SCN` hour -1, the start
// setting in the minute field).
enum class StartTimeSetting : std::int32_t {
  kDawn = 0,
  kDay = 1,
  kDusk = 2,
  kNight = 3,
};

// A start time chosen by `Sky::choose_start_time`.
struct StartTime {
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t second = 0;
};

// Told when a body moved or the fog changed, at the points where the
// original updates the light and fog globals, so that the light percentage
// can be recomputed before the vehicles read it.
class SkyListener {
 public:
  virtual ~SkyListener() = default;
  virtual void on_sky_changed() = 0;
};

class Sky : public ClockObserver {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A sky over `world` (which must have geographic bounds) computed from
  // `ephemeris`, drawing from `random` (the game's generator); all three
  // must outlive the sky. `options` is the options word `0x11a85c` (bit 0:
  // career), `fog_day` the scenario's fog flag (`0x125f68`) and
  // `fog_density` the density rolled by `init_simulation` (`0x11b7d8`).
  Sky(const Ephemeris& ephemeris, const World& world, WatcomRandom& random, std::uint32_t options,
      std::int32_t fog_day, std::int32_t fog_density);

  // Sets the world position the sky is seen from (the player boat,
  // `0x11a9c4`/`0x11a9cc`).
  void set_observer(std::int32_t x, std::int32_t z) {
    observer_x_ = x;
    observer_z_ = z;
  }
  // Sets the receiver of the change notifications (not owned, may be null).
  void set_listener(SkyListener* listener) { listener_ = listener; }
  // Draws from `random` from now on (the mission's generator moves into the
  // simulation after the first daily update).
  void set_random(WatcomRandom& random) { random_ = &random; }
  // Sets an observer told of the clock events after the sky (not owned).
  void set_next_observer(ClockObserver* next) { next_ = next; }
  // Sets the career's daily fog roll (`0x129c68`), which replaces the fog
  // flag at each new day in a career.
  void set_daily_fog(std::int32_t fog) { daily_fog_ = fog; }

  // Runs the daily update (`clock_update_sky`): the sun's and moon's
  // positions, rising and setting, the moon phase and, on a fog day, the
  // day's fog schedule (4 or 5 draws).
  void update_day(const GameClock& clock);
  // Runs `clock_init`'s part: the daily update, then outside a career the
  // fog of a fog day set to its density with no drift.
  void clock_init(const GameClock& clock);
  // Refreshes the sun's or the moon's position (`sky_update_sun_or_moon`).
  void update_body(const GameClock& clock, bool sun);

  // Returns the start time of a generated mission at world point (`x`,
  // `z`) on the date (`clock_init_time_of_day`, 0x7df1d): dawn 35 minutes
  // before sunrise, dusk 20 minutes before sunset, or a random time in the
  // day or the night (2 draws). The sky is seen from there from now on;
  // the original then runs `update_day`. Year since 1930 and month
  // 0-based, as the clock.
  StartTime choose_start_time(StartTimeSetting setting, std::int32_t year, std::int32_t month, std::int32_t day,
                              std::int32_t x, std::int32_t z);

  // ClockObserver: the fog drift, the daily update and the body positions
  // (the sun at even minutes, the moon at odd ones).
  void on_minute_started(const GameClock& clock) override;
  void on_new_day(const GameClock& clock) override;
  void on_minute_ended(const GameClock& clock) override;

  const SkyState& state() const { return state_; }
  // The clock's `+0x28..+0x34` and `+0x38..+0x44`.
  const RiseSetTimes& sun_times() const { return sun_times_; }
  const RiseSetTimes& moon_times() const { return moon_times_; }
  // True while the sun is below the horizon: the ship lights burn
  // (`0x170f2c..0x170f38`).
  bool ship_lights() const { return ship_lights_; }

  // The fog density in percent now (`0x170f88`).
  std::int32_t fog_pct() const { return fog_pct_; }
  // The fog day's density (`0x11b7d8`).
  std::int32_t fog_density() const { return fog_density_; }
  // The fog flag in use (`0x125f68`).
  std::int32_t fog_day() const { return fog_day_; }
  FogState fog_state() const { return fog_state_; }
  // When the fog lifts and returns (`0x11b7d4`, `0x11b7d0`).
  FogTime fog_lift() const { return unpack(fog_lift_); }
  FogTime fog_return() const { return unpack(fog_return_); }

  // Returns and clears the flag the stations check to reshade their palette
  // (`0x129c5c`), set when a body moves or the fog changes.
  bool take_palette_dirty() {
    const bool dirty = palette_dirty_;
    palette_dirty_ = false;
    return dirty;
  }

 private:
  static FogTime unpack(std::int32_t t) { return {t >> 16, t & 0xffff}; }
  // Returns the library's observer at world point (`x`, `z`).
  Observer observer_at(std::int32_t x, std::int32_t z) const;
  // Sets the palette flag and tells the listener.
  void changed();
  // Tells the listener.
  void notify();

  Astronomy astronomy_;
  const World& world_;
  WatcomRandom* random_;
  std::uint32_t options_ = 0;
  SkyListener* listener_ = nullptr;
  ClockObserver* next_ = nullptr;
  std::int32_t observer_x_ = 0;
  std::int32_t observer_z_ = 0;

  SkyState state_;
  RiseSetTimes sun_times_;
  RiseSetTimes moon_times_;
  bool ship_lights_ = false;
  bool palette_dirty_ = false;

  std::int32_t fog_day_ = 0;      // 0x125f68
  std::int32_t daily_fog_ = 0;    // 0x129c68
  std::int32_t fog_pct_ = 0;      // 0x170f88
  std::int32_t fog_density_ = 0;  // 0x11b7d8
  FogState fog_state_ = FogState::kSteady;  // 0x11b7cc
  std::int32_t fog_lift_ = 0;     // 0x11b7d4
  std::int32_t fog_return_ = 0;   // 0x11b7d0
};

}  // namespace sh
