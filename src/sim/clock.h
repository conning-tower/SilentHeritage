// The game clock `0x129c04` (`clock_init`, `clock_advance`,
// `time_compression_*`): calendar date and time of day advanced in
// hundredths of a second by the 100 Hz timer ticks of each frame times the
// time compression.
//
// Three compression values: the *requested* one (keys, doubled up to the
// cap), the *cap* (the lowest maximum the vehicles allowed in the last
// simulated frame, 2048 when unrestricted) and the *effective* one the
// simulation runs at, which follows the requested value every other frame
// as long as frames stay short, drops when they get long, and is cut by the
// game (e.g. to 512 when deep or near contacts).
#pragma once

#include <cstdint>

namespace sh {

// The first year of the calendar (`0x10e3a4`); years are stored since it.
inline constexpr std::int32_t kCalendarBaseYear = 1930;
// The cap when no vehicle restricts the compression.
inline constexpr std::int32_t kUnrestrictedCompression = 0x800;

// Returns the length of `month` (0-based) of the year `kCalendarBaseYear +
// year` (the original's table `0x10e46c` covers 1930..1947 with the
// Gregorian lengths).
std::int32_t days_in_month(std::int32_t year, std::int32_t month);

// Receives the clock's per-minute events at the points the original updates
// the weather and sky, so that random draws happen in the original order.
class ClockObserver {
 public:
  virtual ~ClockObserver() = default;
  // Called once per game minute, after the minute count grew and before the
  // hour rolls over (`minute` may read 60): the fog drift point.
  virtual void on_minute_started(const class GameClock& clock) = 0;
  // Called at midnight after the day grew but before the month rolls over
  // (`clock_update_sky`).
  virtual void on_new_day(const class GameClock& clock) = 0;
  // Called at the end of each minute (`sky_update_sun_or_moon`).
  virtual void on_minute_ended(const class GameClock& clock) = 0;
};

class GameClock {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Sets the date and time (day 1-based, month 0-based, year since 1930)
  // and the clock state of `clock_init`: no ticks, cap unrestricted,
  // compressions 1, weekday computed.
  void init(std::int32_t year, std::int32_t month, std::int32_t day, std::int32_t hour, std::int32_t minute,
            std::int32_t second = 0);

  // Advances the clock by one frame (`clock_advance`): `timer_ticks` is the
  // number of 100 Hz timer ticks since the previous frame (frame time),
  // `station_opened` is set on the first frame of a station (it lets the
  // effective compression double at the next adjustment). Returns the game
  // time of the frame in hundredths of a second (also frame_ticks()).
  std::int32_t advance(std::int32_t timer_ticks, bool station_opened, ClockObserver* observer = nullptr);

  // Requested compression x2 while below the cap; returns whether it changed
  // (the original then plays the click sfx 2).
  bool double_compression();
  // Requested compression / 2 while above 1; returns whether it changed.
  bool halve_compression();
  // Requested compression back to 1 (every alarm); returns whether it changed.
  bool reset_compression();
  // Lowers the effective compression to at most `max` (deep boat, contacts).
  void limit_effective_compression(std::int32_t max);
  // Sets the cap, recomputed by the simulation frame from the vehicles.
  void set_compression_cap(std::int32_t cap) { cap_ = cap; }
  // Disables the frame-load adjustment (the `NOTEST` command-line switch);
  // the effective compression then only changes through the game's limits.
  void set_auto_adjust(bool on) { auto_adjust_ = on; }

  std::int32_t effective_compression() const { return effective_; }
  std::int32_t requested_compression() const { return requested_; }
  std::int32_t compression_cap() const { return cap_; }

  std::int32_t frame_ticks() const { return frame_ticks_; }
  std::int32_t hundredths() const { return hundredths_; }
  std::int32_t second() const { return second_; }
  std::int32_t minute() const { return minute_; }
  std::int32_t hour() const { return hour_; }
  std::int32_t day() const { return day_; }
  std::int32_t month() const { return month_; }
  std::int32_t year() const { return year_; }
  std::int32_t weekday() const { return weekday_; }  // 0 Monday .. 6 Sunday.
  std::int32_t minutes_elapsed() const { return minutes_elapsed_; }

 private:
  void compute_weekday();
  void adjust_compression();

  std::int32_t frame_ticks_ = 0;      // +0x00 game hundredths this frame.
  std::int32_t hundredths_ = 0;       // +0x04
  std::int32_t second_ = 0;           // +0x08
  std::int32_t minute_ = 0;           // +0x0c
  std::int32_t hour_ = 0;             // +0x10
  std::int32_t day_ = 1;              // +0x14
  std::int32_t month_ = 0;            // +0x18
  std::int32_t year_ = 0;             // +0x1c
  std::int32_t weekday_ = 0;          // +0x20
  std::int32_t effective_ = 1;        // +0x48
  std::int32_t requested_ = 1;        // +0x4c
  std::int32_t cap_ = kUnrestrictedCompression;  // +0x50
  std::int32_t minutes_elapsed_ = 0;  // +0x54
  std::int32_t load_ticks_ = 0;       // 0x129bc8: timer ticks since the last adjustment.
  std::int32_t adjust_countdown_ = 1; // 0x129c60: frames to the next adjustment.
  bool force_increase_ = false;       // 0x129c64
  bool auto_adjust_ = true;           // 0x104e7c
};

}  // namespace sh
