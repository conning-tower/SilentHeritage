#include "assets/ephemeris.h"

#include <bit>
#include <cmath>
#include <format>

#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {

namespace {

constexpr std::size_t kIndexRecordSize = 0x48;
constexpr std::size_t kTermSize = 24;
constexpr double kJ2000 = 2451545.0;
constexpr double kDaysPerMillennium = 365250.0;

}  // namespace

std::array<PlanetIndex, kPlanetBodies> parse_planets_ndx(std::span<const std::uint8_t> data) {
  if (data.size() != kIndexRecordSize * kPlanetBodies) {
    throw DataError(std::format("PLANETS.NDX: {} bytes, expected {}", data.size(), kIndexRecordSize * kPlanetBodies));
  }
  ByteReader r(data, "PLANETS.NDX");
  std::array<PlanetIndex, kPlanetBodies> out{};
  std::uint32_t expect = 0;
  for (PlanetIndex& body : out) {
    for (auto& coord : body) {
      for (SeriesRange& range : coord) {
        range.start = r.u16();
        range.count = r.u16();
        if (range.start != expect) {
          r.fail(std::format("term range starts at {}, expected {}", range.start, expect));
        }
        expect += range.count;
      }
    }
  }
  return out;
}

PlanetTheory parse_planet_theory(std::span<const std::uint8_t> data, const PlanetIndex& index) {
  std::size_t total = 0;
  for (const auto& coord : index) {
    for (const SeriesRange& range : coord) {
      total += range.count;
    }
  }
  if (data.size() != total * kTermSize) {
    throw DataError(std::format("planet theory: {} bytes, the index wants {} terms ({} bytes)", data.size(), total,
                                total * kTermSize));
  }
  ByteReader r(data, "planet theory");
  const auto f64 = [&r] {
    const std::uint64_t lo = r.u32();
    const std::uint64_t hi = r.u32();
    return std::bit_cast<double>(lo | (hi << 32));
  };
  PlanetTheory theory;
  for (std::size_t c = 0; c < kEphemerisCoords; ++c) {
    for (std::size_t p = 0; p < kEphemerisPowers; ++p) {
      auto& terms = theory.series[c][p];
      terms.resize(index[c][p].count);
      for (EphemerisTerm& t : terms) {
        t.a = f64();
        t.b = f64();
        t.c = f64();
      }
    }
  }
  return theory;
}

HeliocentricPosition PlanetTheory::heliocentric(double jde) const {
  const double tau = (jde - kJ2000) / kDaysPerMillennium;
  std::array<double, kEphemerisCoords> lbr{};
  for (std::size_t c = 0; c < kEphemerisCoords; ++c) {
    double tau_power = 1.0;
    for (std::size_t p = 0; p < kEphemerisPowers; ++p) {
      double sum = 0.0;
      for (const EphemerisTerm& t : series[c][p]) {
        sum += t.a * std::cos(t.b + t.c * tau);
      }
      lbr[c] += sum * tau_power;
      tau_power *= tau;
    }
  }
  return HeliocentricPosition{lbr[0], lbr[1], lbr[2]};
}

Ephemeris Ephemeris::load(const Install& install) {
  const auto index = parse_planets_ndx(install.read("PLANETS.NDX"));
  return Ephemeris{
      .sun = parse_planet_theory(install.read("SUN.DAT"), index[static_cast<std::size_t>(PlanetBody::kSun)]),
      .earth = parse_planet_theory(install.read("EARTH.DAT"), index[static_cast<std::size_t>(PlanetBody::kEarth)]),
  };
}

}  // namespace sh
