#include "game/options_realism.h"

#include <array>

#include "core/fixed_math.h"

namespace sh {

std::uint32_t apply_realism_preset(std::uint32_t options, RealismPreset preset) {
  switch (preset) {
    case RealismPreset::kNovice:
      return (options & ~0xffffu) | (options & 0x1e43u) | 0x1e40u;
    case RealismPreset::kIntermediate:
      return (options & ~0xffffu) | (options & option::kModeMask) | 0x11d4u;
    case RealismPreset::kExpert:
      return (options & ~0xffffu) | (options & option::kModeMask) | 0x61a8u;
  }
  return options;
}

std::uint32_t cycle_combat_level(std::uint32_t options) {
  const std::uint32_t level = ((options & 0xf) >> 2) + 1 & 3;
  return (options & ~option::kCombatLevelMask) | level << 2;
}

std::uint32_t cycle_sailing_model(std::uint32_t options) {
  std::uint32_t model = ((options & 0x3f) >> 4) + 1 & 3;
  if (model > 2) {
    model = 0;
  }
  return (options & ~option::kSailingModelMask) | model << 4;
}

std::int32_t realism_rating(std::uint32_t options, bool keen_escort_sonar) {
  // The factors of the manual (pp. 6-9) as 16.16 constants.
  std::int32_t r = 0x12666 - (3 - combat_level(options)) * 0x2666;
  r -= fx::mul(r, (2 - sailing_model(options)) * 0x199a);
  struct Factor {
    std::uint32_t bit;
    bool when_set;
    std::int32_t factor;
  };
  constexpr std::array<Factor, 10> kFactors{{
      {option::kInvulnerable, true, 0x3333},        // 0.20
      {option::kUnlimitedFuel, true, 0xe666},       // 0.90
      {option::kUnlimitedBattery, true, 0xb333},    // 0.70
      {option::kUnlimitedAmmo, true, 0x8000},       // 0.50
      {option::kLimitedVisibility, false, 0xb333},  // 0.70
      {option::kDudTorpedoes, false, 0xd99a},       // 0.85
      {option::kRealisticReloads, false, 0xe666},   // 0.90
      {option::kLimitedDepthDataOff, true, 0xf333},  // 0.95
      {option::kRealisticChartsOff, true, 0xe666},  // 0.90
      {option::kRunAground, false, 0xf333},         // 0.95
  }};
  for (const Factor& f : kFactors) {
    if (((options & f.bit) != 0) == f.when_set) {
      r = fx::mul(r, f.factor);
    }
  }
  if (keen_escort_sonar) {
    r = fx::mul(r, kKeenEscortSonarFactor);
  }
  return r;
}

std::int32_t realism_rating_percent(std::int32_t rating) {
  const std::int32_t hundred = fx::imul(rating, 100);
  return hundred < 0 ? -((-hundred + 0x8000) >> 16) : (hundred + 0x8000) >> 16;
}

}  // namespace sh
