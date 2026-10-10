// The weather of a career patrol: rolled from the patrol zone's monthly
// tables at the patrol's start (`wp_init_weather`, 0x97bfc) and changed
// every few days by one step at a time (`wp_update_weather`, 0x97d25, run
// by `wp_patrol_tick` each frame). The day's fog roll feeds the sky's fog
// flag at the next midnight (`Sky::set_daily_fog`).
#pragma once

#include <array>
#include <cstdint>
#include <functional>

#include "core/random.h"

namespace sh {

// The weather the career saves (`0x116c41..0x116c51`, restored on load).
// The patrol zone's weather odds for the current calendar month
// (`PatrolZone::fog_pct` and `weather_pct`, at 0x11b8aa / 0x11b8da).
struct MonthWeather {
  std::int32_t fog_pct = 0;
  // % of clear, partly cloudy, mostly cloudy, overcast.
  std::array<std::int32_t, 4> weather_pct{};
};

struct PatrolWeatherState {
  std::int32_t days_left = 0;          // 0x187218: days to the next change
  std::int32_t weather = 0;            // 0x125f60: 0 clear .. 3 overcast
  std::int32_t sea_state = 0;          // 0x125f64: 0 calm .. 2 heavy
  std::int32_t fog = 0;                // 0x125f68 (the saved copy) / 0x129c68 (restored)
  std::int32_t thermal_layer_ft = 0;   // 0x11b7c8: 100..799, >= 300 in a heavy sea
};

class PatrolWeather {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Rolls the patrol's weather with the month's `odds` on `day` of the
  // month, or restores `saved` (a career loaded mid-patrol). Draws the
  // day's fog roll first, then (unless restored) the weather, sea state,
  // fog flag and thermal layer.
  void init(const MonthWeather& odds, std::int32_t day, WatcomRandom& random, const PatrolWeatherState* saved);

  // Runs the frame's update for the clock's `day` with the month's `odds`:
  // on the first
  // frame of a new day spent away from the outside stations
  // (`shows_world` false), rolls the day's fog and counts the days down; at
  // zero the weather and the sea state move one step toward a new monthly
  // pick (the weather first, calling `reload_environment` when it moved:
  // the original picks a new `.ENV` there, with its draws), a new thermal
  // layer and 1..3 days to the next change. `fog_pct` is the fog density
  // now, kept for the career save. Returns true if the weather moved.
  bool update(const MonthWeather& odds, std::int32_t day, bool shows_world, std::int32_t fog_pct, WatcomRandom& random,
              const std::function<void()>& reload_environment);

  const PatrolWeatherState& state() const { return state_; }
  // The state as the career saves it after `init` or a change (its fog is
  // the flag rolled by `init`; a career saves the sky's flag of the day).
  const PatrolWeatherState& saved() const { return saved_; }
  // The day's fog roll (`0x129c68`).
  std::int32_t daily_fog() const { return daily_fog_; }
  // The fog density at the last update (`0x116c7a`, saved in the career).
  std::int32_t saved_fog_pct() const { return saved_fog_pct_; }

 private:
  PatrolWeatherState state_;
  PatrolWeatherState saved_;
  std::int32_t daily_fog_ = 0;
  std::int32_t last_day_ = 0;  // 0x187214
  std::int32_t saved_fog_pct_ = 0;
};

}  // namespace sh
