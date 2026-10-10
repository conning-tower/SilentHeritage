// The original's fixed-point math helpers, shared by the simulation, the 3D
// view and the sprite blitter, with the exact rounding, wrapping and
// saturation of its code (inline `imul`/`idiv` sequences and the small
// routines at 0x5447c..0x55c4a and 0xae3fb..0xae4d9 of SH.EXE). Values are
// raw `int32` 16.16 numbers (65536 = 1, see `Fixed16` for the strong type)
// and angles in tenths of a degree (`Angle10`).
//
// World axes: x east, y up, z north; heading 0 is north (+z) and grows
// clockwise (90 deg = +x).
#pragma once

#include <array>
#include <cstdint>

#include "core/fixed.h"

namespace sh::fx {

inline constexpr std::int32_t kOne = 0x10000;

// Returns a + b wrapped to 32 bits (a plain `add`).
constexpr std::int32_t wadd(std::int32_t a, std::int32_t b) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

// Returns a - b wrapped to 32 bits (a plain `sub`).
constexpr std::int32_t wsub(std::int32_t a, std::int32_t b) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

// Returns a * b wrapped to 32 bits (a plain 32-bit `imul`).
constexpr std::int32_t imul(std::int32_t a, std::int32_t b) {
  return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// Returns a * b / 65536 rounded half up, wrapped to 32 bits (the `imul;
// add 0x8000; adc; shrd 16` sequence of most 16.16 products).
constexpr std::int32_t mul(std::int32_t a, std::int32_t b) {
  return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b + 0x8000) >> 16);
}

// Returns a * b / 65536 rounded toward negative infinity, wrapped to 32 bits
// (`imul; shrd 16` without the rounding, as the rasterizers'; this is also
// `Fixed16::operator*`).
constexpr std::int32_t mul_floor(std::int32_t a, std::int32_t b) {
  return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * b) >> 16);
}

// Returns a * 65536 / b truncated toward zero, saturated to INT32_MAX/MIN
// (by the sign of a ^ b) when |a >> 16| would overflow, as the original's
// guarded `idiv`; returns 0 when b is 0.
constexpr std::int32_t div(std::int32_t a, std::int32_t b) {
  const std::uint32_t abs_b = b < 0 ? 0u - static_cast<std::uint32_t>(b) : static_cast<std::uint32_t>(b);
  if (abs_b == 0) {
    return 0;
  }
  const std::int32_t hi = a >> 16;
  const std::uint32_t abs_hi = hi < 0 ? 0u - static_cast<std::uint32_t>(hi) : static_cast<std::uint32_t>(hi);
  if ((abs_hi + 1) * 2 <= abs_b) {
    return static_cast<std::int32_t>((static_cast<std::int64_t>(a) * 65536) / b);
  }
  return (a ^ b) < 0 ? INT32_MIN : INT32_MAX;
}

// Returns the 64-bit `a` divided by `d` (`int64_div_int`, 0x548bb): a zero
// divisor counts as 1, and the quotient saturates by the sign of the high
// word ^ d when that word is non-zero and |d| < 2 |high| + 2 (so small
// negative numerators saturate too, like the original).
std::int32_t int64_div(std::int64_t a, std::int32_t d);

// Returns the integer part of a 16.16 value, truncated toward zero (the
// `neg; sar 16; neg` sequence).
constexpr std::int32_t trunc(std::int32_t a) {
  return a < 0 ? static_cast<std::int32_t>(-(-static_cast<std::int64_t>(a) >> 16)) : a >> 16;
}

// Returns |a| (wrapping for INT32_MIN like the C library `abs`).
constexpr std::int32_t abs(std::int32_t a) {
  return a < 0 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(a)) : a;
}

// Returns -1, 0 or 1 (the original's `x / abs(x)`).
constexpr std::int32_t sign(std::int32_t a) { return a < 0 ? -1 : (a > 0 ? 1 : 0); }

// Returns a + b with 3600 subtracted while the sum is 3600 or more
// (`angle_add`, 0x5447c); a negative sum is returned as is.
Angle10 angle_add(Angle10 a, Angle10 b);

// Returns a - b with 3600 added while the difference is negative
// (`angle_sub`, 0x5448d); a difference of 3600 or more is returned as is.
Angle10 angle_sub(Angle10 a, Angle10 b);

// Returns a - b as the signed difference in [-1799, 1800] for a and b in
// [0, 3600), the convention of the original's steering code (`angle_sub`,
// minus 3600 above 1800).
std::int32_t angle_diff(Angle10 a, Angle10 b);

// Cosine and sine as 16.16 values (65536 = 1).
struct SinCos {
  std::int32_t cos = 0;
  std::int32_t sin = 0;
};

// Returns the 16.16 cosine and sine of `angle` from the quarter-wave table
// (`angle_sincos`, 0xae3fb). Any angle is accepted; it is folded into
// [0, 3600] first.
SinCos angle_sincos(Angle10 angle);

// Returns the heading of the vector (dx, dz), 0..3599 tenths of a degree
// (`heading_from_delta`, 0x5449a: octant + arctangent table). Quirks kept:
// (0, 0) gives 450, and a heading just west of north gives 3599.
Angle10 heading_from_delta(std::int32_t dx, std::int32_t dz);

// Returns the approximate length of (a, b) (`approx_distance`, 0x54537):
// max + max * table[min / max], within 0.01 % of the true hypotenuse.
std::uint32_t approx_distance(std::int32_t a, std::int32_t b);

