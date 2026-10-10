// `.CR` convoy routes (`DATA/<REGION>.CR`, one per patrol zone), loaded by
// `load_convoy_routes` (0x51ff2) and used by the route date check (0x30f53),
// the daily route update (0x2dd2c) and `convoy_build_on_route` (0x2e4c9).
// docs/formats.md ".CR".
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "assets/campaign_month.h"

namespace sh {

// A calendar date as the route files store it.
struct RouteDate {
  std::int32_t month = 0;  // 1-based
  std::int32_t day = 0;
  std::int32_t year = 0;   // full year
};

// A waypoint box: a group spawned on the route takes a random point inside
// it. Coordinates are world units of the region's `.WLD` map, `z` north
// (`y`, the height, is always 0).
struct RouteBox {
  std::int32_t x0 = 0, y0 = 0, z0 = 0;
  std::int32_t x1 = 0, y1 = 0, z1 = 0;
};

// One convoy route.
struct ConvoyRoute {
  // Bit 0 of `flags`: a coastal route (may carry coastal tankers; its groups
  // materialize only within sighting range).
  static constexpr std::uint8_t kCoastal = 0x01;

  RouteDate first;  // inclusive window in which the route is used
  RouteDate last;
  // Per campaign month: spawn rate (0 = no traffic); the next group is due
  // 0x34bc0 / frequency ticks later, plus jitter.
  std::array<std::int32_t, kCampaignMonths> frequency{};
  // Per campaign month: chance (%) that a group is a merchant convoy rather
  // than a warship task force.
  std::array<std::int32_t, kCampaignMonths> convoy_pct{};
  std::uint8_t flags = 0;
  std::vector<RouteBox> waypoints;  // run forward or backward at random

  bool coastal() const { return (flags & kCoastal) != 0; }
};

// Parses a whole `.CR` file; `context` names it in errors. Throws
// `DataError` if it is truncated or has a negative count.
std::vector<ConvoyRoute> parse_routes(std::span<const std::uint8_t> data, std::string_view context);

}  // namespace sh
