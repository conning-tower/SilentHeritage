// Strong types for the original game's integer representations.
#pragma once

#include <compare>
#include <cstdint>

namespace sh {

// A 16.16 signed fixed-point number, as used by the original (Watcom) code.
// Its operators are the plain sequences: `*` is `imul; shrd 16` (rounded
// toward negative infinity, `fx::mul_floor`) and `/` an unguarded 64-bit
// `idiv` (truncated toward zero, b must be non-zero). The original's usual
// rounded product and saturating quotient are `fx::mul` and `fx::div`
// (core/fixed_math.h), on `raw()` values.
class Fixed16 {
 public:
  constexpr Fixed16() = default;

  static constexpr Fixed16 from_raw(std::int32_t raw) { return Fixed16(raw); }
  static constexpr Fixed16 from_int(std::int32_t v) { return Fixed16(static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 16)); }
  // Converts `v`, truncating toward zero (as `fistp` with truncation would not:
  // only use this for constants and tests, not to emulate x87 code).
  static constexpr Fixed16 from_double(double v) { return Fixed16(static_cast<std::int32_t>(v * 65536.0)); }

  constexpr std::int32_t raw() const { return raw_; }
  // Returns the integer part, rounded toward negative infinity (`sar 16`).
  constexpr std::int32_t floor_int() const { return raw_ >> 16; }
  constexpr double to_double() const { return raw_ / 65536.0; }

  constexpr Fixed16 operator-() const { return Fixed16(-raw_); }
  constexpr Fixed16 operator+(Fixed16 o) const { return Fixed16(raw_ + o.raw_); }
  constexpr Fixed16 operator-(Fixed16 o) const { return Fixed16(raw_ - o.raw_); }
  constexpr Fixed16 operator*(Fixed16 o) const {
    return Fixed16(static_cast<std::int32_t>((static_cast<std::int64_t>(raw_) * o.raw_) >> 16));
  }
  constexpr Fixed16 operator/(Fixed16 o) const {
    return Fixed16(static_cast<std::int32_t>((static_cast<std::int64_t>(raw_) << 16) / o.raw_));
  }
  constexpr Fixed16& operator+=(Fixed16 o) { raw_ += o.raw_; return *this; }
  constexpr Fixed16& operator-=(Fixed16 o) { raw_ -= o.raw_; return *this; }

  constexpr auto operator<=>(const Fixed16&) const = default;

 private:
  constexpr explicit Fixed16(std::int32_t raw) : raw_(raw) {}
  std::int32_t raw_ = 0;
};

// An angle in tenths of a degree (0..3599 when normalized), the unit of the
// original's headings and bearings.
class Angle10 {
 public:
  constexpr Angle10() = default;
  constexpr explicit Angle10(std::int32_t tenths) : tenths_(tenths) {}

  constexpr std::int32_t tenths() const { return tenths_; }
  constexpr double degrees() const { return tenths_ / 10.0; }

  // Returns the angle folded into [0, 3600).
  constexpr Angle10 normalized() const {
    std::int32_t t = tenths_ % 3600;
    return Angle10(t < 0 ? t + 3600 : t);
  }

  constexpr auto operator<=>(const Angle10&) const = default;

 private:
  std::int32_t tenths_ = 0;
};

// World distances: 16 world units = 1 foot, 48 (0x30) = 1 yard.
inline constexpr std::int32_t kUnitsPerFoot = 16;
inline constexpr std::int32_t kUnitsPerYard = 48;

}  // namespace sh
