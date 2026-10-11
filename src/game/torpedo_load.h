// The own boat's torpedo load at the start of a mission: the tube contents
// and reload racks a scenario header gives (`init_apply_scenario_header`,
// 0x5044a), the Mark 18 upgrade by date and the TDC's torpedo type table
// (`tdc_upgrade_mark18_by_date`, 0x7b149).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/scenario.h"
#include "sim/context.h"

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
  std::string_view name;  // "MARK10", "MARK14", "MARK18-1", "MARK18-2" or "MARK27"
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
//
// Under the torpedo rules (not in the original): with `rules.mixed_mark18`
// the replacement is gradual: in the first `kMark18Changeover` months from
// the 18-2's date every other 18-1 of each group (the tubes, then each
// rack, in order) becomes an 18-2, after that all of them. With it or
// `rules.mark27` (whose Mark 27 the table must offer) a boat carries at most
// `kMaxTorpedoTypes` types: while it carries more, its oldest straight
// runner (Mark 10, then 14, then 18-1) becomes its newest one, so the
// newest torpedoes and the Mark 27 stay. The table holds the types carried
// in id order, each available, then straight runners not carried
// (unavailable); the Mark 10 is available when nothing is carried.
TorpedoTypes tdc_upgrade_mark18_by_date(TorpedoLoad& load, const SubClass& sub, const GameTables& tables,
                                        std::int32_t year, std::int32_t month, const MissionRules& rules = {});

// The months of the gradual Mark 18-2 changeover (`rules.mixed_mark18`).
inline constexpr std::int32_t kMark18Changeover = 3;
// The most torpedo types a boat carries under the torpedo rules: the
// TDC table's three entries.
inline constexpr std::size_t kMaxTorpedoTypes = 3;

// Returns the WEAPONS.DAT ids carried in the class's tubes and racks of
// `load`, in increasing order, each once (not in the original).
std::vector<std::uint32_t> carried_torpedo_types(const TorpedoLoad& load, const SubClass& sub);

// The Mark 27 of a generated load: two in the stern racks.
inline constexpr std::int32_t kMark27Rack = 2;
// Returns whether the Mark 27 (`kMark27Id`) is supplied to `command` on
// `year` (since 1930) / `month` (0-based): from June 1944 for ComSubPac,
// September 1944 elsewhere.
bool mark27_in_service(std::int32_t year, std::int32_t month, SubCommand command);
// Appends the Mark 27 record to `tables` (once): the Mark 18-1's run
// model, slowed to 12 kn for 5000 yd, a 19-inch, 7.5-ft (8) body and a
// 95-lb warhead. Throws `DataError` without the MARK18-1.
void add_extended_torpedoes(GameTables& tables);

}  // namespace sh
