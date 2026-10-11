#include "sim/sky.h"

#include <cmath>
#include <utility>

#include "core/fixed_math.h"

namespace sh {

namespace {

// `angle_add` (0x5447c) on raw tenths.
std::int32_t angle_add(std::int32_t a, std::int32_t b) { return fx::angle_add(Angle10(a), Angle10(b)).tenths(); }

LocalTime local_time(const GameClock& clock) {
  return {clock.year() + kCalendarBaseYear, clock.month() + 1, clock.day(),
          clock.hour(),                     clock.minute(),    clock.second()};
}

}  // namespace

Sky::Sky(const Ephemeris& ephemeris, const World& world, WatcomRandom& random, std::uint32_t options,
         std::int32_t fog_day, std::int32_t fog_density)
    : astronomy_(ephemeris),
      world_(world),
      random_(&random),
      options_(options),
      fog_day_(fog_day),
      fog_density_(fog_density) {}

Observer Sky::observer_at(std::int32_t x, std::int32_t z) const {
  // world_pos_to_latlon (0x95c12) gives whole degrees; the time zone comes
  // from the signed longitude.
  const GeoAngle lon = world_.longitude_at(x);
  const GeoAngle lat = world_.latitude_at(z);
  Observer o;
  o.longitude = lon.hemisphere == 'W' ? 360 - lon.whole_degrees : lon.whole_degrees;
  o.latitude = lat.hemisphere == 'S' ? -lat.whole_degrees : lat.whole_degrees;
  const std::int32_t signed_lon = lon.degrees.raw();
  const std::int32_t lon_degrees = fx::trunc(signed_lon);
  o.time_zone = (lon_degrees + 7) / 15;
  return o;
}

void Sky::changed() {
  palette_dirty_ = true;
  notify();
}

void Sky::notify() {
  if (listener_ != nullptr) {
    listener_->on_sky_changed();
  }
}

void Sky::update_body(const GameClock& clock, bool sun) {
  // sky_update_sun_or_moon (0x7e21c)
  const Observer o = observer_at(observer_x_, observer_z_);
  const LocalTime t = local_time(clock);
  const HorizonPosition p = sun ? astronomy_.sun_position(t, o) : astronomy_.moon_position(t, o);
  std::int32_t elevation = p.elevation;
  if (elevation < 0) {
    elevation = angle_add(elevation, 0xe10);
  }
  std::int32_t azimuth = p.azimuth;
  if (azimuth < 0) {
    azimuth = angle_add(azimuth, 0xe10);
  }
  // Meeus' azimuth counts from the south.
  azimuth = angle_add(azimuth, 0x708);
  if (sun) {
    state_.sun_azimuth = Angle10(azimuth);
    state_.sun_elevation = Angle10(elevation);
    ship_lights_ = elevation >= 0x708;
  } else {
    state_.moon_azimuth = Angle10(azimuth);
    state_.moon_elevation = Angle10(elevation);
  }
  changed();
}

void Sky::update_day(const GameClock& clock) {
  // clock_update_sky (0x7e08e)
  update_body(clock, true);
  sun_times_ = astronomy_.sun_rise_set();
  update_body(clock, false);
  const MoonRiseSet moon = astronomy_.moon_rise_set();
  moon_times_ = moon.times;
  const auto lit = static_cast<std::int32_t>(std::trunc(moon.illuminated * 65536.0 + 0.5));
  state_.moon_phase = 4 - (lit + 0x2000) / 0x4000;
  update_body(clock, false);

  if ((options_ & 1u) != 0) {
    fog_day_ = daily_fog_;
  }
  fog_state_ = FogState::kSteady;
  if (fog_day_ != 1) {
    return;
  }
  // The fog lifts around midday and returns before sunset.
  std::int32_t day_length = sun_times_.set_hour - sun_times_.rise_hour;
  if (day_length < 0) {
    day_length += 24;
  }
  const std::int32_t lift_early = random_->random_int(day_length / 8);
  std::int32_t lift_hour = day_length / 2 - lift_early + sun_times_.rise_hour;
  while (lift_hour > 23) {
    lift_hour -= 24;
  }
  fog_lift_ = lift_hour << 16 | random_->random_int(60);
  const std::int32_t night_length = sun_times_.rise_hour - sun_times_.set_hour;
  const std::int32_t return_early = random_->random_int(night_length / 4);
  // The original's loop adds 24 while the hour is above 23, which never
  // ends; the hour is left as is (it is below 24 unless the sun sets just
  // before midnight and rises later).
  const std::int32_t return_hour = night_length / 8 - return_early + sun_times_.set_hour;
  fog_return_ = return_hour << 16 | random_->random_int(60);
  fog_pct_ = 0;
  fog_state_ = FogState::kClearing;
  if (clock.hour() < fog_lift_ >> 16 || fog_return_ >> 16 < clock.hour()) {
    fog_state_ = FogState::kFoggy;
    fog_density_ = random_->random_int(30) + 60;
  }
  notify();
}

void Sky::clock_init(const GameClock& clock) {
  // clock_init (0x7dae4)
  update_day(clock);
  if ((options_ & 1u) == 0 && fog_day_ == 1) {
    fog_state_ = FogState::kSteady;
    fog_pct_ = fog_density_;
    notify();
  }
}

StartTime Sky::choose_start_time(StartTimeSetting setting, std::int32_t year, std::int32_t month, std::int32_t day,
                                 std::int32_t x, std::int32_t z) {
  // clock_init_time_of_day (0x7df1d): the sun of the day's midnight.
  observer_x_ = x;
  observer_z_ = z;
  astronomy_.sun_position({year + kCalendarBaseYear, month + 1, day, 0, 0, 0}, observer_at(x, z));
  RiseSetTimes times = astronomy_.sun_rise_set();
  StartTime start;
  std::int32_t minute = 0;
  switch (setting) {
    case StartTimeSetting::kDawn:
      start.hour = times.rise_hour;
      minute = times.rise_minute - 35;
      break;
    case StartTimeSetting::kDusk:
      start.hour = times.set_hour;
      minute = times.set_minute - 20;
      break;
    case StartTimeSetting::kNight:
    case StartTimeSetting::kDay: {
      if (setting == StartTimeSetting::kNight) {
        // The night runs from sunset to sunrise.
        std::swap(times.rise_hour, times.set_hour);
      }
      std::int32_t length = times.set_hour - times.rise_hour;
      if (length < 0) {
        length += 24;
      }
      const std::int32_t offset = random_->random_int(length / 4);
      start.hour = times.rise_hour + length * 3 / 8 + offset;
      while (start.hour > 23) {
        start.hour -= 24;
      }
      start.minute = random_->random_int(60);
      return start;
    }
  }
  start.minute = minute;
  if (minute < 0) {
    --start.hour;
    start.minute += 60;
  }
  return start;
}

void Sky::on_minute_started(const GameClock& clock) {
  // clock_advance (0x7db61): the fog drifts by 0..2 % a minute.
  const std::int32_t hour = clock.hour();
  const std::int32_t minute = clock.minute();
  auto past = [&](std::int32_t when) {
    return (when >> 16) < hour || ((when >> 16) == hour && (when & 0xffff) < minute);
  };
  if (fog_state_ == FogState::kFoggy && past(fog_lift_)) {
    fog_pct_ -= random_->random_int(3);
    if (fog_pct_ < 1) {
      fog_pct_ = 0;
      fog_state_ = FogState::kClearing;
    }
    changed();
  }
  if (fog_state_ == FogState::kClearing && past(fog_return_)) {
    fog_pct_ += random_->random_int(3);
    if (fog_density_ <= fog_pct_) {
      fog_state_ = FogState::kFoggy;
      fog_pct_ = fog_density_;
    }
    changed();
  }
  if (fog_day_ == 0 && fog_pct_ > 0) {
    fog_pct_ -= random_->random_int(3);
    if (fog_pct_ < 1) {
      fog_pct_ = 0;
    }
    changed();
  }
  // Also while clearing (the original tests the state against 0): the fog
  // keeps coming back between the lift and return times. Only while foggy
  // with the rules' corrections (not in the original).
  const bool fixed = rules_ != nullptr && rules_->fix_obvious_errors;
  const bool drifts = fixed ? fog_state_ == FogState::kFoggy : fog_state_ != FogState::kSteady;
  if (drifts && fog_pct_ < fog_density_) {
    fog_pct_ += random_->random_int(3);
    if (fog_density_ < fog_pct_) {
      fog_pct_ = fog_density_;
    }
    changed();
  }
  if (next_ != nullptr) {
    next_->on_minute_started(clock);
  }
}

void Sky::on_new_day(const GameClock& clock) {
  update_day(clock);
  if (next_ != nullptr) {
    next_->on_new_day(clock);
  }
}

void Sky::on_minute_ended(const GameClock& clock) {
  if (clock.minute() % 2 == 0) {
    update_body(clock, true);
  }
  if ((clock.minute() + 1) % 2 == 0) {
    update_body(clock, false);
  }
  if (next_ != nullptr) {
    next_->on_minute_ended(clock);
  }
}

}  // namespace sh
