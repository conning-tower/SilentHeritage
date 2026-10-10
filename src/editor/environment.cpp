#include "editor/environment.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "editor/objects.h"

namespace sh::editor {

namespace {

// The year cycle's range (the strings 0x9bef4: "1941" .. "1945").
constexpr std::int32_t kFirstYear = 1941 - kScenarioYearBase;
constexpr std::int32_t kLastYear = 1945 - kScenarioYearBase;

bool in_war(const EnvironmentForm& f) { return war_period(f.date()) == WarPeriod::kDuring; }

// Steps `value` by one modulo `count`.
std::int32_t next(std::int32_t value, std::int32_t count) { return value + 1 >= count ? 0 : value + 1; }

// Returns a signed elevation in tenths of a degree (-1800..1800) from the
// ephemeris' value made positive as the original does (+3600 when
// negative).
std::int32_t signed_elevation(std::int32_t elevation) {
  if (elevation < 0) {
    elevation += 3600;
  }
  return elevation > 1800 ? elevation - 3600 : elevation;
}

}  // namespace

// ------------------------------------------------------------ the form

EnvironmentForm EnvironmentForm::from_header(const ScnHeader& h) {
  EnvironmentForm f;
  f.weather = static_cast<std::int32_t>(h.weather);
  f.sea = static_cast<std::int32_t>(h.sea_state);
  f.fog = h.fog;
  f.gradient = h.gradient;
  f.month = h.month;
  f.day = h.day;
  f.year = h.year;
  f.hour = h.hour;
  f.minute = h.minute;
  return f;
}

void EnvironmentForm::commit(ScnHeader& h) const {
  h.weather = static_cast<Weather>(weather);
  h.sea_state = static_cast<SeaState>(sea);
  h.fog = fog;
  h.gradient = gradient;
  h.month = month;
  h.day = day;
  h.year = year;
  h.hour = hour;
  h.minute = minute;
}

std::int32_t EnvironmentForm::cycle_month(bool advance) {
  if (advance) {
    do {
      month = next(month, 12);
    } while (!in_war(*this));
    if (days_in_month(month, year) < day) {
      day_dirty = true;
      day = days_in_month(month, year);
    }
    lighting_dirty = true;
  }
  return month;
}

std::int32_t EnvironmentForm::cycle_year(bool advance) {
  if (advance) {
    do {
      ++year;
      if (year > kLastYear) {
        year = kFirstYear;
      }
    } while (!in_war(*this));
    if (days_in_month(month, year) < day) {
      day_dirty = true;
      day = days_in_month(month, year);
    }
    lighting_dirty = true;
  }
  return year - kFirstYear;
}

std::int32_t EnvironmentForm::cycle_day_tens(bool advance) {
  std::int32_t tens = day / 10;
  if (advance) {
    do {
      tens = next(day / 10, 10);
      day = tens * 10 + day % 10;
    } while (!in_war(*this) || days_in_month(month, year) < day || day == 0);
    lighting_dirty = true;
  }
  return tens;
}

std::int32_t EnvironmentForm::cycle_day_units(bool advance) {
  std::int32_t units = day % 10;
  if (advance) {
    do {
      units = next(day % 10, 10);
      day = day / 10 * 10 + units;
    } while (!in_war(*this) || days_in_month(month, year) < day || day == 0);
    lighting_dirty = true;
  }
  return units;
}

std::int32_t EnvironmentForm::cycle_hour_tens(bool advance) {
  std::int32_t tens = hour / 10;
  if (advance) {
    do {
      tens = next(tens, 3);
      hour = tens * 10 + hour % 10;
    } while (hour > 23);
    lighting_dirty = true;
  }
  return tens;
}

std::int32_t EnvironmentForm::cycle_hour_units(bool advance) {
  const std::int32_t tens = hour / 10;
  std::int32_t units = hour % 10;
  if (advance) {
    do {
      units = next(units, tens == 2 ? 4 : 10);
      hour = tens * 10 + units;
    } while (hour > 23);
    lighting_dirty = true;
  }
  return units;
}

std::int32_t EnvironmentForm::cycle_minute_tens(bool advance) {
  std::int32_t tens = minute / 10;
  if (advance) {
    tens = next(tens, 6);
    minute = tens * 10 + minute % 10;
    lighting_dirty = true;
  }
  return tens;
}

std::int32_t EnvironmentForm::cycle_minute_units(bool advance) {
  std::int32_t units = minute % 10;
  if (advance) {
    units = next(units, 10);
    minute = minute / 10 * 10 + units;
    lighting_dirty = true;
  }
  return units;
}

std::int32_t EnvironmentForm::cycle_weather(bool advance) {
  if (advance) {
    weather = next(weather, 4);
    weather_dirty = true;
  }
  return weather;
}

std::int32_t EnvironmentForm::cycle_sea(bool advance) {
  if (advance) {
    sea = next(sea, 3);
  }
  return sea;
}

std::int32_t EnvironmentForm::cycle_fog(bool advance) {
  if (advance) {
    fog = next(fog, 2);
    lighting_dirty = true;
  }
  return fog;
}

std::int32_t EnvironmentForm::cycle_gradient(bool advance) {
  if (advance) {
    gradient = next(gradient, 5);
  }
  return gradient;
}

// ------------------------------------------------------------ the sky

WeatherLight WeatherLight::read(const SeExe& se, std::int32_t weather) {
  const std::uint32_t at = se_addr::kWeatherLight + static_cast<std::uint32_t>(weather) * 8;
  const ExeImage& image = se.image();
  return WeatherLight{
      .no_sky_bodies = image.i32(at) != 0,
      .ambient_pct = image.u8(at + 4),
      .tint = {image.u8(at + 5), image.u8(at + 6), image.u8(at + 7)},
  };
}

EnvironmentSky compute_environment_sky(const EnvironmentForm& form, const EditorData& data, const World& world,
                                       std::int32_t x, std::int32_t y) {
  // environment_update_lighting (0x28283): the observer of
  // world_pos_to_latlon in whole degrees, its time zone from the signed
  // longitude (as SH's `sky_update_sun_or_moon`).
  const GeoAngle lon = world.longitude_at(x);
  const GeoAngle lat = world.latitude_at(y);
  Observer observer;
  observer.longitude = lon.hemisphere == 'W' ? 360 - lon.whole_degrees : lon.whole_degrees;
  observer.latitude = lat.hemisphere == 'S' ? -lat.whole_degrees : lat.whole_degrees;
  observer.time_zone = (lon.degrees.raw() / 0x10000 + 7) / 15;  // whole degrees, truncated
  const LocalTime time{.year = form.year + kScenarioYearBase,
                       .month = form.month + 1,
                       .day = form.day,
                       .hour = form.hour,
                       .minute = form.minute,
                       .second = 0};

  Astronomy astronomy(data.ephemeris);
  EnvironmentSky sky;
  const HorizonPosition sun = astronomy.sun_position(time, observer);
  sky.sun = astronomy.sun_rise_set();
  const HorizonPosition moon = astronomy.moon_position(time, observer);
  const MoonRiseSet moon_day = astronomy.moon_rise_set();
  sky.moon = moon_day.times;
  const auto lit = static_cast<std::int32_t>(std::trunc(moon_day.illuminated * 65536.0 + 0.5));
  sky.moon_phase = 4 - ((lit + 0x2000) >> 14);

  // The daylight formula of SH's `world_apply_daylight_palette` with the
  // weather table: the sums stay within 0..400, so the angle wraps of the
  // original never apply.
  const WeatherLight light = WeatherLight::read(data.se, form.weather);
  const std::int32_t sun_e = std::clamp(signed_elevation(sun.elevation) - 50, -200, 200);
  const std::int32_t moon_e = std::clamp(signed_elevation(moon.elevation) + 50, -100, 100);
  const std::int32_t tint_fade = std::abs((sun_e - 50) / 2) * 100 / 100;
  const std::int32_t dark = (400 - (sun_e + 200)) * 100 / 400;
  const std::int32_t moon_low = (200 - (moon_e + 100)) * 100 / 200;
  std::int32_t ambient = light.ambient_pct;
  if (!light.no_sky_bodies) {
    ambient += (100 - moon_low) * ((4 - sky.moon_phase) * 6) / 100;
  }
  for (std::size_t c = 0; c < 3; ++c) {
    const std::int32_t tint = light.tint[c] - light.tint[c] * tint_fade / 100;
    sky.darkening[c] = std::clamp(dark - tint, 0, 100 - ambient);
  }
  sky.light_pct = 100 - (sky.darkening[0] + sky.darkening[1] + sky.darkening[2]) / 3;
  return sky;
}

void refresh_environment(Scenario& scenario) {
  // environment_refresh: the working copy gets the date, time and weather
  // (not the fog, which only tints the preview).
  const EnvironmentForm form = EnvironmentForm::from_header(scenario.header);
  const ObjectEnv env = scenario.object_env();
  const WorldPoint p = scenario.player().position(env);
  scenario.sighting_light_pct =
      compute_environment_sky(form, scenario.data(), scenario.world().world(), p.x, p.y).light_pct;
}

std::string rise_set_text(const SeExe& se, RiseSet which, const EnvironmentSky& sky) {
  // {label, separator} of env_sunrise_text .. env_moonset_text.
  constexpr std::array<std::array<std::uint32_t, 2>, 4> kTexts{
      {{0x91165, 0x91188}, {0x911a2, 0x911c6}, {0x911e0, 0x91204}, {0x9121e, 0x91243}}};
  const RiseSetTimes& t = which == RiseSet::kSunrise || which == RiseSet::kSunset ? sky.sun : sky.moon;
  const bool rise = which == RiseSet::kSunrise || which == RiseSet::kMoonrise;
  const auto& texts = kTexts[static_cast<std::size_t>(which)];
  return std::string(se.str(texts[0])) + format_padded(rise ? t.rise_hour : t.set_hour, 2) +
         std::string(se.str(texts[1])) + format_padded(rise ? t.rise_minute : t.set_minute, 2);
}

MonthYear clamp_to_war(std::int32_t month, std::int32_t year) {
  if (year < kWarStart.year || (year == kWarStart.year && month - 1 < kWarStart.month)) {
    return {kWarStart.month + 1, kWarStart.year};
  }
  if (year > kWarEnd.year || (year == kWarEnd.year && month - 1 > kWarEnd.month)) {
    return {kWarEnd.month + 1, kWarEnd.year};
  }
  return {month, year};
}

std::string format_month_year(const MonthYear& date, std::string_view slash) {
  return std::to_string(date.month) + std::string(slash) + std::to_string(date.year + kScenarioYearBase - 1900);
}

}  // namespace sh::editor
