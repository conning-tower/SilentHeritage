// The game's astronomy library (0xc2cec..0xc3bc1 and 0xd2c28..0xd746f): the
// positions of the sun and the moon in the local sky, their rising and
// setting times, and the moon's illuminated fraction, after Meeus,
// "Astronomical Algorithms" (the Earth from `SUN.DAT`/`EARTH.DAT`'s abridged
// VSOP87 with the FK5 correction, ELP-2000/82 abridged for the moon, the
// IAU 1980 nutation, Laskar's obliquity, the Delta T table 1620..1992).
//
// The original ran on the x87; this port computes in `double` with the
// original's operations, constants and quirks (the FK5 correction uses
// millennia where Meeus has centuries, the hour angle subtracts an
// east-positive longitude, the Newton step of the rising time has an extra
// cos(h) factor). Its integer results are truncated like the original's.
//
// The library keeps the last position's time and place: `sun_rise_set` and
// `moon_rise_set` work on the day of the previous `*_position` call, and the
// moon's rising uses the parallax of the last `moon_position`.
#pragma once

#include <cstdint>

#include "assets/ephemeris.h"

namespace sh {

// A local date and time as the clock passes it to the library.
struct LocalTime {
  std::int32_t year = 1944;  // full year
  std::int32_t month = 1;    // 1..12
  std::int32_t day = 1;      // may run one past the month's end (midnight)
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t second = 0;
};

// The observer as the game passes it (`sky_update_sun_or_moon`): whole
// degrees, the longitude 0..359 east (west longitudes as 360 - degrees; the
// library subtracts it from the sidereal time like Meeus' west-positive
// longitude), and the time zone in hours, added to the local Julian day.
struct Observer {
  std::int32_t longitude = 0;
  std::int32_t latitude = 0;  // north positive
  std::int32_t time_zone = 0;
  std::int32_t height = 0;    // metres (the game passes 0)
};

// A body's place in the sky in tenths of a degree, each truncated from the
// absolute value plus 0.4 degree (`0xc2e07`): azimuth measured from the
// south, westward (Meeus), and elevation.
struct HorizonPosition {
  std::int32_t azimuth = 0;
  std::int32_t elevation = 0;
};

// The rising and setting times of the day, rounded to the minute; all four
// are `kNeverSets` when the body stays up all day and `kNeverRises` when it
// stays down.
struct RiseSetTimes {
  static constexpr std::int32_t kNeverSets = 32000;
  static constexpr std::int32_t kNeverRises = -32000;

  std::int32_t rise_hour = 0;
  std::int32_t rise_minute = 0;
  std::int32_t set_hour = 0;
  std::int32_t set_minute = 0;
};

// `ephem_moon_rise_set`'s results.
struct MoonRiseSet {
  double illuminated = 0.0;  // the moon's illuminated fraction 0..1 now
  RiseSetTimes times;
};

// Returns the Julian day of `year`, `month` (1..12) at `day` (fraction of a
// day included) (`0xd2ecf`): Gregorian after 4 October 1582.
double julian_day(std::int32_t year, std::int32_t month, double day);

// Returns Delta T in seconds at `centuries` from J2000.0 (`0xd3347`): the
// table of 1620..1992 interpolated every two years, Meeus' formulas outside.
double delta_t_seconds(double centuries);

class Astronomy {
 public:
  // Keeps a reference to `ephemeris` (the loaded `SUN.DAT`/`EARTH.DAT`).
  explicit Astronomy(const Ephemeris& ephemeris) : ephemeris_(ephemeris) {}

  // Returns the topocentric apparent place of the sun
  // (`ephem_sun_position`, 0xc2f44).
  HorizonPosition sun_position(const LocalTime& time, const Observer& observer);
  // Returns the sun's rising and setting times for the day of the last
  // position (`ephem_sun_rise_set`, 0xc32b2).
  RiseSetTimes sun_rise_set();

  // Returns the topocentric apparent place of the moon
  // (`ephem_moon_position`, 0xc355b).
  HorizonPosition moon_position(const LocalTime& time, const Observer& observer);
  // Returns the moon's illuminated fraction at the last position's time and
  // its rising and setting times for that day (`ephem_moon_rise_set`,
  // 0xc3891).
  MoonRiseSet moon_rise_set();

 private:
  struct Spherical {
    double longitude = 0.0;
    double latitude = 0.0;
    double radius = 0.0;
  };
  struct Equatorial {
    double right_ascension = 0.0;
    double declination = 0.0;
  };

  // Sets up the time and place of a position (both position entry points).
  void begin_position(const LocalTime& time, const Observer& observer);
  // Returns the Earth's heliocentric place at `centuries` with the FK5
  // correction (`0xd2cef`).
  Spherical earth(double centuries) const;
  // Returns the sun seen from the Earth's centre (`0xd4564` on the Earth).
  Spherical geocentric_sun(double centuries) const;
  // Returns the day's rising, transit and setting as fractions of a turn
  // (`0xd4e4d`); the status is 0, 0x10 (always up) or 0x20 (never up).
  struct DayTimes {
    double rise = 0.0;
    double set = 0.0;
    std::int32_t status = 0;
  };
  DayTimes rise_transit_set(double centuries, const Equatorial (&places)[3], double altitude) const;
  // Moves the time to the start of the local day (the rise/set entry
  // points).
  void start_of_day();
  RiseSetTimes to_times(const DayTimes& day) const;

  const Ephemeris& ephemeris_;
  // The library's globals (0x18b2b8..0x18b3bc) that outlive a call.
  double longitude_ = 0.0;        // 0x18b2d8, radians
  double latitude_ = 0.0;         // 0x18b2e0
  double time_zone_days_ = 0.0;   // 0x18b2e8
  double centuries_ = 0.0;        // 0x18b308: T (UT) from J2000.0
  double delta_t_ = 0.0;          // 0x18b310, seconds
  double obliquity_ = 0.0;        // 0x18b320: mean obliquity
  double nutation_longitude_ = 0.0;  // 0x18b328
  double nutation_obliquity_ = 0.0;  // 0x18b330
  double moon_parallax_ = 0.0;    // 0x18b3b8
};

}  // namespace sh