// Returns approx_distance(approx_distance(dx, dy), dz) (`approx_distance_3d`,
// 0x54589).
std::uint32_t approx_distance_3d(std::int32_t dx, std::int32_t dy, std::int32_t dz);

// Returns asin(x) of a 16.16 value in tenths of a degree (sign kept), 0
// outside [-1, 1] (`fixed_asin`, 0x545b0: a 4097-entry table rounded to
// tenths).
std::int32_t fixed_asin(std::int32_t x);

// An integer 2D point (screen pixels or world units).
struct Point2 {
  std::int32_t x = 0;
  std::int32_t y = 0;
  constexpr bool operator==(const Point2&) const = default;
};

// Returns `src` rotated by `angle` around `centre` after scaling its offset
// by the 16.16 factors `sx`/`sy` (`point_rotate_scale`, 0xae4d9): clockwise
// on screen (y down).
Point2 point_rotate_scale(Point2 src, Point2 centre, Angle10 angle, std::int32_t sx = kOne, std::int32_t sy = kOne);

// A vector of three raw values (16.16 or world units, by context).
struct Vec3 {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  // Component-wise, wrapping like the original's 32-bit arithmetic
  // (`vec3_add`, `vec3_sub`, `vec3_negate`).
  constexpr Vec3 operator+(const Vec3& o) const { return {wadd(x, o.x), wadd(y, o.y), wadd(z, o.z)}; }
  constexpr Vec3 operator-(const Vec3& o) const { return {wsub(x, o.x), wsub(y, o.y), wsub(z, o.z)}; }
  constexpr Vec3 operator-() const { return {wsub(0, x), wsub(0, y), wsub(0, z)}; }
  constexpr bool operator==(const Vec3&) const = default;
};

// Returns v * s per component, rounded (`vec3_scale`, 0x54dd0).
constexpr Vec3 scale(const Vec3& v, std::int32_t s) { return {mul(v.x, s), mul(v.y, s), mul(v.z, s)}; }

// Returns v / s per component, saturated (`vec3_div`, 0x5514b).
constexpr Vec3 divide(const Vec3& v, std::int32_t s) { return {div(v.x, s), div(v.y, s), div(v.z, s)}; }

// Returns the rounded dot product, wrapping (`vec3_dot`, 0x550d8).
constexpr std::int32_t dot(const Vec3& a, const Vec3& b) {
  return wadd(wadd(mul(a.z, b.z), mul(a.x, b.x)), mul(a.y, b.y));
}

// Returns a x b with rounded products (`vec3_cross`, 0x54ea2).
constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
  return {wsub(mul(a.y, b.z), mul(a.z, b.y)), wsub(mul(a.z, b.x), mul(a.x, b.z)), wsub(mul(a.x, b.y), mul(a.y, b.x))};
}

// Returns v / |v| with the approximate length and saturated divisions
// (`vec3_normalize`, 0x55431).
Vec3 normalize(const Vec3& v);

// A 3x3 matrix of 16.16 values, row-major (`m[row * 3 + col]`).
struct Mat3 {
  std::array<std::int32_t, 9> m{kOne, 0, 0, 0, kOne, 0, 0, 0, kOne};

  constexpr Vec3 row(int r) const { return {m[r * 3], m[r * 3 + 1], m[r * 3 + 2]}; }
  constexpr bool operator==(const Mat3&) const = default;
};

// Returns m * v (`mat3_mul_vec3`, 0x54f98).
constexpr Vec3 transform(const Mat3& m, const Vec3& v) {
  return {dot(v, m.row(0)), dot(v, m.row(1)), dot(v, m.row(2))};
}

// Returns the transpose of m (`mat3_transpose`, 0x55a39), the inverse of a
// rotation.
constexpr Mat3 transpose(const Mat3& m) {
  Mat3 t;
  for (std::size_t r = 0; r < 3; ++r) {
    for (std::size_t c = 0; c < 3; ++c) {
      t.m[c * 3 + r] = m.m[r * 3 + c];
    }
  }
  return t;
}

// Returns the orientation matrix of the Euler angles in tenths of a degree
// (`mat3_from_euler`, 0x55a92). Vehicles pass their heading negated.
Mat3 mat3_from_euler(Angle10 heading, Angle10 pitch, Angle10 roll);

// The lookup tables of the helpers above. They are the original's own, read
// from the player's SH.EXE at startup (`load_game_exe`, assets/game_exe.h):
// the helpers that use them throw `std::logic_error` until they are
// installed.
struct Tables {
  // 0xad5e7: round(65536 cos(i / 10 deg)) for i = 0..900; the sine is the
  // same table mirrored (index 900 - i).
  std::array<std::int32_t, 901> cos{};
  // 0x10b5ca: floor(10 atan(i / 256) in degrees) for i = 0..256.
  std::array<std::int16_t, 257> atan_tenths{};
  // 0x10b7cc: floor(65536 (sqrt(1 + (i / 256)^2) - 1)) for i = 0..256
  // (entry 192 is one below the exact 16384).
  std::array<std::uint16_t, 257> hypot_excess{};
  // 0x1095c8: asin(i / 4096) in tenths of a degree, rounded, i = 0..4096.
  std::array<std::uint16_t, 4097> asin_tenths{};
};

// Installs the tables of the helpers (a copy of `t`), replacing any earlier
// ones. Not thread-safe: called once at startup, before any helper runs.
void install_tables(const Tables& t);

// Returns whether `install_tables` has run.
bool tables_installed();

}  // namespace sh::fx
