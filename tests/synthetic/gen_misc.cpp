// The remaining files of the synthetic install: the data directories
// (SHPATH.INI), the sun's ephemeris (SUN.DAT, EARTH.DAT, PLANETS.NDX: the
// textbook first terms of the Earth's orbit, mean longitude, equation of
// the centre and distance, enough for sunrise and sunset) and the
// executables (synthetic/le_writer.h).
#include <numbers>
#include <string_view>

#include "assets/ephemeris.h"
#include "synthetic/exe_manifest.h"
#include "synthetic/generators.h"
#include "synthetic/le_writer.h"
#include "synthetic/table_encoders.h"

namespace sh::test::synth {

void generate_misc(Context& context) {
  constexpr std::string_view kPaths =
      "BUD    =BUD\\\r\nDATA   =DATA\\\r\nART    =ART\\\r\nSFX    =SFX\\\r\nSPEECH =SPEECH\\\r\n"
      "CINES  =CINES\\\r\nSCEN   =SCEN\\\r\nSAVE   =SAVE\\\r\n";
  context.files["SHPATH.INI"] = std::vector<std::uint8_t>(kPaths.begin(), kPaths.end());

  // Heliocentric longitude L = L0 + 2 e sin(M), radius R = 1 - e cos(M),
  // as A cos(B + C tau) terms (tau in millennia from J2000).
  constexpr double kYearRate = 6283.0758;  // radians per millennium
  constexpr double kMeanLongitude = 1.7535;
  constexpr double kPerihelion = 1.7966;  // mean anomaly = L - perihelion longitude
  constexpr double kEccentricity = 0.0167;
  const double half_pi = std::numbers::pi / 2;
  PlanetTheory earth;
  earth.series[0][0] = {{kMeanLongitude, 0, 0},
                        {2 * kEccentricity, kMeanLongitude - kPerihelion - half_pi, kYearRate}};
  earth.series[0][1] = {{kYearRate, 0, 0}};
  earth.series[2][0] = {{1.00014, 0, 0}, {-kEccentricity, kMeanLongitude - kPerihelion, kYearRate}};
  PlanetTheory sun;
  sun.series[2][0] = {{0.0, 0, 0}};
  std::array<const PlanetTheory*, kPlanetBodies> bodies{};
  bodies[static_cast<std::size_t>(PlanetBody::kSun)] = &sun;
  bodies[static_cast<std::size_t>(PlanetBody::kEarth)] = &earth;
  context.files["PLANETS.NDX"] = encode_planets_ndx(bodies);
  context.files["SUN.DAT"] = encode_planet_theory(sun);
  context.files["EARTH.DAT"] = encode_planet_theory(earth);

  // The executables' tables and texts at the original's addresses.
  context.files["SH.EXE"] = write_le_executable(committed_exe_manifest("sh_exe"));
  context.files["SE.EXE"] = write_le_executable(committed_exe_manifest("se_exe"));
}

}  // namespace sh::test::synth
