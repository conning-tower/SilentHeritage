// The planetary-theory files `SUN.DAT`, `EARTH.DAT` and `PLANETS.NDX` (game
// root): Meeus' abridged VSOP87 heliocentric series ("Astronomical
// Algorithms", appendix III) that give the sun's position for sunrise, sunset
// and the sky lighting.
//
// `load_ephemeris` (0xc2dbd) loads `sun.dat` as body 0 and `earth.dat` as body
// 3, each with its `PLANETS.NDX` record. Both describe the Earth's
// heliocentric position: `EARTH.DAT` in the ecliptic and equinox of date
// (VSOP87D), `SUN.DAT` referred to the fixed J2000 ecliptic (VSOP87B).
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "core/install.h"

namespace sh {

// The bodies of `PLANETS.NDX`, in record order.
enum class PlanetBody : std::uint8_t {
  kSun = 0,
  kMercury = 1,
  kVenus = 2,
  kEarth = 3,
  kMars = 4,
  kJupiter = 5,
  kSaturn = 6,
  kUranus = 7,
  kNeptune = 8,
};

inline constexpr std::size_t kPlanetBodies = 9;
// Coordinates of a theory: L (longitude, rad), B (latitude, rad), R (AU).
inline constexpr std::size_t kEphemerisCoords = 3;
// Powers of tau (Julian millennia from J2000.0) per coordinate.
inline constexpr std::size_t kEphemerisPowers = 6;

// One series' range in the concatenated terms of all 9 bodies.
struct SeriesRange {
  std::uint16_t start = 0;  // first term index (unused by the game)
  std::uint16_t count = 0;
};

// One body's `PLANETS.NDX` record (0x48 bytes): ranges per [coord][power].
using PlanetIndex = std::array<std::array<SeriesRange, kEphemerisPowers>, kEphemerisCoords>;

// One term: A cos(B + C tau).
struct EphemerisTerm {
  double a = 0.0;
  double b = 0.0;
  double c = 0.0;
};

// Heliocentric ecliptic coordinates.
struct HeliocentricPosition {
  double longitude_rad = 0.0;
  double latitude_rad = 0.0;
  double radius_au = 0.0;
};

// A body file split into its 18 series ([coord][power]).
struct PlanetTheory {
  std::array<std::array<std::vector<EphemerisTerm>, kEphemerisPowers>, kEphemerisCoords> series;

  // Returns the position at Julian ephemeris day `jde`, without the FK5
  // correction. The longitude is not reduced to one turn.
  HeliocentricPosition heliocentric(double jde) const;
};

// Parses `PLANETS.NDX`; throws `DataError` unless it is exactly 9 records whose
// term ranges are contiguous.
std::array<PlanetIndex, kPlanetBodies> parse_planets_ndx(std::span<const std::uint8_t> data);

// Splits a body file (f64 A, B, C terms back to back) by its index record;
// throws `DataError` if the size does not match the index's term count.
PlanetTheory parse_planet_theory(std::span<const std::uint8_t> data, const PlanetIndex& index);

// The two theories the game loads.
struct Ephemeris {
  PlanetTheory sun;    // SUN.DAT, body slot 0
  PlanetTheory earth;  // EARTH.DAT, body slot 3

  static Ephemeris load(const Install& install);
};

}  // namespace sh
