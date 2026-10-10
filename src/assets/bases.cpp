#include "assets/bases.h"

#include <format>

#include "core/byte_reader.h"

namespace sh {
namespace {

BasePoint read_point(ByteReader& r) {
  BasePoint p;
  p.x = r.i32();
  p.y = r.i32();
  p.z = r.i32();
  return p;
}

BaseRecord read_base(ByteReader r) {
  BaseRecord b;
  b.name = r.fixed_string(15);
  b.station = read_point(r);
  b.harbor = read_point(r);
  b.patrol = read_point(r);
  for (BaseOwnerPeriod& o : b.owners) {
    o.side = static_cast<BaseSide>(r.i32());
    o.month = r.i32();
    o.day = r.i32();
    o.year = r.i32();
  }
  b.flags = r.u32();
  b.anchored_size = r.i32();
  b.anchored_size_cap = r.i32();
  b.patrol_param_a = r.i32();
  b.patrol_spawn_pct = r.i32();
  b.patrol_param_b = r.i32();
  b.pick_weight = r.i32();
  return b;
}

}  // namespace

std::string base_file_name(int map) {
  return map < 10 ? std::format("MAP{}BASE.DAT", map) : std::format("MAP{}BAS.DAT", map);
}

std::vector<BaseRecord> parse_bases(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  std::vector<BaseRecord> bases;
  const std::size_t count = data.size() / BaseRecord::kRecordSize;
  bases.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    bases.push_back(read_base(r.sub(BaseRecord::kRecordSize)));
  }
  return bases;
}

}  // namespace sh
