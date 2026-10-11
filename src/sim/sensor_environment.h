// What the sensors and the AI read besides the scenario conditions: the
// sky's light and fog, the chart terrain grid, the player's crew and
// targets, the station on screen, and the hooks into the weapons and the
// strategic contact groups (see sensors.h).
#pragma once

#include <cstdint>

#include "sim/context.h"
#include "sim/line_of_sight.h"
#include "sim/sighting.h"

namespace sh {

class ShipWeaponsHook;
class ContactGroupHook;
class Vehicle;

// The bits of the options word `0x11a85c` the sensors read.
namespace sensor_option {
inline constexpr std::uint32_t kContactGroups = 0x1;     // Bit 0: the strategic contact system.
inline constexpr std::uint32_t kRealismLevelMask = 0xc;  // Bits 2-3.
inline constexpr std::uint32_t kLimitedVisibility = 0x80;
inline constexpr std::uint32_t kSjRadarMask = 0x30000;   // 1 SJ, 2 improved SJ, 3 ST.
inline constexpr std::uint32_t kStRadar = 0x30000;
inline constexpr std::uint32_t kSdRadar = 0x40000;
inline constexpr std::uint32_t kPpiScope = 0x80000;
// Bit 20: the APR-1 radar detector (equipment bit 4; read only by the APR-1
// rule).
inline constexpr std::uint32_t kApr1Detector = 0x100000;
// Bit 22, not in the original: the ST periscope radar, set by the mission
// when the ST rule is on and the boat has it (sim/radar.h).
inline constexpr std::uint32_t kStPeriscopeRadar = 0x400000;
}  // namespace sensor_option

// The station on screen, as far as the sensors care (`0x11a860`).
enum class SensorStation : std::int32_t {
  kOther = 0,
  kChart = 1,  // The ships keep their sonar beam for the chart.
  kRadar = 2,  // The SJ shows land echoes.
};

struct SensorEnvironment {
  const TerrainGrid* grid = nullptr;     // Null: no land anywhere.
  std::int32_t light_pct = 100;          // 0x170f84, from the sun and moon.
  std::int32_t fog_pct = 0;              // 0x170f88
  std::int32_t crew = 1;                 // 0x11ac2b, the player's crew, 0..2.
  SensorStation station = SensorStation::kOther;
  bool auto_sj = false;                  // 0x11a8c8, "Auto SJ Radar".
  bool auto_sd = false;                  // 0x11a8cc
  const Vehicle* tdc_target = nullptr;   // 0x129a20
  const Vehicle* gun_target = nullptr;   // 0x11c1c4
  ShipWeaponsHook* weapons = nullptr;
  ContactGroupHook* contact_groups = nullptr;

  // Returns the lookouts' conditions under `env`'s weather.
  SightingConditions sighting(const MissionConditions& env) const { return {env.weather, fog_pct, light_pct}; }
};

}  // namespace sh
