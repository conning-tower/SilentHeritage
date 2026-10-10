// Small arithmetic helpers of the weapons code: the knot conversions and
// roundings it computes inline.
#pragma once

#include <cstdint>

#include "core/fixed_math.h"

namespace sh {

// Returns the 16.16 feet per second in one knot, div(6077, 3600) as the
// TDC computes it inline (raw 0x1b024).
inline constexpr std::int32_t kFeetPerSecondPerKnot = fx::div(6077, 3600);

// Returns the 16.16 world units per second in one knot, div(0x17bd0, 3600)
// (raw 0x1b0246).
inline constexpr std::int32_t kUnitsPerSecondPerKnot = fx::div(0x17bd0, 3600);

// Returns a 16.16 value rounded the TDC way: frac / 2 is added before
// truncating, so fractions of 2/3 and more round up.
constexpr std::int32_t tdc_round(std::int32_t v) { return fx::trunc(v + static_cast<std::int32_t>(v & 0xffff) / 2); }

// Returns the volume 0x7f - d x 0x7f / range of a sound heard at `d`
// (yards or feet, as the caller's range).
constexpr std::int32_t distance_volume(std::int32_t d, std::int32_t range) { return 0x7f - d * 0x7f / range; }

}  // namespace sh
