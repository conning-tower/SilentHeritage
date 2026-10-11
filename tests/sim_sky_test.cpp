// The sky: the astronomy library against values of the original library
// (SH.EXE's routines run under an x86 emulator) and an independent solar
// computation, the moon phase on known dates, the light of the day, the fog
// schedule's draws, a career patrol's weather, and HIST01's sky after load.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "assets/dat_tables.h"
#include "core/random.h"
#include "game/mission.h"
#include "game_data.h"
#include "sim/astronomy.h"
#include "sim/sky.h"
#include "sim/weather.h"
#include "view/world_palette.h"

namespace sh {

namespace {

// Truk (7 N, 151 E) as `sky_update_sun_or_moon` passes it.
constexpr Observer kTruk{151, 7, 10, 0};

std::unique_ptr<Mission> load_hist01(const Install& install, const GameTables& tables) {
  MissionSetup setup;
  setup.random = WatcomRandom(12345);
  return Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
}

// Advances `clock` by one game minute at 1x, telling `observer`.
void advance_minute(GameClock& clock, ClockObserver& observer) {
  const std::int32_t start = clock.minutes_elapsed();
  while (clock.minutes_elapsed() == start) {
    clock.advance(15, false, &observer);
  }
}

// Returns a clock at the date (full year, 1-based month) and time.
GameClock clock_at(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour, std::int32_t minute) {
  GameClock clock;
  clock.init(year - kCalendarBaseYear, month - 1, day, hour, minute);
  return clock;
}

}  // namespace

TEST_CASE("The Julian day and Delta T follow Meeus", "[sky][content]") {
  SH_REQUIRE_GAME_EXE();
  // Meeus, examples 7.a and 7.b, and the first Gregorian day.
  CHECK(julian_day(1957, 10, 4.81) == Catch::Approx(2436116.31).margin(1e-6));
  CHECK(julian_day(333, 1, 27.5) == 1842713.0);
  CHECK(julian_day(1582, 10, 15.0) == 2299160.5);
  CHECK(julian_day(1582, 10, 4.0) == 2299159.5);
  // The table: 26.2 s in 1944, 27.3 s in 1946; Meeus' formula after 2000.
  CHECK(delta_t_seconds(-0.56) == Catch::Approx(26.2).margin(1e-9));
  CHECK(delta_t_seconds(-0.55) == Catch::Approx(26.75).margin(1e-9));
  CHECK(delta_t_seconds(0.1) == Catch::Approx((0.1 * 32.5 + 123.5) * 0.1 + 102.3));
}

TEST_CASE("The sun's place and day match the original library", "[sky][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Ephemeris ephemeris = Ephemeris::load(install);
  Astronomy astronomy(ephemeris);

  // 16 February 1944, 16:00 at Truk. The original gives azimuth 71.1 deg
  // west of south, elevation 31.8 (both + 0.4 truncated), sunrise 6:21 and
  // sunset 18:15; an independent low-precision computation for the same
  // local mean time gives 250.75 / 31.57 deg and 6:22 / 18:16.
  const HorizonPosition afternoon = astronomy.sun_position({1944, 2, 16, 16, 0, 0}, kTruk);
  CHECK(afternoon.azimuth == 711);
  CHECK(afternoon.elevation == 318);
  const RiseSetTimes day = astronomy.sun_rise_set();
  CHECK(day.rise_hour == 6);
  CHECK(day.rise_minute == 21);
  CHECK(day.set_hour == 18);
  CHECK(day.set_minute == 15);

  const HorizonPosition noon = astronomy.sun_position({1944, 2, 16, 12, 0, 0}, kTruk);
  CHECK(noon.azimuth == -132);
  CHECK(noon.elevation == 704);

  // At 80 N: the midnight sun in June, the polar night in December.
  astronomy.sun_position({1944, 6, 21, 12, 0, 0}, {151, 80, 10, 0});
  const RiseSetTimes summer = astronomy.sun_rise_set();
  CHECK(summer.rise_hour == RiseSetTimes::kNeverSets);
  CHECK(summer.set_minute == RiseSetTimes::kNeverSets);
  const HorizonPosition winter = astronomy.sun_position({1944, 12, 21, 12, 0, 0}, {151, 80, 10, 0});
  CHECK(winter.elevation == -138);
  CHECK(astronomy.sun_rise_set().set_hour == RiseSetTimes::kNeverRises);
}

TEST_CASE("The moon's place, phase and day match the original library", "[sky][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Ephemeris ephemeris = Ephemeris::load(install);
  Astronomy astronomy(ephemeris);

  // Full moon on 9 February 1944, new moon on 24 February.
  const HorizonPosition full = astronomy.moon_position({1944, 2, 9, 12, 0, 0}, kTruk);
  CHECK(full.azimuth == 1500);
  CHECK(full.elevation == -663);
  const MoonRiseSet full_day = astronomy.moon_rise_set();
  CHECK(full_day.illuminated == Catch::Approx(0.995577134554).margin(1e-9));
  CHECK(full_day.times.rise_hour == 18);
  CHECK(full_day.times.rise_minute == 56);
  CHECK(full_day.times.set_hour == 6);
  CHECK(full_day.times.set_minute == 48);

  const HorizonPosition fresh = astronomy.moon_position({1944, 2, 24, 12, 0, 0}, kTruk);
  CHECK(fresh.azimuth == -483);
  CHECK(fresh.elevation == 673);
  const MoonRiseSet new_day = astronomy.moon_rise_set();
  CHECK(new_day.illuminated == Catch::Approx(0.011602451866).margin(1e-9));
  CHECK(new_day.times.rise_hour == 7);
  CHECK(new_day.times.rise_minute == 1);
  CHECK(new_day.times.set_hour == 19);
  CHECK(new_day.times.set_minute == 20);
}

TEST_CASE("The daily update turns the moon into the clock's phase", "[sky]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = load_hist01(install, tables);
  const Ephemeris ephemeris = Ephemeris::load(install);
  WatcomRandom random(1);
  Sky sky(ephemeris, mission->world(), random, 0, 0, 70);
  const fx::Vec3 p = mission->player().position();
  sky.set_observer(p.x, p.z);

  sky.update_day(clock_at(1944, 2, 9, 0, 0));
  CHECK(sky.state().moon_phase == 0);  // full
  sky.update_day(clock_at(1944, 2, 24, 0, 0));
  CHECK(sky.state().moon_phase == 4);  // new: no moon drawn
  sky.update_day(clock_at(1944, 2, 16, 0, 0));
  CHECK(sky.state().moon_phase == 2);  // last quarter
  // No fog day: no draws.
  CHECK(random.rand() == WatcomRandom(1).rand());
}

TEST_CASE("The light follows the sun and the moon through the day", "[sky][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = load_hist01(install, tables);
  const Ephemeris ephemeris = Ephemeris::load(install);
  WatcomRandom random(1);
  Sky sky(ephemeris, mission->world(), random, 0, 0, 70);
  const fx::Vec3 p = mission->player().position();
  sky.set_observer(p.x, p.z);

  auto light_at = [&](std::int32_t hour, std::int32_t minute) {
    const GameClock clock = clock_at(1944, 2, 16, hour, minute);
    sky.update_body(clock, true);
    sky.update_body(clock, false);
    WorldLighting lighting;
    const SkyState& s = sky.state();
    lighting.set_sky({s.sun_azimuth, s.sun_elevation, s.moon_azimuth, s.moon_elevation, s.moon_phase});
    lighting.update_light(mission->environment());
    return lighting.light_percent();
  };
  CHECK(light_at(12, 0) == 100);
  CHECK(!sky.ship_lights());
  // Sunset is at 17:55 there: half an hour later the sun is 8 degrees
  // down; at 23:00 the moon has not risen yet and only the .ENV's night
  // minimum is left.
  CHECK(light_at(18, 30) == 14);
  CHECK(sky.ship_lights());
  CHECK(light_at(23, 0) == 8);
}

TEST_CASE("A fog day draws its schedule and drifts by the minute", "[sky]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = load_hist01(install, tables);
  const Ephemeris ephemeris = Ephemeris::load(install);
  WatcomRandom random(77);
  Sky sky(ephemeris, mission->world(), random, 0, 1, 70);
  const fx::Vec3 p = mission->player().position();
  sky.set_observer(p.x, p.z);

  // clock_init at 16:00: the day's schedule, then the fog of the fog day
  // held at its density.
  GameClock clock = clock_at(1944, 2, 16, 16, 0);
  sky.clock_init(clock);
  const RiseSetTimes& sun = sky.sun_times();
  const std::int32_t day_length = sun.set_hour - sun.rise_hour;
  WatcomRandom r(77);
  const std::int32_t lift_hour = day_length / 2 - r.random_int(day_length / 8) + sun.rise_hour;
  CHECK(sky.fog_lift().hour == lift_hour);
  CHECK(sky.fog_lift().minute == r.random_int(60));
  const std::int32_t night = sun.rise_hour - sun.set_hour;
  const std::int32_t return_hour = night / 8 - r.random_int(night / 4) + sun.set_hour;
  CHECK(sky.fog_return().hour == return_hour);
  CHECK(sky.fog_return().minute == r.random_int(60));
  if (16 < lift_hour || return_hour < 16) {
    CHECK(sky.fog_density() == 60 + r.random_int(30));
  } else {
    CHECK(sky.fog_density() == 70);
  }
  CHECK(sky.fog_state() == FogState::kSteady);
  CHECK(sky.fog_pct() == sky.fog_density());

  // No drift and no draws while steady.
  for (int i = 0; i < 30; ++i) {
    advance_minute(clock, sky);
  }
  CHECK(sky.fog_pct() == sky.fog_density());
  WatcomRandom copy = random;
  CHECK(copy.rand() == r.rand());

  // The next day's schedule starts from no fog, which comes back by 0..2 %
  // a minute (also before the return time: the original's test).
  sky.update_day(clock_at(1944, 2, 17, 13, 0));
  CHECK(sky.fog_pct() == 0);
  const std::int32_t density = sky.fog_density();
  GameClock afternoon = clock_at(1944, 2, 17, 13, 0);
  WatcomRandom expected = random;
  std::int32_t fog = 0;
  for (int minute = 0; minute < 10; ++minute) {
    advance_minute(afternoon, sky);
    if (sky.fog_state() == FogState::kClearing && fog < density) {
      fog = std::min(density, fog + expected.random_int(3));
    }
    CHECK(sky.fog_pct() == fog);
  }
  CHECK(fog > 0);
}

TEST_CASE("With the rules' corrections the fog stays lifted until its return time", "[sky][rules]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = load_hist01(install, tables);
  const Ephemeris ephemeris = Ephemeris::load(install);
  // The same fog day for both rule sets: the original's fog comes back
  // while clearing, the corrected one stays at 0 without a draw.
  for (const bool fixed : {false, true}) {
    WatcomRandom random(77);
    Sky sky(ephemeris, mission->world(), random, 0, 1, 70);
    const MissionRules rules = fixed ? MissionRules::extended() : MissionRules{};
    sky.set_rules(&rules);
    const fx::Vec3 p = mission->player().position();
    sky.set_observer(p.x, p.z);
    GameClock clock = clock_at(1944, 2, 16, 16, 0);
    sky.clock_init(clock);
    sky.update_day(clock_at(1944, 2, 17, 13, 0));
    REQUIRE(sky.fog_state() == FogState::kClearing);
    GameClock afternoon = clock_at(1944, 2, 17, 13, 0);
    const WatcomRandom before = random;
    for (int minute = 0; minute < 10; ++minute) {
      advance_minute(afternoon, sky);
    }
    if (fixed) {
      CHECK(sky.fog_pct() == 0);
      WatcomRandom a = random;
      WatcomRandom b = before;
      CHECK(a.rand() == b.rand());
    } else {
      CHECK(sky.fog_pct() > 0);
    }
  }
}

TEST_CASE("A generated mission starts at dawn, dusk or a random time", "[sky]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = load_hist01(install, tables);
  const Ephemeris ephemeris = Ephemeris::load(install);
  WatcomRandom random(5);
  Sky sky(ephemeris, mission->world(), random, 0, 0, 70);
  const fx::Vec3 p = mission->player().position();
  sky.set_observer(p.x, p.z);
  sky.update_day(clock_at(1944, 2, 16, 0, 0));
  const RiseSetTimes sun = sky.sun_times();

  const StartTime dawn = sky.choose_start_time(StartTimeSetting::kDawn, 14, 1, 16, p.x, p.z);
  CHECK(dawn.hour * 60 + dawn.minute == sun.rise_hour * 60 + sun.rise_minute - 35);
  const StartTime dusk = sky.choose_start_time(StartTimeSetting::kDusk, 14, 1, 16, p.x, p.z);
  CHECK(dusk.hour * 60 + dusk.minute == sun.set_hour * 60 + sun.set_minute - 20);

  WatcomRandom r(5);
  const StartTime day = sky.choose_start_time(StartTimeSetting::kDay, 14, 1, 16, p.x, p.z);
  const std::int32_t length = sun.set_hour - sun.rise_hour;
  CHECK(day.hour == sun.rise_hour + length * 3 / 8 + r.random_int(length / 4));
  CHECK(day.minute == r.random_int(60));
  const StartTime night = sky.choose_start_time(StartTimeSetting::kNight, 14, 1, 16, p.x, p.z);
  const std::int32_t dark = sun.rise_hour + 24 - sun.set_hour;
  CHECK(night.hour == (sun.set_hour + dark * 3 / 8 + r.random_int(dark / 4)) % 24);
  CHECK(night.minute == r.random_int(60));
}

TEST_CASE("A patrol's weather moves one step every few days", "[sky]") {
  MonthWeather odds{100, {0, 0, 0, 100}};  // always fog, overcast
  WatcomRandom random(9);
  PatrolWeather weather;
  weather.init(odds, 10, random, nullptr);
  WatcomRandom r(9);
  CHECK(weather.daily_fog() == (r.random_int(100) <= 100 ? 1 : 0));
  r.random_int(100);  // the weather pick: overcast
  CHECK(weather.state().weather == 3);
  const std::int32_t sea_roll = r.random_int(100);  // 0 / 40 / 60 %
  CHECK(weather.state().sea_state == (sea_roll < 40 ? 1 : 2));
  CHECK(weather.state().fog == 1);
  r.random_int(100);
  std::int32_t layer = r.random_int(700) + 100;
  if (weather.state().sea_state == 2 && layer < 300) {
    layer = 300;
  }
  CHECK(weather.state().thermal_layer_ft == layer);
  CHECK(weather.state().days_left == 0);

  // The same day, or a day spent on the bridge: nothing happens.
  int reloads = 0;
  const auto reload = [&] { ++reloads; };
  CHECK(!weather.update(odds, 10, false, 0, random, reload));
  CHECK(!weather.update(odds, 11, true, 0, random, reload));
  // A new day below: the fog roll, then a change toward the new pick (the
  // environment is reloaded), 1..3 days to the next one.
  odds.weather_pct = {100, 0, 0, 0};
  CHECK(weather.update(odds, 11, false, 42, random, reload));
  CHECK(weather.state().weather == 2);  // one step toward clear
  CHECK(reloads == 1);
  CHECK(weather.saved_fog_pct() == 42);
  CHECK(weather.state().days_left >= 1);
  CHECK(weather.state().days_left <= 3);
}

TEST_CASE("HIST01's sky after load", "[sky][mission][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = load_hist01(install, tables);
  // 16 February 1944, 16:00 near Truk (146 E, 8 N for the library): the
  // afternoon sun in the west-south-west, the moon (half lit) below the
  // horizon until 23:59, no fog. The original library gives the same.
  const Sky& sky = mission->sky();
  CHECK(sky.state().sun_azimuth.tenths() == 2521);
  CHECK(sky.state().sun_elevation.tenths() == 268);
  CHECK(sky.state().moon_azimuth.tenths() == 2536);
  CHECK(sky.state().moon_elevation.tenths() == 2930);  // -67 deg
  CHECK(sky.state().moon_phase == 2);
  CHECK(!sky.ship_lights());
  CHECK(sky.sun_times().rise_hour == 6);
  CHECK(sky.sun_times().rise_minute == 2);
  CHECK(sky.sun_times().set_hour == 17);
  CHECK(sky.sun_times().set_minute == 55);
  CHECK(sky.moon_times().rise_hour == 23);
  CHECK(sky.moon_times().rise_minute == 59);
  CHECK(sky.moon_times().set_hour == 11);
  CHECK(sky.moon_times().set_minute == 16);
  CHECK(mission->fog_pct() == 0);
  CHECK(mission->light_pct() == 100);
  CHECK(mission->sim().sensors().environment().light_pct == 100);
  CHECK(mission->sim().sensors().environment().fog_pct == 0);
}

TEST_CASE("HIST02 starts in its fog", "[sky][mission][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.random = WatcomRandom(4);
  const std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST02.SCN", setup);
  // A fog day: clock_init holds the fog at the day's density (60..89 %)
  // without drift until the next midnight's schedule.
  const Sky& sky = mission->sky();
  CHECK(mission->scenario().header.fog == 1);
  CHECK(sky.fog_state() == FogState::kSteady);
  CHECK(mission->fog_pct() == mission->fog_day_pct());
  CHECK(mission->fog_pct() >= 60);
  CHECK(mission->fog_pct() < 90);
  CHECK(mission->sim().sensors().environment().fog_pct == mission->fog_pct());
  CHECK(mission->sim().sensors().environment().light_pct == mission->light_pct());
}

}  // namespace sh
