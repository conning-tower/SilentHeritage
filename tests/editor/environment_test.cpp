// The ENVIRONMENT settings: the date limits of the cycles (7 Dec 1941 ..
// 15 Aug 1945, the day clamped to the month), the time digits, the sky of
// the preview, and the dates of the class sheets clamped to the war.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>

#include "editor/environment.h"
#include "editor_ui_fixture.h"
#include "game_data.h"

namespace sh::editor {
namespace {

EnvironmentForm form_on(std::int32_t day, std::int32_t month, std::int32_t year) {
  EnvironmentForm f;
  f.day = day;
  f.month = month;
  f.year = year;
  return f;
}

}  // namespace

TEST_CASE("The month and year cycles keep the date in the war and clamp the day", "[editor][environment]") {
  SECTION("31 January 1942 becomes 28 February") {
    EnvironmentForm f = form_on(31, 0, 12);
    CHECK(f.cycle_month(true) == 1);
    CHECK(f.day == 28);
    CHECK(f.day_dirty);
    CHECK(f.lighting_dirty);
  }
  SECTION("December 1941 has no other month in the war") {
    EnvironmentForm f = form_on(7, 11, 11);
    CHECK(f.cycle_month(true) == 11);
    CHECK(f.year == 11);
  }
  SECTION("August 1945 is skipped after the 15th") {
    EnvironmentForm f = form_on(20, 6, 15);
    CHECK(f.cycle_month(true) == 0);
    CHECK(f.year == 15);
  }
  SECTION("20 August 1944 goes round to 1942") {
    EnvironmentForm f = form_on(20, 7, 14);
    CHECK(f.cycle_year(true) == 1);
    CHECK(f.year == 12);
  }
  SECTION("29 February 1944 is clamped in 1945") {
    EnvironmentForm f = form_on(29, 1, 14);
    CHECK(f.cycle_year(true) == 4);
    CHECK(f.day == 28);
    CHECK(f.day_dirty);
  }
  SECTION("Reading changes nothing") {
    EnvironmentForm f = form_on(31, 0, 12);
    CHECK(f.cycle_month(false) == 0);
    CHECK(f.cycle_year(false) == 1);
    CHECK_FALSE(f.lighting_dirty);
  }
}

TEST_CASE("The day digits skip day 0, days past the month and dates outside the war", "[editor][environment]") {
  SECTION("9 December 1941 goes to the 7th") {
    EnvironmentForm f = form_on(9, 11, 11);
    CHECK(f.cycle_day_units(true) == 7);
    CHECK(f.day == 7);
  }
  SECTION("15 August 1945 goes to the 10th") {
    EnvironmentForm f = form_on(15, 7, 15);
    CHECK(f.cycle_day_units(true) == 0);
    CHECK(f.day == 10);
  }
  SECTION("29 February 1944: the tens go to 09, the units to 20") {
    EnvironmentForm tens = form_on(29, 1, 14);
    CHECK(tens.cycle_day_tens(true) == 0);
    CHECK(tens.day == 9);
    EnvironmentForm units = form_on(29, 1, 14);
    CHECK(units.cycle_day_units(true) == 0);
    CHECK(units.day == 20);
  }
  SECTION("30 April: the tens skip 0 (day 00)") {
    EnvironmentForm f = form_on(30, 3, 13);
    CHECK(f.cycle_day_tens(true) == 1);
    CHECK(f.day == 10);
  }
}

TEST_CASE("The time digits keep the hour at most 23", "[editor][environment]") {
  EnvironmentForm f;
  f.hour = 19;
  CHECK(f.cycle_hour_tens(true) == 0);  // 29 is skipped
  CHECK(f.hour == 9);
  CHECK(f.cycle_hour_tens(true) == 1);
  CHECK(f.cycle_hour_tens(true) == 0);
  CHECK(f.hour == 9);
  f.hour = 13;
  CHECK(f.cycle_hour_tens(true) == 2);
  CHECK(f.hour == 23);
  CHECK(f.cycle_hour_units(true) == 0);  // units 0..3 after 2
  CHECK(f.hour == 20);
  f.hour = 9;
  CHECK(f.cycle_hour_units(true) == 0);
  CHECK(f.hour == 0);
  f.minute = 59;
  CHECK(f.cycle_minute_tens(true) == 0);
  CHECK(f.minute == 9);
  CHECK(f.cycle_minute_units(true) == 0);
  CHECK(f.minute == 0);
}

TEST_CASE("The other environment cycles wrap and flag their redraws", "[editor][environment]") {
  EnvironmentForm f;
  f.weather = 3;
  CHECK(f.cycle_weather(true) == 0);
  CHECK(f.weather_dirty);
  CHECK_FALSE(f.lighting_dirty);
  f.sea = 2;
  CHECK(f.cycle_sea(true) == 0);
  CHECK(f.cycle_fog(true) == 1);
  CHECK(f.lighting_dirty);
  f.gradient = 4;
  CHECK(f.cycle_gradient(true) == 0);
}

TEST_CASE("Class sheet dates are clamped to the war", "[editor][environment]") {
  CHECK(format_month_year(clamp_to_war(1, 1940 - kScenarioYearBase), "/") == "12/41");
  CHECK(format_month_year(clamp_to_war(3, 1943 - kScenarioYearBase), "/") == "3/43");
  CHECK(format_month_year(clamp_to_war(12, 1945 - kScenarioYearBase), "/") == "8/45");
  CHECK(format_month_year(clamp_to_war(8, 1945 - kScenarioYearBase), "/") == "8/45");
}

TEST_CASE("The environment sky follows the time of day", "[editor][environment][content]") {
  SH_REQUIRE_GAME_DATA(install);
  test::EditorFixture fx(install);
  const Scenario& s = fx.scenario;
  const WorldPoint p = s.player().position(s.object_env());
  EnvironmentForm f = EnvironmentForm::from_header(s.header);

  // Over a day the light goes from the night's to full daylight; the
  // rising and setting times stay those of the day.
  std::int32_t darkest = 100;
  std::int32_t brightest = 0;
  std::int32_t darkest_overcast = 100;
  const EnvironmentSky noon = compute_environment_sky(f, s.data(), s.world().world(), p.x, p.y);
  for (std::int32_t hour = 0; hour < 24; ++hour) {
    f.hour = hour;
    f.weather = 0;
    const EnvironmentSky sky = compute_environment_sky(f, s.data(), s.world().world(), p.x, p.y);
    darkest = std::min(darkest, sky.light_pct);
    brightest = std::max(brightest, sky.light_pct);
    CHECK(sky.sun.rise_hour == noon.sun.rise_hour);
    f.weather = 3;
    darkest_overcast =
        std::min(darkest_overcast, compute_environment_sky(f, s.data(), s.world().world(), p.x, p.y).light_pct);
  }
  CHECK(brightest == 100);
  CHECK(darkest < 50);
  // Overcast (no moonlight, an ambient floor of 24 %) is never darker.
  CHECK(darkest_overcast == 24);
  CHECK(noon.moon_phase >= 0);
  CHECK(noon.moon_phase <= 4);
  CHECK(rise_set_text(s.data().se, RiseSet::kSunrise, noon).starts_with("Sunrise - "));
}

TEST_CASE("The environment refresh sets the scenario's light", "[editor][environment]") {
  SH_REQUIRE_INSTALL(install);
  test::EditorFixture fx(install);
  const WorldPoint p = fx.scenario.player().position(fx.scenario.object_env());
  for (std::int32_t hour : {0, 6, 12, 18}) {
    fx.scenario.header.hour = hour;
    refresh_environment(fx.scenario);
    const EnvironmentForm f = EnvironmentForm::from_header(fx.scenario.header);
    CHECK(fx.scenario.sighting_light_pct ==
          compute_environment_sky(f, fx.data, fx.scenario.world().world(), p.x, p.y).light_pct);
  }
}

}  // namespace sh::editor
