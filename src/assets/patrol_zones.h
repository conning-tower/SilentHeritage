// `DATA/PZ.DAT`: the 15 patrol zones (one per `.WLD` map), loaded by
// `load_pz_dat` (0x51d03); the selected record is copied to 0x11b808 and read
// by the patrol-zone pickers, the base, convoy, air and weather code.
// docs/formats.md "Other files".
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/routes.h"

namespace sh {

// The submarine command a zone belongs to.
enum class Command : std::int32_t {
  kComSubsAsiatic = 0,
  kComSubPac = 1,
  kComSubSoWesPac = 2,
  kTaskForce42 = 3,
};

// Map edges, indexing `start_edges`/`air_start_edges`: the last grid row
// (south), the first grid row (north), the first column (west), the last
// column (east).
enum class MapEdge : int { kSouth = 0, kNorth = 1, kWest = 2, kEast = 3 };

// One 0x4bc-byte `PZ.DAT` record. "Per month" tables are indexed by
// campaign month unless they say calendar month (0 = January).
struct PatrolZone {
  static constexpr std::size_t kRecordSize = 0x4bc;

  std::string name;    // shown on the orders screen, e.g. "Southern Japan"
  std::string world;   // "WORLD-1.WLD"; also matched against a `.SCN` world
  std::string routes;  // "SJAPAN.CR"
  std::string bases;   // "MAP1BASE.DAT"
  // The window in which the generator may pick the zone (full years,
  // 1-based months, inclusive).
  std::int32_t first_year = 0;
  std::int32_t last_year = 0;
  std::int32_t first_month = 0;
  std::int32_t last_month = 0;
  std::array<std::int32_t, 5> year_weight{};  // single-mission draw weight, 1941..1945
  Command command = Command::kComSubsAsiatic;
  // x0, y0, x1, y1 of the zone box on the patrol-orders map (screen pixels
  // offset by 12, 40).
  std::array<std::int32_t, 4> orders_rect{};
  // Per `MapEdge`: non-zero = the player may start there (the career's edge
  // search and the bomber groups read it so).
  std::array<std::int32_t, 4> start_edges{};
  // Per `MapEdge`: the same for bomber-group starts.
  std::array<std::int32_t, 4> air_start_edges{};
  // n / 4 + rand(n / 8) sampan groups a day.
  std::int32_t sampan_density = 0;
  std::array<std::int32_t, 12> fog_pct{};  // per calendar month
  // Per calendar month: % of Clear, Partly Cloudy, Mostly Cloudy, Overcast.
  std::array<std::array<std::int32_t, 4>, 12> weather_pct{};
  std::array<std::array<std::uint8_t, 3>, kCampaignMonths> convoy_size_pct{};  // small, medium, large
  std::array<std::array<std::uint8_t, 4>, kCampaignMonths> escort_pct{};       // escort level 0..3
  std::array<std::array<std::uint8_t, 3>, kCampaignMonths> crew_pct{};         // crew quality 0..2
  std::array<std::int32_t, kCampaignMonths> air_cover_pct{};  // chance of air cover over a group
  // Daily chance of a career air raid (also gates air-strike single missions).
  std::array<std::int32_t, kCampaignMonths> air_raid_pct{};
};

// Parses a whole `PZ.DAT` (`u32 count` + records); `context` names it in
// errors. Throws `DataError` if it is truncated.
std::vector<PatrolZone> parse_patrol_zones(std::span<const std::uint8_t> data, std::string_view context);

}  // namespace sh
