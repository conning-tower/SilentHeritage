#include "sim/weather.h"

#include <array>
#include <cstddef>
#include <span>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"

namespace sh {

namespace {

// The sea-state weights in % (calm, moderate, heavy), 3 per weather (SH.EXE
// 0x1108bc).
const std::array<std::int32_t, 12>& sea_state_pct() {
  static const std::array<std::int32_t, 12> table = [] {
    constexpr std::uint32_t kAddress = 0x1108bc;
    const auto t = exe_i32_array<12>(game_exe(), kAddress);
    for (std::size_t w = 0; w < 4; ++w) {
      check_exe_table(t[3 * w] + t[3 * w + 1] + t[3 * w + 2] == 100, "sea state", kAddress);
    }
    return t;
  }();
  return table;
}

// Returns the index whose cumulative percentage first exceeds a
// `random_int(100)` draw, the last one if none does
// (`random_weighted_index_i32`, 0x98240).
std::int32_t weighted_index(std::span<const std::int32_t> pct, WatcomRandom& random) {
  const std::int32_t r = random.random_int(100);
  std::int32_t sum = 0;
  for (std::size_t i = 0; i < pct.size(); ++i) {
    sum += pct[i];
    if (r < sum) {
      return static_cast<std::int32_t>(i);
    }
  }
  return static_cast<std::int32_t>(pct.size()) - 1;
}

// Returns the sign of `v` (-1, 0, 1).
std::int32_t step_toward(std::int32_t v) { return v == 0 ? 0 : v > 0 ? 1 : -1; }

std::int32_t roll_thermal_layer(std::int32_t sea_state, WatcomRandom& random) {
  std::int32_t layer = random.random_int(700) + 100;
  if (sea_state == 2 && layer < 300) {
    layer = 300;
  }
  return layer;
}

std::int32_t sea_state_pick(std::int32_t weather, WatcomRandom& random) {
  return weighted_index(std::span(sea_state_pct()).subspan(3 * static_cast<std::size_t>(weather), 3), random);
}

}  // namespace

void PatrolWeather::init(const MonthWeather& odds, std::int32_t day, WatcomRandom& random,
                         const PatrolWeatherState* saved) {
  // wp_init_weather (0x97bfc)
  last_day_ = day;
  daily_fog_ = random.random_int(100) <= odds.fog_pct ? 1 : 0;
  if (saved != nullptr) {
    state_.weather = saved->weather;
    state_.sea_state = saved->sea_state;
    daily_fog_ = saved->fog;
    state_.days_left = saved->days_left;
    state_.thermal_layer_ft = saved->thermal_layer_ft;
    saved_ = *saved;
    return;
  }
  state_.days_left = 0;
  state_.weather = weighted_index(odds.weather_pct, random);
  state_.sea_state = sea_state_pick(state_.weather, random);
  state_.fog = random.random_int(100) <= odds.fog_pct ? 1 : 0;
  state_.thermal_layer_ft = roll_thermal_layer(state_.sea_state, random);
  saved_ = state_;
}

bool PatrolWeather::update(const MonthWeather& odds, std::int32_t day, bool shows_world, std::int32_t fog_pct,
                           WatcomRandom& random, const std::function<void()>& reload_environment) {
  // wp_update_weather (0x97d25): a day spent on an outside station is
  // skipped until the player leaves it.
  saved_fog_pct_ = fog_pct;
  if (last_day_ == day || shows_world) {
    return false;
  }
  daily_fog_ = random.random_int(100) <= odds.fog_pct ? 1 : 0;
  --state_.days_left;
  last_day_ = day;
  if (state_.days_left >= 1) {
    return false;
  }
  bool moved = false;
  const std::int32_t weather = weighted_index(odds.weather_pct, random);
  if (weather != state_.weather) {
    state_.weather += step_toward(weather - state_.weather);
    moved = true;
    reload_environment();
  }
  state_.sea_state += step_toward(sea_state_pick(state_.weather, random) - state_.sea_state);
  state_.thermal_layer_ft = roll_thermal_layer(state_.sea_state, random);
  state_.days_left = random.random_int(3) + 1;
  saved_ = state_;
  return moved;
}

}  // namespace sh
