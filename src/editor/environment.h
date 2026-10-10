// The ENVIRONMENT settings without their dialog: the working copy the
// dialog edits (0xa66d8..0xa66f8) with its cycle callbacks and their date
// and time limits, the sun / moon / light computation of the preview
// (`environment_update_lighting`, SE 0x28283) and the clamping of dates to
// the war that the class sheets and PATROL AREA share.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "assets/scn_file.h"
#include "editor/data.h"
#include "editor/scenario.h"
#include "editor/se_strings.h"
#include "sim/astronomy.h"

namespace sh::editor {

// SE addresses of the environment's tables.
namespace se_addr {
// Per weather {i32 no sun and moon, u8 ambient minimum %, u8 red, green,
// blue tint} (the `.ENV` daylight fields of SH's world palette).
inline constexpr std::uint32_t kWeatherLight = 0x9bc94;
}  // namespace se_addr

// The working copy of ENVIRONMENT (`cmd_environment`, SE 0x27f25). The cycle
// methods are the controls' callbacks (`advance` false only reads): each
// returns the index its cycle button shows.
struct EnvironmentForm {
  std::int32_t weather = 0;   // 0xa66d8
  std::int32_t sea = 0;       // 0xa66dc
  std::int32_t fog = 0;       // 0xa66e0
  std::int32_t gradient = 0;  // 0xa66e4
  std::int32_t month = 0;     // 0xa66e8, 0-based
  std::int32_t day = 1;       // 0xa66ec
  std::int32_t year = 0;      // 0xa66f0, since 1930
  std::int32_t hour = 0;      // 0xa66f4
  std::int32_t minute = 0;    // 0xa66f8

  // What the callbacks ask to redraw: the weather picture (0xa66fc), the
  // light preview and rise / set times (0xa6700), the two day digits (their
  // dirty flags 0xa70fc / 0xa7130, set when a month or year change clamps
  // the day).
  bool weather_dirty = false;
  bool lighting_dirty = false;
  bool day_dirty = false;

  // The header's values (the dialog's opening copy).
  static EnvironmentForm from_header(const ScnHeader& header);
  // Writes the values back (OK).
  void commit(ScnHeader& header) const;
  ScenarioDate date() const { return {day, month, year}; }

  // `env_month_cycle`: the next month with the date inside the war, then
  // the day clamped to the month's length.
  std::int32_t cycle_month(bool advance);
  // `env_year_cycle`: 1941..1945, the next year with the date inside the
  // war, the day clamped; returns year - 1941.
  std::int32_t cycle_year(bool advance);
  // `env_day_tens_cycle` / `env_day_units_cycle`: the digit cycles until
  // the day is not 0, fits the month and the date is inside the war.
  std::int32_t cycle_day_tens(bool advance);
  std::int32_t cycle_day_units(bool advance);
  // `env_hour_tens_cycle` (0..2) / `env_hour_units_cycle` (0..3 after 2):
  // the digit cycles until the hour is at most 23.
  std::int32_t cycle_hour_tens(bool advance);
  std::int32_t cycle_hour_units(bool advance);
  // `env_minute_tens_cycle` (0..5) / `env_minute_units_cycle` (0..9).
  std::int32_t cycle_minute_tens(bool advance);
  std::int32_t cycle_minute_units(bool advance);
  // Weather 0..3 (dirties the picture), sea 0..2, fog 0..1 (dirties the
  // light), gradient 0..4.
  std::int32_t cycle_weather(bool advance);
  std::int32_t cycle_sea(bool advance);
  std::int32_t cycle_fog(bool advance);
  std::int32_t cycle_gradient(bool advance);
};

// The light of SE's table `se_addr::kWeatherLight` for one weather.
struct WeatherLight {
  bool no_sky_bodies = false;
  std::int32_t ambient_pct = 0;
  std::array<std::int32_t, 3> tint{};

  static WeatherLight read(const SeExe& se, std::int32_t weather);
};

// What `environment_update_lighting` computes for a date, time and weather
// at the player's position.
struct EnvironmentSky {
  RiseSetTimes sun;   // 0xa6708..0xa6714
  RiseSetTimes moon;  // 0xa6718..0xa6724
  std::int32_t moon_phase = 4;  // 0xa6704: 0 full .. 4 new
  // The per-channel darkening of the preview in percent and the ambient
  // light (0xf6224), as `WorldLighting::update_light` gets them from a
  // `.ENV` in the game.
  std::array<std::int32_t, 3> darkening{};
  std::int32_t light_pct = 100;
};

// Computes the sky of `form` (its date, time and weather) seen from the
// world position (`x`, `y`) of `world` (which must have geographic bounds):
// the ephemeris positions and rising / setting times, the moon phase and
// the light. The elevations are used as the ephemeris gives them (the
// original round-trips them through a rotation matrix, as SH's
// `world_set_sun`).
EnvironmentSky compute_environment_sky(const EnvironmentForm& form, const EditorData& data, const World& world,
                                       std::int32_t x, std::int32_t y);

// `environment_refresh` (SE 0x2886a): the scenario's light
// (`Scenario::sighting_light_pct`) recomputed from its header and the
// player's position, after a load, New and ENVIRONMENT's OK.
void refresh_environment(Scenario& scenario);

// The four rise and set texts of the preview (`env_sunrise_text` ..
// `env_moonset_text`): "Sunrise - hh:mm" with SE's labels; times outside
// the day print as 00, as `int_to_padded_str` does.
enum class RiseSet { kSunrise, kSunset, kMoonrise, kMoonset };
std::string rise_set_text(const SeExe& se, RiseSet which, const EnvironmentSky& sky);

// A month and year clamped into the war (7 Dec 1941 .. 15 Aug 1945) as the
// class sheets and PATROL AREA print "m/yy thru m/yy": `month` 1-based and
// `year` since 1930 in and out. The original compares with the day of the
// ENVIRONMENT working copy, which cannot change the month it ends on.
struct MonthYear {
  std::int32_t month = 1;
  std::int32_t year = 0;
};
MonthYear clamp_to_war(std::int32_t month, std::int32_t year);
// Returns "m/yy" of a clamped month, `slash` being the SE string of the
// call site ("/").
std::string format_month_year(const MonthYear& date, std::string_view slash);

}  // namespace sh::editor
