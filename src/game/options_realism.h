// The realism settings of the options word `0x11a85c` as the REALISM dialog
// (0x1879c0) edits them: the presets, the toggles and the realism rating.
#pragma once

#include <cstdint>

#include "game/saves_options.h"

namespace sh {

// The realism presets (`realism_set_novice` 0xa2cde, `_intermediate`
// 0xa2d07, `_expert` 0xa2d2f): they replace the low 16 bits of the options
// word, keeping the mode bits 0-1.
enum class RealismPreset : std::uint8_t { kNovice, kIntermediate, kExpert };
std::uint32_t apply_realism_preset(std::uint32_t options, RealismPreset preset);

// One check box of the REALISM dialog: its options bit and whether the box
// is checked when the bit is set (unlimited fuel shows as LIMITED FUEL
// unchecked...).
struct RealismToggle {
  std::uint32_t bit;
  bool checked_when_set;

  bool checked(std::uint32_t options) const { return ((options & bit) != 0) == checked_when_set; }
};

// The check boxes in dialog order (`realism_toggle_*`, 0xa298e..0xa2b94):
// LIMITED FUEL, LIMITED BATTERY, LIMITED AMMO, LIMITED VISIBILITY, DUD
// TORPEDOES, REALISTIC RELOADS, LIMITED DEPTH DATA, VULNERABLE BOAT,
// REALISTIC CHARTS, RUN AGROUND.
inline constexpr RealismToggle kRealismToggles[10] = {
    {option::kUnlimitedFuel, false},     {option::kUnlimitedBattery, false},   {option::kUnlimitedAmmo, false},
    {option::kLimitedVisibility, true},  {option::kDudTorpedoes, true},        {option::kRealisticReloads, true},
    {option::kLimitedDepthDataOff, false}, {option::kInvulnerable, false},     {option::kRealisticChartsOff, false},
    {option::kRunAground, true},
};

// Returns the combat level (0..3) and sailing model (0..2).
inline std::int32_t combat_level(std::uint32_t options) { return static_cast<std::int32_t>((options & 0xf) >> 2); }
inline std::int32_t sailing_model(std::uint32_t options) { return static_cast<std::int32_t>((options & 0x3f) >> 4); }
// `realism_cycle_combat_level` (0xa2bce) / `realism_cycle_sailing_model`
// (0xa2c17): the next level, wrapping after 3 / 2.
std::uint32_t cycle_combat_level(std::uint32_t options);
std::uint32_t cycle_sailing_model(std::uint32_t options);

// The factors of the extended variant's REALISM items (not in the
// original): UNDEAFENED ESCORTS on 1.10, DETECTABLE RADAR off 0.95 (on, as
// at EXPERT, it keeps the rating).
inline constexpr std::int32_t kUndeafenedEscortsFactor = 0x1199a;
inline constexpr std::int32_t kNoDetectableRadarFactor = 0xf333;

// `realism_rating` (0xa2d57): the realism rating of `options` in 16.16
// (1.15 at most), computed with the original's rounded fixed-point
// products. With `rules.extended_realism` (not in the original), the
// extended items' factors follow.
std::int32_t realism_rating(std::uint32_t options, const MissionRules& rules = {});

// The extended items of a REALISM preset (not in the original): DETECTABLE
// RADAR on from INTERMEDIATE, UNDEAFENED ESCORTS off.
MissionRules apply_realism_preset_rules(MissionRules rules, RealismPreset preset);
// The rounded percentage `realism_rating_text` (0xa2c60) shows, e.g. 115.
std::int32_t realism_rating_percent(std::int32_t rating);

}  // namespace sh
