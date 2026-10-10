// `DATA/MAP<n>BASE.DAT` (`MAP<n>BAS.DAT` from map 10): the ports and bases of
// a patrol map, named by the `PZ.DAT` zone record and loaded by `bases_load`
// (0x16648) to 0x116240. docs/structures.md
// "MAP<n>BASE.DAT".
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sh {

// Who holds a base during an owner period.
enum class BaseSide : std::int32_t {
  kNone = 0,      // unused period
  kJapanese = 1,
  kAllied = 2,    // no enemy ships spawn
};

// The bits of `BaseRecord::flags`.
enum class BaseFlag : std::uint32_t {
  kActive = 1u << 0,
  kSmallGuard = 1u << 1,  // a guard of 20 instead of 100
  kAircraft = 1u << 4,    // aircraft with the anchored ships
  kSpawnsPatrols = 1u << 5,
  kNoRecon = 1u << 7,     // excluded from photo-recon picks
};

// A world point of a base: world units of the map, `y` the height (0), `z`
// north.
struct BasePoint {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;
};

// An owner period: the side holding the base from this date on (the first
// period from the start); `bases_owner_on_date` (0x17315) picks the current
// one.
struct BaseOwnerPeriod {
  BaseSide side = BaseSide::kNone;
  std::int32_t month = 0;  // 1-based
  std::int32_t day = 0;
  std::int32_t year = 0;   // full year
};

// One 0x7f-byte base record.
struct BaseRecord {
  static constexpr std::size_t kRecordSize = 0x7f;

  std::string name;
  BasePoint station;  // chart icon and single-mission start
  BasePoint harbor;   // anchored ships and career start
  BasePoint patrol;   // patrol point
  std::array<BaseOwnerPeriod, 3> owners{};
  std::uint32_t flags = 0;  // `BaseFlag` bits
  // Anchored-ship size class, drawn on the chart icon; also the air-base
  // size (`bases_pick_random_japanese_base` returns it minus 1).
  std::int32_t anchored_size = 0;
  std::int32_t anchored_size_cap = 0;
  std::int32_t patrol_param_a = 0;    // patrol group parameter (contacts_patrol_init)
  std::int32_t patrol_spawn_pct = 0;  // per day
  std::int32_t patrol_param_b = 0;    // patrol group parameter (contacts_patrol_init)
  std::int32_t pick_weight = 0;       // air-raid and photo-recon base picks

  bool has(BaseFlag f) const { return (flags & static_cast<std::uint32_t>(f)) != 0; }
};

// Returns the DOS file name of patrol map `map`'s base table (8.3 names from
// map 10 on), as the `PZ.DAT` records name it.
std::string base_file_name(int map);

// Parses a whole base table: `size / 0x7f` records, a trailing partial record
// ignored as the game does; `context` names it in errors.
std::vector<BaseRecord> parse_bases(std::span<const std::uint8_t> data, std::string_view context);

}  // namespace sh
