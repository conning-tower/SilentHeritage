// The fixed-point math tables computed from their defining formulas, for
// the test runs without SH_DATA (tests/game_exe_setup.cpp).
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "core/fixed_math.h"

namespace sh::test {

// Returns tables equal to SH.EXE's (checked by "[fixedmath]").
inline fx::Tables formula_tables() {
  fx::Tables t;
  const double pi = std::numbers::pi;
  for (std::size_t i = 0; i < t.cos.size(); ++i) {
    t.cos[i] = static_cast<std::int32_t>(std::lround(65536.0 * std::cos(static_cast<double>(i) * pi / 1800.0)));
  }
  for (std::size_t i = 0; i < t.atan_tenths.size(); ++i) {
    t.atan_tenths[i] = static_cast<std::int16_t>(std::floor(std::atan(static_cast<double>(i) / 256.0) * 1800.0 / pi));
  }
  for (std::size_t i = 0; i < t.hypot_excess.size(); ++i) {
    const double r = static_cast<double>(i) / 256.0;
    t.hypot_excess[i] = static_cast<std::uint16_t>(std::floor(65536.0 * (std::sqrt(1.0 + r * r) - 1.0)));
  }
  t.hypot_excess[192] = 16383;  // The original's entry, one below the exact value.
  for (std::size_t i = 0; i < t.asin_tenths.size(); ++i) {
    t.asin_tenths[i] =
        static_cast<std::uint16_t>(std::lround(std::asin(static_cast<double>(i) / 4096.0) * 1800.0 / pi));
  }
  return t;
}

}  // namespace sh::test
