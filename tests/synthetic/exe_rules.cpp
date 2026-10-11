#include "synthetic/exe_rules.h"

#include <array>
#include <format>
#include <initializer_list>
#include <stdexcept>

#include "core/fixed_math.h"
#include "formula_tables.h"

namespace sh::test {

namespace {

template <typename T, std::size_t N>
void put_array(std::span<std::uint8_t> out, const std::array<T, N>& values) {
  if (out.size() != N * sizeof(T)) {
    throw std::runtime_error(std::format("exe rule: {} bytes for a table of {}", out.size(), N * sizeof(T)));
  }
  for (std::size_t i = 0; i < N; ++i) {
    const auto v = static_cast<std::uint32_t>(values[i]);
    for (std::size_t k = 0; k < sizeof(T); ++k) {
      out[i * sizeof(T) + k] = static_cast<std::uint8_t>(v >> (8 * k));
    }
  }
}

std::vector<ExeManifest::Rule> exe_rules_list(std::string_view exe) {
  if (exe != "sh_exe") {
    return {};
  }
  const fx::Tables t;
  const auto ints = [](std::uint32_t n) { return 4 * n; };
  return {
      // The fixed-point math tables.
      {0xad5e7, static_cast<std::uint32_t>(t.cos.size() * 4), "cos"},
      {0x10b5ca, static_cast<std::uint32_t>(t.atan_tenths.size() * 2), "atan"},
      {0x10b7cc, static_cast<std::uint32_t>(t.hypot_excess.size() * 2), "hypot"},
      {0x1095c8, static_cast<std::uint32_t>(t.asin_tenths.size() * 2), "asin"},
      // The gameplay tables (their readers in the comments).
      {0xd305b, ints(187), "delta_t"},            // sim/astronomy.cpp
      {0x102fd8, ints(16), "depth_charges"},      // sim/ship_weapons.cpp
      {0x10304c, ints(8), "zigzag"},              // sim/ai_ship.cpp
      {0x1037e8, ints(8), "warship_types"},       // game/patrol_contacts.cpp
      {0x10cb80, ints(15), "helm"},               // sim/player_sub.cpp
      {0x10ce50, ints(4), "convoy_sizes"},        // game/scengen.cpp
      {0x10ce60, ints(5), "escort_sizes"},
      {0x10ce74, ints(4), "patrol_sizes"},
      {0x10ce84, ints(4), "anchored_sizes"},
      {0x10cea4, ints(8), "ring_bearings"},
      {0x10cec4, ints(6), "patrol_sectors"},
      {0x10cedc, ints(3), "guard_bearings"},
      {0x10ceec, ints(12), "task_force_tiers"},
      {0x10d6d4, ints(10), "screen_shake"},       // game/screen_shake.cpp
      {0x10e3c0, ints(18), "torpedo_fixes"},      // sim/torpedo.cpp
      {0x10e408, ints(18), "equipment_dates"},    // game/scengen.cpp
      {0x1108bc, ints(12), "sea_states"},         // sim/weather.cpp
  };
}

// Writes `values` as int32s over `out`, whose size they must fill.
void put_ints(std::span<std::uint8_t> out, std::initializer_list<std::int32_t> values) {
  if (out.size() != 4 * values.size()) {
    throw std::runtime_error(std::format("exe rule: {} bytes for {} ints", out.size(), values.size()));
  }
  std::size_t i = 0;
  for (const std::int32_t v : values) {
    for (std::size_t k = 0; k < 4; ++k) {
      out[i++] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * k));
    }
  }
}

}  // namespace

std::vector<ExeManifest::Rule> exe_rules(std::string_view exe) { return exe_rules_list(exe); }

void fill_exe_rule(std::string_view name, std::span<std::uint8_t> out) {
  static const fx::Tables tables = formula_tables();
  if (name == "delta_t") {
    // A smooth fall from the first value the code checks to the last.
    std::array<std::int32_t, 187> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
      const double x = static_cast<double>(i) / (t.size() - 1);
      t[i] = static_cast<std::int32_t>(1240 - 660 * x * (2 - x));
    }
    put_array(out, t);
    return;
  }
  if (name == "helm") {
    std::array<std::int32_t, 15> t{};
    for (std::size_t i = 0; i < t.size(); ++i) {
      t[i] = 5 * (static_cast<std::int32_t>(i) - 7);
    }
    put_array(out, t);
    return;
  }
  if (name == "depth_charges") {  // four rack offsets (ft), twelve thrower angles (deg)
    put_ints(out, {-24, -8, 8, 24, 60, -60, 90, -90, 120, -120, 45, -45, 75, -75, 105, -105});
  } else if (name == "zigzag") {  // four legs: minutes, degrees
    put_ints(out, {4, 30, 3, -45, 5, 25, 3, -20});
  } else if (name == "warship_types") {  // SHIPS.DAT type ranges: battleships, carriers, heavy, light cruisers
    put_ints(out, {3, 4, 0, 2, 5, 5, 6, 6});
  } else if (name == "convoy_sizes") {
    put_ints(out, {3, 5, 8, 12});
  } else if (name == "escort_sizes") {
    put_ints(out, {0, 1, 2, 4, 6});
  } else if (name == "patrol_sizes") {
    put_ints(out, {1, 2, 3, 5});
  } else if (name == "anchored_sizes") {
    put_ints(out, {1, 3, 5, 8});
  } else if (name == "ring_bearings") {
    put_ints(out, {0, 45, 90, 135, 180, 225, 270, 315});
  } else if (name == "patrol_sectors") {
    put_ints(out, {0, 60, 120, 180, 240, 300});
  } else if (name == "guard_bearings") {
    put_ints(out, {30, 150, 270});
  } else if (name == "task_force_tiers") {  // {lo, hi, next}: battleships, carriers, cruisers
    put_ints(out, {3, 4, 1, 0, 2, 2, 5, 5, 3, 6, 6, 3});
  } else if (name == "screen_shake") {
    put_ints(out, {4, 0, 6, 1, 5, 0, 3, 0, 1, 0});
  } else if (name == "torpedo_fixes") {  // {month, day, year since 1930}: deep running, four exploders, contact
    put_ints(out, {7, 1, 12, 9, 15, 13, 10, 1, 13, 11, 1, 13, 11, 15, 13, 9, 30, 13});
  } else if (name == "equipment_dates") {  // {month 0-based, day, year since 1930}
    put_ints(out, {1, 1, 12, 8, 1, 12, 8, 1, 13, 6, 1, 13, 5, 1, 14, 3, 1, 13});
  } else if (name == "sea_states") {  // per weather: calm, moderate, heavy (%)
    put_ints(out, {70, 25, 5, 50, 35, 15, 30, 45, 25, 15, 45, 40});
  } else if (name == "cos") {
    put_array(out, tables.cos);
  } else if (name == "atan") {
    put_array(out, tables.atan_tenths);
  } else if (name == "hypot") {
    put_array(out, tables.hypot_excess);
  } else if (name == "asin") {
    put_array(out, tables.asin_tenths);
  } else {
    throw std::runtime_error(std::format("unknown exe rule {}", name));
  }
}

}  // namespace sh::test
