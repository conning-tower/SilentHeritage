#include "assets/routes.h"

#include <format>

#include "core/byte_reader.h"

namespace sh {

std::vector<ConvoyRoute> parse_routes(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  const std::int32_t n = r.i32();
  if (n < 0) {
    r.fail(std::format("negative route count {}", n));
  }
  // The smallest route: count, dates, two monthly tables and flags.
  constexpr std::size_t kMinRouteSize = 4 + 24 + 2 * 4 * kCampaignMonths + 1;
  if (static_cast<std::size_t>(n) > r.remaining() / kMinRouteSize) {
    r.fail(std::format("{} routes do not fit in the file", n));
  }
  // Each array runs over all routes before the next one starts.
  std::vector<ConvoyRoute> routes(static_cast<std::size_t>(n));
  std::vector<std::int32_t> counts;
  for (std::int32_t i = 0; i < n; ++i) {
    const std::int32_t count = r.i32();
    if (count < 0) {
      r.fail(std::format("negative waypoint count {}", count));
    }
    counts.push_back(count);
  }
  for (ConvoyRoute& route : routes) {
    for (RouteDate* d : {&route.first, &route.last}) {
      d->month = r.i32();
      d->day = r.i32();
      d->year = r.i32();
    }
  }
  for (ConvoyRoute& route : routes) {
    for (auto& f : route.frequency) {
      f = r.i32();
    }
  }
  for (ConvoyRoute& route : routes) {
    for (auto& p : route.convoy_pct) {
      p = r.i32();
    }
  }
  for (ConvoyRoute& route : routes) {
    route.flags = r.u8();
  }
  for (std::size_t i = 0; i < routes.size(); ++i) {
    if (static_cast<std::size_t>(counts[i]) > r.remaining() / sizeof(RouteBox)) {
      r.fail(std::format("route {}: {} waypoints do not fit in the file", i, counts[i]));
    }
    routes[i].waypoints.resize(static_cast<std::size_t>(counts[i]));
    for (RouteBox& b : routes[i].waypoints) {
      b = RouteBox{r.i32(), r.i32(), r.i32(), r.i32(), r.i32(), r.i32()};
    }
  }
  return routes;
}

}  // namespace sh
