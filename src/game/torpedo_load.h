// The own boat's torpedo load at the start of a mission: the tube contents
// and reload racks a scenario header gives (`init_apply_scenario_header`,
// 0x5044a), the Mark 18 upgrade by date and the TDC's torpedo type table
// (`tdc_upgrade_mark18_by_date`, 0x7b149).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "assets/dat_tables.h"
#include "assets/scenario.h"

namespace sh {

// Torpedoes as WEAPONS.DAT ids (`Torpedo::id`), 0 for none.
struct TorpedoLoad {
  static constexpr std::size_t kTubes = 10;
  static constexpr std::size_t kRackCapacity = 20;

  std::array<std::int32_t, kTubes> tubes{};              // 0x11ac98: bow tubes first, then stern
  std::array<std::int32_t, kRackCapacity> bow_racks{};   // 0x11ad60
  std::array<std::int32_t, kRackCapacity> stern_racks{}; // 0x11adb0
};

// Returns the load of a scenario header: its tube contents, and each reload
// entry (until type -1) expanded into its forward count of bow-rack and aft
// count of stern-rack entries, filled in list order. Throws `DataError` when
// a rack overflows (the original writes past it).
TorpedoLoad scenario_torpedo_load(const ScenarioHeader& header);

// One entry of the TDC's torpedo type table (0x10f550).
struct TorpedoTypeChoice {
  std::string_view name;  // "MARK10", "MARK14", "MARK18-1" or "MARK18-2"
  std::int32_t id = 0;
  bool available = false;
};

// The TDC's torpedo type table.
struct TorpedoTypes {
  std::array<TorpedoTypeChoice, 3> types;
  std::int32_t selected = 0;  // 0x129a80, the TDC's torpedo type
  bool single_type = false;   // 0x12971c: only one type is available
};

// Replaces the MARK18-1 torpedoes by MARK18-2 in the tubes and the racks
// once the MARK18-2 is in service at `year` (since 1930) / `month`
// (0-based), then builds the type table: a type is available when the boat
// carries it or, by its service date, on boats with modern torpedoes (the
// Mark 10 on every boat). The selected type is the last available one, then
// that of the first loaded tube. Throws `DataError` if a Mark torpedo is
// missing from WEAPONS.DAT.
TorpedoTypes tdc_upgrade_mark18_by_date(TorpedoLoad& load, const SubClass& sub, const GameTables& tables,
                                        std::int32_t year, std::int32_t month);

}  // namespace sh
