#include "sim/clock.h"

#include <stdexcept>

namespace sh {

std::int32_t days_in_month(std::int32_t year, std::int32_t month) {
  static constexpr std::int32_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 0 || month > 11) {
    throw std::out_of_range("days_in_month: month out of range");
  }
  const std::int32_t y = kCalendarBaseYear + year;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return month == 1 && leap ? 29 : kDays[month];
}

void GameClock::init(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour, std::int32_t minute,
                     std::int32_t second) {
  year_ = year;
  month_ = month;
  day_ = day;
  hour_ = hour;
  minute_ = minute;
  second_ = second;
  hundredths_ = 0;
  frame_ticks_ = 0;
  minutes_elapsed_ = 0;
  load_ticks_ = 0;
  force_increase_ = false;
  adjust_countdown_ = 1;
  cap_ = kUnrestrictedCompression;
  effective_ = 1;
  requested_ = 1;
  compute_weekday();
}

void GameClock::compute_weekday() {
  // clock_compute_weekday (0x7de91): Zeller's congruence, January and
  // February counted as months 13 and 14 of the previous year.
  std::int32_t m = month_ + 1;
  std::int32_t y = year_ + kCalendarBaseYear;
  if (m == 1 || m == 2) {
    m = month_ + 13;
    --y;
  }
  weekday_ = (y / 400 + (m * 2 + day_ + (m + 1) * 3 / 5 + y + y / 4) - y / 100) % 7;
}

void GameClock::adjust_compression() {
  const std::int32_t slow_frame = cap_ == kUnrestrictedCompression ? 20 : 10;
  --adjust_countdown_;
  if (adjust_countdown_ != 0 && load_ticks_ / adjust_countdown_ <= slow_frame) {
    return;
  }
  adjust_countdown_ = 2;
  if (slow_frame < load_ticks_ / 2) {
    effective_ >>= 1;
  } else if ((load_ticks_ / 2 < slow_frame / 5 || force_increase_) && effective_ < requested_) {
    effective_ <<= 1;
  }
  if (effective_ < 1) {
    effective_ = 1;
  }
  const std::int32_t jump = cap_ == kUnrestrictedCompression ? 0x100 : 8;
  if (effective_ < jump && jump <= requested_) {
    effective_ = jump;
  }
  if (requested_ < effective_) {
    effective_ = requested_;
  }
  if (cap_ < effective_) {
    effective_ = cap_;
  }
  load_ticks_ = 0;
  force_increase_ = false;
}

std::int32_t GameClock::advance(std::int32_t timer_ticks, bool station_opened, ClockObserver* observer) {
  load_ticks_ += timer_ticks;
  if (station_opened) {
    adjust_countdown_ = 2;
    force_increase_ = true;
  }
  if (auto_adjust_) {
    adjust_compression();
  }
  frame_ticks_ = (timer_ticks > 15 ? 15 : timer_ticks) * effective_;
  hundredths_ += frame_ticks_;
  while (hundredths_ > 99) {
    hundredths_ -= 100;
    ++second_;
    if (second_ <= 59) {
      continue;
    }
    second_ = 0;
    ++minute_;
    ++minutes_elapsed_;
    if (observer != nullptr) {
      observer->on_minute_started(*this);
    }
    if (minute_ == 60) {
      minute_ = 0;
      ++hour_;
      if (hour_ == 24) {
        hour_ = 0;
        ++day_;
        if (observer != nullptr) {
          observer->on_new_day(*this);
        }
        if (days_in_month(year_, month_) < day_) {
          day_ = 1;
          ++month_;
          if (month_ == 12) {
            month_ = 0;
            ++year_;
          }
        }
        compute_weekday();
      }
    }
    if (observer != nullptr) {
      observer->on_minute_ended(*this);
    }
  }
  return frame_ticks_;
}

bool GameClock::double_compression() {
  if (requested_ < cap_) {
    requested_ <<= 1;
    return true;
  }
  return false;
}

bool GameClock::halve_compression() {
  if (requested_ > 1) {
    requested_ >>= 1;
    return true;
  }
  return false;
}

bool GameClock::reset_compression() {
  if (requested_ > 1) {
    requested_ = 1;
    return true;
  }
  return false;
}

void GameClock::limit_effective_compression(std::int32_t max) {
  if (effective_ > max) {
    effective_ = max;
  }
}

}  // namespace sh
