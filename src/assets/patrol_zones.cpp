#include "assets/patrol_zones.h"

#include <format>

#include "core/byte_reader.h"

namespace sh {
namespace {

template <std::size_t N>
void read_i32s(ByteReader& r, std::array<std::int32_t, N>& out) {
  for (auto& v : out) {
    v = r.i32();
  }
}

template <std::size_t N>
void read_u8s(ByteReader& r, std::array<std::uint8_t, N>& out) {
  for (auto& v : out) {
    v = r.u8();
  }
}

PatrolZone read_zone(ByteReader r) {
  PatrolZone z;
  z.name = r.fixed_string(31);
  z.world = r.fixed_string(13);
  z.routes = r.fixed_string(13);
  z.bases = r.fixed_string(13);
  z.first_year = r.i32();
  z.last_year = r.i32();
  z.first_month = r.i32();
  z.last_month = r.i32();
  read_i32s(r, z.year_weight);
  z.command = static_cast<Command>(r.i32());
  read_i32s(r, z.orders_rect);
  read_i32s(r, z.start_edges);
  read_i32s(r, z.air_start_edges);
  z.sampan_density = r.i32();
  read_i32s(r, z.fog_pct);
  for (auto& m : z.weather_pct) {
    read_i32s(r, m);
  }
  for (auto& m : z.convoy_size_pct) {
    read_u8s(r, m);
  }
  for (auto& m : z.escort_pct) {
    read_u8s(r, m);
  }
  for (auto& m : z.crew_pct) {
    read_u8s(r, m);
  }
  read_i32s(r, z.air_cover_pct);
  read_i32s(r, z.air_raid_pct);
  if (!r.at_end()) {
    r.fail("patrol zone record layout mismatch");
  }
  return z;
}

}  // namespace

std::vector<PatrolZone> parse_patrol_zones(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  const std::uint32_t n = r.u32();
  if (n > r.remaining() / PatrolZone::kRecordSize) {
    r.fail(std::format("{} patrol zones do not fit in the file", n));
  }
  std::vector<PatrolZone> zones;
  zones.reserve(n);
  for (std::uint32_t i = 0; i < n; ++i) {
    zones.push_back(read_zone(r.sub(PatrolZone::kRecordSize)));
  }
  return zones;
}

}  // namespace sh
