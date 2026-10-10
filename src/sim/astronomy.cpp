#include "sim/astronomy.h"

#include <array>
#include <cmath>
#include <cstddef>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"

namespace sh {

namespace {

// The library's constants: computed at start-up (0xc2cec) from pi, or
// literals of the data segment (which round pi / 180 differently).
constexpr double kPi = 3.141592653589793;  // 0x102d1c
constexpr double kTwoPi = kPi * 2.0;        // 0x18b258
constexpr double kRadiansPerDegree = kPi / 180.0;   // 0x18b260
constexpr double kRadiansPerHour = kPi / 12.0;      // 0x18b268
constexpr double kDegreesPerRadian = 180.0 / kPi;   // 0x18b270
constexpr double kHoursPerRadian = 12.0 / kPi;      // 0x18b278
constexpr double kSunParallax = kRadiansPerDegree / 3600.0 * 8.794;  // 0x18b288
constexpr double kSunRiseAltitude = kRadiansPerDegree * -0.8333;     // 0x18b2a8
constexpr double kDegree = 0.0174532925199433;        // literal
constexpr double kArcsecond = 4.84813681109536e-06;   // literal
constexpr double kTwoPiLiteral = 6.283185307179586;   // 0x1005f8
constexpr double kDaysPerCentury = 36525.0;
constexpr double kDayInCenturies = 2.737850787132101e-05;  // 0xff2b2
constexpr double kSecondsToCenturies = 3.168808781402895e-10;  // 0x102d2c
constexpr double kEarthRadiusKm = 6378.14;      // 0x102d24
constexpr double kAstronomicalUnitKm = 149597870.0;
constexpr double kAberration = 9.936508497454117e-05;  // 0xd4877: 20.49552"

struct Rectangular {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

// Returns `x` reduced to [0, 2 pi) (`0xe2a0d`).
double reduce(double x) { return x - std::floor(x / kTwoPi) * kTwoPi; }
// Returns `x` reduced to [-pi, pi) (`0xe2a3b`).
double reduce_signed(double x) { return x - std::floor(x / kTwoPi + 0.5) * kTwoPi; }

// Returns the polynomial `c[0] + c[1] x + ...` evaluated as `0xe2998`.
template <std::size_t N>
double polynomial(const std::array<double, N>& c, double x) {
  double r = c[N - 1];
  for (std::size_t i = N - 1; i-- > 0;) {
    r = r * x + c[i];
  }
  return r;
}

// Returns whole degrees in radians (`0xc2e7e`).
double degrees_to_radians(std::int32_t v) {
  const double sign = v < 0 ? -1.0 : 1.0;
  const double magnitude = std::abs(v);
  return sign * (magnitude + 2.7777777777777777e-10) * kRadiansPerDegree;
}

// Returns an angle in tenths of a degree (`0xc2e07`).
std::int32_t to_tenths(double radians) {
  const double v = (std::abs(radians) * kDegreesPerRadian + 0.4) * 10.0;
  return static_cast<std::int32_t>(std::trunc(radians < 0.0 ? -v : v));
}

// Splits a time of day in radians into hours and minutes rounded to the
// minute (`0xc2ec2`).
void to_hours_minutes(double radians, std::int32_t& hour, std::int32_t& minute) {
  const double h = std::abs(radians) * kHoursPerRadian + 0.00833333333;
  const double whole = std::floor(h);
  hour = static_cast<std::int32_t>(whole);
  minute = static_cast<std::int32_t>(std::trunc((h - whole) * 60.0));
}

// Returns the mean sidereal time at Greenwich (`0xd443d`).
double sidereal_time(double t) {
  return reduce((((0.000387933 - t / 38710000.0) * t + 13185000.77005374) * t + 280.46061837) * kDegree);
}

// Returns the mean obliquity of the ecliptic (`0xd34c4`, Laskar).
double mean_obliquity(double t) {
  static constexpr std::array<double, 11> kArcseconds = {84381.448, -4680.93, -1.55,  1999.25, -51.38, -249.67,
                                                         -39.05,    7.12,     27.87, 5.79,    2.45};  // 0xd346c
  return polynomial(kArcseconds, t / 100.0) * kArcsecond;
}

// The nutation's fundamental arguments (`0xd359a`), degrees by powers of T.
constexpr std::array<double, 4> kNodeArgument = {125.0445222, -1934.1362608, 0.00207833, 2.22e-06};       // 0xd34fa
constexpr std::array<double, 4> kSunAnomalyArgument = {357.5277233, 35999.05034, -0.0001603, -3.33e-06};  // 0xd351a
constexpr std::array<double, 4> kMoonAnomalyArgument = {134.9629814, 477198.8673981, 0.0086972, 1.778e-05};
constexpr std::array<double, 4> kMoonLatitudeArgument = {93.2719103, 483202.0175381, -0.0036825, 3.056e-06};
constexpr std::array<double, 4> kElongationArgument = {297.8503631, 445267.11148, -0.0019142, 5.278e-06};

// One periodic term of the nutation: multiples of D, M, M', F and the node,
// and the coefficient a + b T in arcseconds.
struct NutationTerm {
  std::array<std::int8_t, 5> multiples;
  double a;
  double b;
};

// The terms of the unrolled sums of 0xd359a.
constexpr NutationTerm kNutationLongitude[] = {
    {{0, 0, 0, 0, 1}, -17.1996, -0.01742}, {{-2, 0, 0, 2, 2}, -1.3187, -0.00016},
    {{0, 0, 0, 2, 2}, -0.2274, -0.00002}, {{0, 0, 0, 0, 2}, 0.2062, 0.00002},
    {{0, 1, 0, 0, 0}, 0.1426, -0.00034}, {{0, 0, 1, 0, 0}, 0.0712, 0.00001},
    {{-2, 1, 0, 2, 2}, -0.0517, 0.00012}, {{0, 0, 0, 2, 1}, -0.0386, -0.00004},
    {{0, 0, 1, 2, 2}, -0.0301, 0.0}, {{2, 1, 0, -2, -2}, -0.0217, 0.00005},
    {{2, 0, -1, 0, 0}, 0.0158, 0.0}, {{-2, 0, 0, 2, 1}, 0.0129, 0.00001},
    {{0, 0, -1, 2, 2}, 0.0123, 0.0}, {{2, 0, 0, 0, 0}, 0.0063, 0.0},
    {{0, 0, 1, 0, 1}, 0.0063, 0.00001}, {{2, 0, -1, 2, 2}, -0.0059, 0.0},
    {{0, 0, 1, 0, -1}, 0.0058, 0.00001}, {{0, 0, 1, 2, 1}, -0.0051, 0.0},
    {{2, 0, -2, 0, 0}, -0.0048, 0.0}, {{0, 0, -2, 2, 1}, 0.0046, 0.0},
    {{2, 0, 0, 2, 2}, -0.0038, 0.0}, {{0, 0, 2, 2, 2}, -0.0031, 0.0},
    {{0, 0, 2, 0, 0}, 0.0029, 0.0}, {{-2, 0, 1, 2, 2}, 0.0029, 0.0},
    {{0, 0, 0, 2, 0}, 0.0026, 0.0}, {{2, 0, 0, -2, 0}, 0.0022, 0.0},
    {{0, 0, -1, 2, 1}, 0.0021, 0.0}, {{0, 2, 0, 0, 0}, 0.0017, -0.00001},
    {{2, 0, -1, 0, 1}, 0.0016, 0.0}, {{-2, 2, 0, 2, 2}, -0.0016, 0.00001},
    {{0, 1, 0, 0, 1}, -0.0015, 0.0}, {{-2, 0, 1, 0, 1}, -0.0013, 0.0},
    {{0, 1, 0, 0, -1}, 0.0012, 0.0}, {{0, 0, -2, 2, 0}, -0.0011, 0.0},
    {{2, 0, -1, 2, 1}, -0.001, 0.0}, {{2, 0, 1, 2, 2}, -0.0008, 0.0},
    {{0, 1, 0, 2, 2}, 0.0007, 0.0}, {{-2, 1, 1, 0, 0}, -0.0007, 0.0},
    {{2, 0, 0, 2, 1}, -0.0007, 0.0}, {{0, -1, 0, 2, 2}, -0.0007, 0.0},
    {{2, 0, 1, 0, 0}, 0.0006, 0.0}, {{-2, 0, 1, 2, 1}, 0.0006, 0.0},
    {{-2, 0, 2, 2, 2}, 0.0006, 0.0}, {{2, 0, 0, 0, 1}, -0.0006, 0.0},
    {{2, 0, -2, 0, 1}, -0.0006, 0.0}, {{2, 0, 0, 0, -1}, 0.0005, 0.0},
    {{2, 1, 0, -2, -1}, 0.0005, 0.0}, {{0, 1, -1, 0, 0}, -0.0005, 0.0},
    {{0, 0, 2, 2, 1}, -0.0005, 0.0}, {{1, 0, -1, 0, 0}, 0.0004, 0.0},
    {{2, -1, 0, 0, 0}, 0.0004, 0.0}, {{-2, 0, 2, 0, 1}, 0.0004, 0.0},
    {{-2, 1, 0, 2, 1}, 0.0004, 0.0}, {{1, 0, 0, 0, 0}, -0.0004, 0.0},
    {{0, 0, -1, 2, 0}, -0.0004, 0.0}, {{0, 0, 1, 2, 0}, 0.0003, 0.0},
    {{1, 1, -1, 0, 0}, 0.0003, 0.0}, {{0, 1, 1, 0, 0}, -0.0003, 0.0},
    {{0, 0, 3, 2, 2}, -0.0003, 0.0}, {{0, 0, -2, 2, 2}, -0.0003, 0.0},
    {{0, -1, 1, 2, 2}, -0.0003, 0.0}, {{2, -1, 0, 2, 2}, -0.0003, 0.0},
    {{2, -1, -1, 2, 2}, -0.0003, 0.0}
};
constexpr NutationTerm kNutationObliquity[] = {
    {{0, 0, 0, 0, 1}, 9.2025, 0.00089}, {{-2, 0, 0, 2, 2}, 0.5736, -0.00031},
    {{0, 0, 0, 2, 2}, 0.0977, -0.00005}, {{0, 0, 0, 0, 2}, -0.0895, 0.00005},
    {{-2, 1, 0, 2, 2}, 0.0224, -0.00006}, {{0, 0, 0, 2, 1}, 0.02, 0.0},
    {{0, 0, 1, 2, 2}, 0.0129, -0.00001}, {{2, 1, 0, -2, -2}, -0.0095, 0.00003},
    {{-2, 0, 0, 2, 1}, -0.007, 0.0}, {{0, 1, 0, 0, 0}, 0.0054, -0.00001},
    {{0, 0, -1, 2, 2}, -0.0053, 0.0}, {{0, 0, 1, 0, 1}, -0.0033, 0.0},
    {{0, 0, 1, 0, -1}, 0.0032, 0.0}, {{0, 0, 1, 2, 1}, 0.0027, 0.0},
    {{2, 0, -1, 2, 2}, 0.0026, 0.0}, {{0, 0, -2, 2, 1}, -0.0024, 0.0},
    {{2, 0, 0, 2, 2}, 0.0016, 0.0}, {{0, 0, 2, 2, 2}, 0.0013, 0.0},
    {{-2, 0, 1, 2, 2}, -0.0012, 0.0}, {{0, 0, -1, 2, 1}, -0.001, 0.0},
    {{0, 1, 0, 0, 1}, 0.0009, 0.0}, {{2, 0, -1, 0, 1}, -0.0008, 0.0},
    {{-2, 0, 1, 0, 1}, 0.0007, 0.0}, {{-2, 2, 0, 2, 2}, 0.0007, 0.0},
    {{0, 0, 1, 0, 0}, -0.0007, 0.0}, {{0, 1, 0, 0, -1}, 0.0006, 0.0},
    {{2, 0, -1, 2, 1}, 0.0005, 0.0}, {{2, 0, 0, 0, 1}, 0.0003, 0.0},
    {{2, 0, 0, 0, -1}, 0.0003, 0.0}, {{2, 0, 0, 2, 1}, 0.0003, 0.0},
    {{2, 0, -2, 0, 1}, 0.0003, 0.0}, {{0, 0, 2, 2, 1}, 0.0003, 0.0},
    {{0, -1, 0, 2, 2}, 0.0003, 0.0}, {{2, 1, 0, -2, -1}, 0.0003, 0.0},
    {{2, 0, 1, 2, 2}, 0.0003, 0.0}, {{0, 1, 0, 2, 2}, -0.0003, 0.0},
    {{-2, 0, 1, 2, 1}, -0.0003, 0.0}, {{-2, 0, 2, 2, 2}, -0.0003, 0.0}
};

struct Nutation {
  double longitude = 0.0;  // radians
  double obliquity = 0.0;
};

// Returns the nutation in longitude and obliquity (`0xd359a`).
Nutation nutation(double t) {
  const std::array<double, 5> args = {
      reduce(polynomial(kElongationArgument, t) * kDegree), reduce(polynomial(kSunAnomalyArgument, t) * kDegree),
      reduce(polynomial(kMoonAnomalyArgument, t) * kDegree), reduce(polynomial(kMoonLatitudeArgument, t) * kDegree),
      reduce(polynomial(kNodeArgument, t) * kDegree)};
  auto argument = [&](const std::array<std::int8_t, 5>& m) {
    double x = 0.0;
    for (std::size_t i = 0; i < args.size(); ++i) {
      x += m[i] * args[i];
    }
    return x;
  };
  Nutation n;
  for (const NutationTerm& term : kNutationLongitude) {
    n.longitude += (term.a + term.b * t) * std::sin(argument(term.multiples));
  }
  for (const NutationTerm& term : kNutationObliquity) {
    n.obliquity += (term.a + term.b * t) * std::cos(argument(term.multiples));
  }
  n.longitude *= kArcsecond;
  n.obliquity *= kArcsecond;
  return n;
}

// The moon's fundamental arguments (`0xe2dd6`, `0xd5439`), degrees by
// powers of T.
constexpr std::array<double, 5> kMoonMeanLongitude = {218.3164591, 481267.88134236, -0.0013268, 1.855835e-06,
                                                      1.53388e-08};  // 0xd5411
constexpr std::array<double, 5> kMoonElongation = {297.8502042, 445267.1115168, -0.00163, 1.831945e-06,
                                                   8.84447e-06};  // 0xe2d3e
constexpr std::array<double, 4> kSunMeanAnomaly = {357.5291092, 35999.0502909, -0.0001536, 4.083299e-08};
constexpr std::array<double, 5> kMoonMeanAnomaly = {134.9634114, 477198.8676313, 0.008997, 1.434741e-05,
                                                    6.79717e-08};
constexpr std::array<double, 5> kMoonArgumentOfLatitude = {93.2720993, 483202.0175273, -0.0034029, 2.836075e-07,
                                                           1.15833e-09};

// One periodic term of the moon's series: multiples of D, M, M', F, the
// coefficient (degrees or km) and the power of E applied.
struct MoonTerm {
  std::array<std::int8_t, 4> multiples;
  double coefficient;
  std::int32_t e_power;
};

// The terms of the unrolled sums 0xd5613 (sine, degrees), 0xd5cb5 (sine,
// degrees) and 0xd64a7 (cosine, km).
constexpr MoonTerm kMoonLongitude[] = {
    {{0, 0, 1, 0}, 6.288774, 0}, {{2, 0, -1, 0}, 1.274027, 0},
    {{2, 0, 0, 0}, 0.658314, 0}, {{0, 0, 2, 0}, 0.213618, 0},
    {{0, 1, 0, 0}, -0.185116, 1}, {{0, 0, 0, 2}, -0.114332, 0},
    {{2, 0, -2, 0}, 0.058793, 0}, {{-2, 1, 1, 0}, -0.057066, 1},
    {{2, 0, 1, 0}, 0.053322, 0}, {{2, -1, 0, 0}, 0.045758, 1},
    {{0, 1, -1, 0}, -0.040923, 1}, {{1, 0, 0, 0}, -0.03472, 0},
    {{0, 1, 1, 0}, -0.030383, 1}, {{2, 0, 0, -2}, 0.015327, 0},
    {{0, 0, 1, 2}, -0.012528, 0}, {{0, 0, -1, 2}, -0.01098, 0},
    {{4, 0, -1, 0}, 0.010675, 0}, {{0, 0, 3, 0}, 0.010034, 0},
    {{4, 0, -2, 0}, 0.008548, 0}, {{2, 1, -1, 0}, -0.007888, 1},
    {{2, 1, 0, 0}, -0.006766, 1}, {{1, 0, -1, 0}, -0.005163, 0},
    {{1, 1, 0, 0}, 0.004987, 1}, {{2, -1, 1, 0}, 0.004036, 1},
    {{2, 0, 2, 0}, 0.003994, 0}, {{4, 0, 0, 0}, 0.003861, 0},
    {{2, 0, -3, 0}, 0.003665, 0}, {{0, 1, -2, 0}, -0.002689, 1},
    {{2, 0, -1, 2}, -0.002602, 0}, {{-2, 1, 2, 0}, -0.00239, 1},
    {{1, 0, 1, 0}, -0.002348, 0}, {{2, -2, 0, 0}, 0.002236, 2},
    {{0, 1, 2, 0}, -0.00212, 1}, {{0, 2, 0, 0}, -0.002069, 2},
    {{-2, 2, 1, 0}, -0.002048, 2}, {{2, 0, 1, -2}, -0.001773, 0},
    {{2, 0, 0, 2}, -0.001595, 0}, {{-4, 1, 1, 0}, -0.001215, 1},
    {{0, 0, 2, 2}, -0.00111, 0}, {{3, 0, -1, 0}, -0.000892, 0},
    {{2, 1, 1, 0}, -0.00081, 1}, {{-4, 1, 2, 0}, -0.000759, 1},
    {{0, 2, -1, 0}, -0.000713, 2}, {{2, 2, -1, 0}, -0.0007, 2},
    {{2, 1, -2, 0}, 0.000691, 1}, {{-2, 1, 0, 2}, -0.000596, 1},
    {{4, 0, 1, 0}, 0.000549, 0}, {{0, 0, 4, 0}, 0.000537, 0},
    {{4, -1, 0, 0}, 0.00052, 1}, {{1, 0, -2, 0}, -0.000487, 0},
    {{2, 1, 0, -2}, -0.000399, 1}, {{0, 0, -2, 2}, 0.000381, 0},
    {{1, 1, 1, 0}, 0.000351, 1}, {{3, 0, -2, 0}, -0.00034, 0},
    {{4, 0, -3, 0}, 0.00033, 0}, {{2, -1, 2, 0}, 0.000327, 1},
    {{0, 2, 1, 0}, -0.000323, 2}, {{1, 1, -1, 0}, 0.000299, 1},
    {{2, 0, 3, 0}, 0.000294, 0}
};
constexpr MoonTerm kMoonLatitude[] = {
    {{0, 0, 0, 1}, 5.128122, 0}, {{0, 0, 1, 1}, 0.280602, 0},
    {{0, 0, -1, 1}, -0.277693, 0}, {{2, 0, 0, -1}, 0.173237, 0},
    {{2, 0, -1, 1}, 0.055413, 0}, {{-2, 0, 1, 1}, -0.046271, 0},
    {{2, 0, 0, 1}, 0.032573, 0}, {{0, 0, 2, 1}, 0.017198, 0},
    {{2, 0, 1, -1}, 0.009266, 0}, {{0, 0, -2, 1}, -0.008822, 0},
    {{-2, 1, 0, 1}, -0.008216, 1}, {{-2, 0, 2, 1}, -0.004324, 0},
    {{2, 0, 1, 1}, 0.0042, 0}, {{2, 1, 0, -1}, -0.003359, 1},
    {{2, -1, -1, 1}, 0.002463, 1}, {{2, -1, 0, 1}, 0.002211, 1},
    {{-2, 1, 1, 1}, -0.002065, 1}, {{0, -1, 1, 1}, 0.00187, 1},
    {{-4, 0, 1, 1}, -0.001828, 0}, {{0, 1, 0, 1}, -0.001794, 1},
    {{0, 0, 0, 3}, -0.001749, 0}, {{0, 1, -1, 1}, -0.001565, 1},
    {{1, 0, 0, 1}, -0.001491, 0}, {{0, 1, 1, 1}, -0.001475, 1},
    {{0, 1, 1, -1}, -0.00141, 1}, {{0, -1, 0, 1}, 0.001344, 1},
    {{1, 0, 0, -1}, -0.001335, 0}, {{0, 0, 3, 1}, 0.001107, 0},
    {{4, 0, 0, -1}, 0.001021, 0}, {{4, 0, -1, 1}, 0.000833, 0},
    {{0, 0, -1, 3}, -0.000777, 0}, {{4, 0, -2, 1}, 0.000671, 0},
    {{2, 0, 0, -3}, 0.000607, 0}, {{2, 0, 2, -1}, 0.000596, 0},
    {{2, -1, 1, -1}, 0.000491, 1}, {{2, 0, -2, 1}, -0.000451, 0},
    {{0, 0, -3, 1}, -0.000439, 0}, {{2, 0, 2, 1}, 0.000422, 0},
    {{-2, 0, 3, 1}, -0.000421, 0}, {{2, 1, -1, 1}, -0.000366, 1},
    {{2, 1, 0, 1}, -0.000351, 1}, {{4, 0, 0, 1}, 0.000331, 0},
    {{2, -1, 1, 1}, 0.000315, 1}, {{-2, 2, 0, 1}, -0.000302, 2},
    {{0, 0, 1, 3}, -0.000283, 0}, {{2, 1, 1, -1}, -0.000229, 1},
    {{1, 1, 0, 1}, 0.000223, 1}, {{1, 1, 0, -1}, 0.000223, 1},
    {{0, -1, 2, 1}, 0.00022, 1}, {{2, 1, -1, -1}, -0.00022, 1},
    {{1, 0, 1, 1}, -0.000185, 0}, {{-2, 1, 2, 1}, -0.000181, 1},
    {{0, 1, 2, 1}, -0.000177, 1}, {{-4, 0, 2, 1}, -0.000176, 0},
    {{-4, 1, 1, 1}, -0.000166, 1}, {{1, 0, 1, -1}, -0.000164, 0},
    {{4, 0, 1, -1}, 0.000132, 0}, {{-1, 0, 1, 1}, 0.000119, 0},
    {{-4, 1, 0, 1}, -0.000115, 1}, {{2, -2, 0, 1}, 0.000107, 2}
};
constexpr MoonTerm kMoonDistance[] = {
    {{0, 0, 1, 0}, -20905.355, 0}, {{2, 0, -1, 0}, -3699.111, 0},
    {{2, 0, 0, 0}, -2955.968, 0}, {{0, 0, 2, 0}, -569.925, 0},
    {{2, 0, -2, 0}, 246.158, 0}, {{2, -1, 0, 0}, -204.586, 1},
    {{2, 0, 1, 0}, -170.733, 0}, {{-2, 1, 1, 0}, -152.138, 1},
    {{0, 1, -1, 0}, -129.62, 1}, {{1, 0, 0, 0}, 108.743, 0},
    {{0, 1, 1, 0}, 104.755, 1}, {{0, 0, -1, 2}, 79.661, 0},
    {{0, 1, 0, 0}, 48.888, 1}, {{4, 0, -1, 0}, -34.782, 0},
    {{2, 1, 0, 0}, 30.824, 1}, {{2, 1, -1, 0}, 24.208, 1},
    {{0, 0, 3, 0}, -23.21, 0}, {{4, 0, -2, 0}, -21.636, 0},
    {{1, 1, 0, 0}, -16.675, 1}, {{2, 0, -3, 0}, 14.403, 0},
    {{2, -1, 1, 0}, -12.831, 1}, {{4, 0, 0, 0}, -11.65, 0},
    {{2, 0, 2, 0}, -10.445, 0}, {{2, 0, 0, -2}, 10.321, 0},
    {{-2, 1, 2, 0}, 10.056, 1}, {{2, -2, 0, 0}, -9.884, 2},
    {{-2, 0, 1, 2}, 8.752, 0}, {{1, 0, -1, 0}, -8.379, 0},
    {{0, 1, -2, 0}, -7.003, 1}, {{1, 0, 1, 0}, 6.322, 0},
    {{0, 1, 2, 0}, 5.751, 1}, {{-2, 2, 1, 0}, -4.95, 2},
    {{0, 0, -2, 2}, -4.421, 0}, {{2, 0, 1, -2}, 4.13, 0},
    {{-4, 1, 1, 0}, -3.958, 1}, {{3, 0, -1, 0}, 3.258, 0},
    {{0, 0, 0, 2}, -3.149, 0}, {{2, 1, 1, 0}, 2.616, 1},
    {{2, 2, -1, 0}, 2.354, 2}, {{0, 2, -1, 0}, -2.117, 2},
    {{-4, 1, 2, 0}, -1.897, 1}, {{1, 0, -2, 0}, -1.739, 0},
    {{4, -1, 0, 0}, -1.571, 1}, {{4, 0, 1, 0}, -1.423, 0},
    {{0, 2, 1, 0}, 1.165, 2}, {{0, 0, 4, 0}, -1.117, 0}
};

}  // namespace

double julian_day(std::int32_t year, std::int32_t month, double day) {
  std::int32_t y = year;
  std::int32_t m = month;
  if (month < 3) {
    --y;
    m += 12;
  }
  const std::int32_t a = y / 100;
  std::int32_t b = 0;
  if (year > 1582 || (year == 1582 && (month > 10 || (month == 10 && day > 4.0)))) {
    b = 2 - a + a / 4;
  }
  const double years = std::trunc(365.25 * (y + 4716));
  const double months = std::trunc(30.6001 * (m + 1));
  return months + years + day + b + -1524.5;
}

double delta_t_seconds(double centuries) {
  // Tenths of a second every two years from 1620 to 1992 (SH.EXE 0xd305b).
  static const std::array<std::int32_t, 187> kTable = [] {
    constexpr std::uint32_t kAddress = 0xd305b;
    const auto t = exe_i32_array<187>(game_exe(), kAddress);
    check_exe_table(t.front() == 1240 && t.back() == 580, "delta T", kAddress);
    return t;
  }();
  const double t = centuries;
  const double year = t * 100.0 + 2000.0;
  if (year > 2000.0) {
    return (t * 32.5 + 123.5) * t + 102.3;
  }
  if (year >= 1620.0) {
    std::int32_t i = static_cast<std::int32_t>(std::trunc((year + -1620.0) * 0.5));
    if (i > 185) {
      i = 185;
    }
    const double fraction = year * 0.5 - i + -810.0;
    const auto k = static_cast<std::size_t>(i);
    return ((kTable[k + 1] - kTable[k]) * fraction + kTable[k]) / 10.0;
  }
  if (year >= 948.0) {
    return (t * 22.5 + 67.5) * t + 50.6;
  }
  return (t * 46.5 + 573.36) * t + 2715.6;
}

namespace {

// Returns the rectangular coordinates of `s` (`0xe2b0a`).
Rectangular to_rectangular(double longitude, double latitude, double radius) {
  return {std::cos(longitude) * radius * std::cos(latitude), std::sin(longitude) * radius * std::cos(latitude),
          std::sin(latitude) * radius};
}

// Returns `a` - `b` in spherical coordinates (`0xd4564` with `0xe2b54`).
void difference(const Rectangular& a, const Rectangular& b, double& longitude, double& latitude, double& radius) {
  const Rectangular d{a.x - b.x, a.y - b.y, a.z - b.z};
  longitude = std::atan2(d.y, d.x);
  if (longitude < 0.0) {
    longitude += kTwoPiLiteral;
  }
  radius = std::sqrt(d.y * d.y + d.x * d.x + d.z * d.z);
  latitude = std::asin(d.z / radius);
}

// Converts ecliptic to equatorial coordinates (`0xd4621`).
void to_equatorial(double longitude, double latitude, double obliquity, double& ra, double& dec) {
  const double sin_e = std::sin(obliquity);
  const double cos_e = std::cos(obliquity);
  const double sin_l = std::sin(longitude);
  const double cos_l = std::cos(longitude);
  const double sin_b = std::sin(latitude);
  const double cos_b = std::cos(latitude);
  ra = std::atan2(cos_b * sin_l * cos_e - sin_b * sin_e, cos_b * cos_l);
  if (ra < 0.0) {
    ra += kTwoPiLiteral;
  }
  dec = std::asin(cos_b * sin_e * sin_l + sin_b * cos_e);
}

// The observer's geocentric place: rho sin(phi') and rho cos(phi') (`0xd44df`).
struct ObserverPlace {
  double rho_cos = 0.0;
  double rho_sin = 0.0;
};
ObserverPlace observer_place(double latitude, double height) {
  const double h = height / 6378140.0;
  const double u = std::atan(std::tan(latitude) * 0.9966471868221031);
  return {std::cos(u) + h * std::cos(latitude), std::sin(u) * 0.9966471868221031 + h * std::sin(latitude)};
}

// Corrects for the aberration with the sun's low-precision theory
// (`0xd495e`, flag 2: with the e-terms; `0xd487f` for the sun).
void aberration(double t, double obliquity, double& ra, double& dec) {
  const double cos_e = std::cos(obliquity);
  const double sin_e = std::sin(obliquity);
  const double cos_a = std::cos(ra);
  const double sin_a = std::sin(ra);
  const double cos_d = std::cos(dec);
  const double sin_d = std::sin(dec);
  // 0xd487f: the sun's true longitude and the eccentricity.
  const double l0 = ((t * 0.0003032 + 36000.76983) * t + 280.46646) * kDegree;
  const double m = ((35999.05028 - t * 0.0001561) * t + 357.5291) * kDegree;
  const double e = 0.016708617 - (t * 1.236e-07 + 4.204e-05) * t;
  const double e2 = e * e;
  const double centre = (2.0 - e2 * 0.25) * e * std::sin(m) + std::sin(m * 2.0) * (e2 * 1.25) +
                        e * 1.0833333333 * e2 * std::sin(m * 3.0);
  const double sun = centre + l0;
  const double perihelion = ((t * 0.00046 + 1.71954) * t + 102.93735) * kDegree;
  const double c = std::cos(sun) - std::cos(perihelion) * e;
  const double s = std::sin(sun) - std::sin(perihelion) * e;
  ra = ra - (cos_a * c * cos_e + sin_a * s) * kAberration / cos_d;
  dec = dec - ((sin_e * cos_d - sin_a * sin_d * cos_e) * c + cos_a * sin_d * s) * kAberration;
}

// Moves the place for the parallax, approximately (`0xd4cf2`, the sun).
void parallax_approximate(double sin_parallax, double theta, const ObserverPlace& place, double& ra, double& dec) {
  const double h = theta - ra;
  const double cos_h = std::cos(h);
  ra = ra - sin_parallax * place.rho_cos * std::sin(h) / std::cos(dec);
  dec = dec - (place.rho_sin * std::cos(dec) - place.rho_cos * cos_h * std::sin(dec)) * sin_parallax;
}

// Moves the place for the parallax, rigorously (`0xd4d73`, the moon).
void parallax_exact(double sin_parallax, double theta, const ObserverPlace& place, double& ra, double& dec) {
  const double h = theta - ra;
  const double cos_d = std::cos(dec);
  const double sin_d = std::sin(dec);
  const double a = std::sin(h) * cos_d;
  const double b = std::cos(h) * cos_d - place.rho_cos * sin_parallax;
  const double c = sin_d - place.rho_sin * sin_parallax;
  const double q = std::sqrt(a * a + b * b + c * c);
  ra = reduce(theta - std::atan2(a, b));
  dec = std::asin(c / q);
}

// Returns the horizontal place (`0xd46dc`).
HorizonPosition to_horizon(double ra, double dec, double theta, double latitude) {
  const double h = theta - ra;
  const double cos_h = std::cos(h);
  const double sin_lat = std::sin(latitude);
  const double cos_lat = std::cos(latitude);
  const double azimuth = std::atan2(std::sin(h), cos_h * sin_lat - std::tan(dec) * cos_lat);
  const double elevation = std::asin(std::cos(dec) * cos_lat * cos_h + std::sin(dec) * sin_lat);
  return {to_tenths(azimuth), to_tenths(elevation)};
}

// Returns the moon's geocentric place at `t` (`0xd5439`): longitude and
// latitude in radians, distance in km.
void moon_geocentric(double t, double& longitude, double& latitude, double& distance) {
  const double l = reduce(polynomial(kMoonMeanLongitude, t) * kDegree);
  const double d = reduce(polynomial(kMoonElongation, t) * kDegree);
  const double m = reduce(polynomial(kSunMeanAnomaly, t) * kDegree);
  const double mp = reduce(polynomial(kMoonMeanAnomaly, t) * kDegree);
  const double f = reduce(polynomial(kMoonArgumentOfLatitude, t) * kDegree);
  const double e = 1.0 - (t * 7.4e-06 + 0.002516) * t;
  const double e2 = e * e;
  const double a1 = (t * 131.849 + 119.75) * kDegree;
  const double a2 = (t * 479264.29 + 53.09) * kDegree;
  const double a3 = (t * 481266.484 + 313.45) * kDegree;
  auto sum = [&](const auto& terms, auto trig) {
    double s = 0.0;
    for (const MoonTerm& term : terms) {
      const double x = term.multiples[0] * d + term.multiples[1] * m + term.multiples[2] * mp + term.multiples[3] * f;
      const double factor = term.e_power == 0 ? 1.0 : term.e_power == 1 ? e : e2;
      s += term.coefficient * factor * trig(x);
    }
    return s;
  };
  auto sine = [](double x) { return std::sin(x); };
  auto cosine = [](double x) { return std::cos(x); };
  double sum_l = sum(kMoonLongitude, sine);
  double sum_b = sum(kMoonLatitude, sine);
  const double sum_r = sum(kMoonDistance, cosine);
  sum_l = std::sin(l - f) * 0.001962 + std::sin(a1) * 0.003958 + std::sin(a2) * 0.000318 + sum_l;
  sum_b = std::sin(l) * -0.002235 + std::sin(a3) * 0.000382 + std::sin(a1) * 0.00035 * std::cos(f) +
          std::sin(l - mp) * 0.000127 - std::sin(l + mp) * 0.000115 + sum_b;
  longitude = sum_l * kDegree + l;
  while (longitude < 0.0) {
    longitude += kTwoPiLiteral;
  }
  latitude = sum_b * kDegree;
  distance = sum_r + 385000.56;
}

// Returns Meeus' three-point interpolation at `n` (`0xe2baa`).
double interpolate(double y1, double y2, double y3, double n) {
  const double a = y2 - y1;
  const double b = y3 - y2;
  const double c = b - a;
  return (a + b + n * c) * n * 0.5 + y2;
}

}  // namespace

Astronomy::Spherical Astronomy::earth(double centuries) const {
  // 0xd2cef: the game passes centuries and divides by 10.
  const double tau = centuries / 10.0;
  const PlanetTheory& theory = ephemeris_.earth;
  double lbr[kEphemerisCoords] = {};
  for (std::size_t c = 0; c < kEphemerisCoords; ++c) {
    double power = 1.0;
    for (std::size_t p = 0; p < kEphemerisPowers; ++p) {
      double sum = 0.0;
      for (const EphemerisTerm& term : theory.series[c][p]) {
        sum = std::cos(term.c * tau + term.b) * term.a + sum;
      }
      lbr[c] = sum * power + lbr[c];
      power *= tau;
    }
  }
  Spherical s{reduce(lbr[0]), lbr[1], lbr[2]};
  // The FK5 correction, with tau in place of Meeus' T.
  const double l1 = s.longitude - (tau * 0.00031 + 1.397) * tau * kDegree;
  const double cos_l1 = std::cos(l1);
  const double sin_l1 = std::sin(l1);
  s.longitude = ((cos_l1 + sin_l1) * (std::tan(s.latitude) * 0.03916) + -0.09033) * kArcsecond + s.longitude;
  s.latitude = (cos_l1 - sin_l1) * 0.03916 * kArcsecond + s.latitude;
  return s;
}

Astronomy::Spherical Astronomy::geocentric_sun(double centuries) const {
  const Spherical e = earth(centuries);
  Spherical s;
  difference({}, to_rectangular(e.longitude, e.latitude, e.radius), s.longitude, s.latitude, s.radius);
  return s;
}

void Astronomy::begin_position(const LocalTime& time, const Observer& observer) {
  const double seconds = time.second + 1e-06;
  const double day = ((time.minute / 60.0 + time.hour) + seconds / 3600.0) * kRadiansPerHour / kTwoPi +
                     (time.day + 9.999999999999998e-08);
  longitude_ = degrees_to_radians(observer.longitude);
  latitude_ = degrees_to_radians(observer.latitude);
  time_zone_days_ = observer.time_zone / 24.0;
  centuries_ = (julian_day(time.year, time.month, day) + time_zone_days_ + -2451545.0) / kDaysPerCentury;
  delta_t_ = delta_t_seconds(centuries_);
  obliquity_ = mean_obliquity(centuries_);
  const Nutation n = nutation(centuries_);
  nutation_longitude_ = n.longitude;
  nutation_obliquity_ = n.obliquity;
}

HorizonPosition Astronomy::sun_position(const LocalTime& time, const Observer& observer) {
  // ephem_sun_position (0xc2f44)
  begin_position(time, observer);
  const double dynamical = delta_t_ * kSecondsToCenturies + centuries_;
  const double theta = sidereal_time(dynamical) + std::cos(obliquity_) * nutation_longitude_ - longitude_;
  const ObserverPlace place = observer_place(latitude_, observer.height);
  // The Earth's position is taken at the universal time.
  const Spherical sun = geocentric_sun(centuries_);
  double ra = 0.0;
  double dec = 0.0;
  to_equatorial(sun.longitude + nutation_longitude_, sun.latitude, obliquity_ + nutation_obliquity_, ra, dec);
  aberration(centuries_, obliquity_, ra, dec);
  parallax_approximate(kSunParallax / sun.radius, theta, place, ra, dec);
  return to_horizon(ra, dec, theta, latitude_);
}

HorizonPosition Astronomy::moon_position(const LocalTime& time, const Observer& observer) {
  // ephem_moon_position (0xc355b): the sidereal time at the universal time.
  begin_position(time, observer);
  const double theta = sidereal_time(centuries_) + std::cos(obliquity_) * nutation_longitude_ - longitude_;
  const ObserverPlace place = observer_place(latitude_, observer.height);
  double longitude = 0.0;
  double latitude = 0.0;
  double distance = 0.0;
  moon_geocentric(centuries_, longitude, latitude, distance);
  moon_parallax_ = std::asin(kEarthRadiusKm / distance);
  double ra = 0.0;
  double dec = 0.0;
  to_equatorial(longitude + nutation_longitude_, latitude, obliquity_ + nutation_obliquity_, ra, dec);
  parallax_exact(std::sin(moon_parallax_), theta, place, ra, dec);
  return to_horizon(ra, dec, theta, latitude_);
}

void Astronomy::start_of_day() {
  const double day = std::floor(centuries_ * kDaysPerCentury + -0.5 - time_zone_days_);
  centuries_ = (day + 0.5 + time_zone_days_) / kDaysPerCentury;
}

Astronomy::DayTimes Astronomy::rise_transit_set(double centuries, const Equatorial (&places)[3],
                                                double altitude) const {
  // 0xd4e4d (Meeus, chapter 15).
  const double a2 = places[1].right_ascension;
  const double a1 = reduce_signed(places[0].right_ascension - a2) + a2;
  const double a3 = reduce_signed(places[2].right_ascension - a2) + a2;
  const double d1 = places[0].declination;
  const double d2 = places[1].declination;
  const double d3 = places[2].declination;
  const double theta0 = sidereal_time(centuries);
  const double sin_lat = std::sin(latitude_);
  const double cos_lat = std::cos(latitude_);
  const double cos_h0 = (std::sin(altitude) - sin_lat * std::sin(d2)) / (cos_lat * std::cos(d2));
  double m[3] = {(a2 + longitude_ - theta0) / kTwoPiLiteral, 0.0, 0.0};
  if (m[0] < 0.0) {
    m[0] = m[0] + 1.0;
  }
  DayTimes out;
  if (std::abs(cos_h0) <= 1.0) {
    const double h0 = std::acos(cos_h0) / kTwoPiLiteral;
    m[1] = m[0] - h0;
    m[2] = m[0] + h0;
  } else if (cos_h0 < -1.0) {
    m[1] = m[0] + -0.5;
    m[2] = m[0] + 0.5;
    out.status = 0x10;
  } else {
    m[1] = m[0];
    m[2] = m[0];
    out.status = 0x20;
  }
  if (m[1] < 0.0) {
    m[1] = m[1] + 1.0;
  }
  if (m[2] > 1.0) {
    m[2] = m[2] - 1.0;
  }
  const std::int32_t refined = out.status == 0 ? 3 : 1;
  for (std::int32_t i = 0; i < refined; ++i) {
    double& mi = m[i];
    // The original iterates until the correction is small; the cap only
    // guards against a non-converging (polar) case, where it would hang.
    for (std::int32_t iteration = 0; iteration < 1000; ++iteration) {
      const double theta = theta0 + 6.300388092591991 * mi;
      const double n = delta_t_ / 86400.0 + mi;
      const double ra = interpolate(a1, a2, a3, n);
      const double hour_angle = reduce_signed(theta - longitude_ - ra);
      double dm = 0.0;
      if (i == 0) {
        dm = -hour_angle / kTwoPiLiteral;
      } else {
        const double dec = interpolate(d1, d2, d3, n);
        const double cos_d = std::cos(dec);
        const double h = std::asin(cos_lat * cos_d * std::cos(hour_angle) + sin_lat * std::sin(dec));
        dm = (h - altitude) * std::cos(h) / (cos_d * kTwoPiLiteral * cos_lat * std::sin(hour_angle));
      }
      mi = mi + dm;
      if (!(std::abs(dm) > 0.0002)) {
        break;
      }
    }
  }
  if (out.status == 0) {
    for (std::size_t i = 1; i <= 2; ++i) {
      if (m[i] < 0.0) {
        m[i] = m[i] + 1.0;
      } else if (m[i] > 1.0) {
        m[i] = m[i] - 1.0;
      }
    }
  }
  out.rise = m[1] * kTwoPiLiteral;
  out.set = m[2] * kTwoPiLiteral;
  return out;
}

RiseSetTimes Astronomy::to_times(const DayTimes& day) const {
  RiseSetTimes t;
  if (day.status == 0x10 || day.status == 0x20) {
    const std::int32_t v = day.status == 0x10 ? RiseSetTimes::kNeverSets : RiseSetTimes::kNeverRises;
    t = {v, v, v, v};
    return t;
  }
  to_hours_minutes(day.rise, t.rise_hour, t.rise_minute);
  to_hours_minutes(day.set, t.set_hour, t.set_minute);
  return t;
}

RiseSetTimes Astronomy::sun_rise_set() {
  // ephem_sun_rise_set (0xc32b2): the sun at the day's start and one day
  // either side, without nutation nor aberration.
  start_of_day();
  const double dynamical = kSecondsToCenturies * delta_t_ + centuries_;
  Equatorial places[3];
  const double offsets[3] = {-kDayInCenturies, 0.0, kDayInCenturies};
  for (std::size_t i = 0; i < 3; ++i) {
    const Spherical sun = geocentric_sun(dynamical + offsets[i]);
    to_equatorial(sun.longitude, sun.latitude, obliquity_, places[i].right_ascension, places[i].declination);
  }
  return to_times(rise_transit_set(centuries_, places, kSunRiseAltitude));
}

MoonRiseSet Astronomy::moon_rise_set() {
  // ephem_moon_rise_set (0xc3891): first the illuminated fraction now
  // (0xd6f1d, from the Earth's centre, the moon's longitude with the
  // nutation).
  MoonRiseSet out;
  {
    const Spherical earth_now = earth(centuries_);
    const double sun_longitude = reduce(earth_now.longitude + kPi);
    double longitude = 0.0;
    double latitude = 0.0;
    double distance = 0.0;
    moon_geocentric(centuries_, longitude, latitude, distance);
    longitude += nutation_longitude_;
    const double psi = std::acos(std::cos(longitude - sun_longitude) * std::cos(latitude));
    double i = std::atan(std::sin(psi) * earth_now.radius /
                         (distance / kAstronomicalUnitKm - std::cos(psi) * earth_now.radius));
    if (i < 0.0) {
      i += kPi;
    }
    out.illuminated = (std::cos(i) + 1.0) * 0.5;
  }
  start_of_day();
  const double dynamical = delta_t_ * kSecondsToCenturies + centuries_;
  Equatorial places[3];
  const double offsets[3] = {-kDayInCenturies, 0.0, kDayInCenturies};
  for (std::size_t i = 0; i < 3; ++i) {
    double longitude = 0.0;
    double latitude = 0.0;
    double distance = 0.0;
    moon_geocentric(dynamical + offsets[i], longitude, latitude, distance);
    to_equatorial(longitude, latitude, obliquity_, places[i].right_ascension, places[i].declination);
  }
  // 0xd5262: the moon's altitude at rising from the last position's parallax.
  const double altitude = moon_parallax_ * 0.7275 + -0.009890780871051864;
  out.times = to_times(rise_transit_set(centuries_, places, altitude));
  return out;
}

}  // namespace sh
