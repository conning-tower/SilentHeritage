#include "assets/scn_file.h"

#include <algorithm>
#include <format>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

#include "assets/format_header.h"
#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {
namespace {

// The record tags (`scen_load_ascn`).
enum class Tag : std::uint8_t {
  kEnd = 0,
  kPlayerSub = 1,
  kShip = 2,
  kAircraft = 3,
  kEnemySub = 4,
  kBomberGroup = 5,
  kPhotoRecon = 6,
};

template <typename E>
E read_enum(ByteReader& r) {
  return static_cast<E>(r.i32());
}

template <std::size_t N>
void read_chars(ByteReader& r, ScnChars<N>& out) {
  const auto raw = r.bytes(N);
  std::transform(raw.begin(), raw.end(), out.begin(), [](std::uint8_t b) { return static_cast<char>(b); });
}

void read_waypoints(ByteReader& r, Waypoints& w) {
  for (auto& v : w.x) {
    v = r.i32();
  }
  for (auto& v : w.y) {
    v = r.i32();
  }
  w.count = r.i32();
}

ScnPlayerSub read_player_sub(ByteReader& r, std::uint8_t version) {
  ScnPlayerSub p;
  p.x = r.i32();
  p.y = r.i32();
  p.depth = r.i32();
  p.heading = r.i32();
  p.engine_order = r.i32();
  p.preserved_14 = r.i32();
  read_chars(r, p.sub_class);
  read_chars(r, p.name);
  if (version >= 3) {
    p.crew = read_enum<CrewQuality>(r);
  }
  return p;
}

ScnShip read_ship(ByteReader& r) {
  ScnShip s;
  s.x = r.i32();
  s.y = r.i32();
  s.heading = r.i32();
  s.speed = r.i32();
  s.orders = read_enum<ShipOrders>(r);
  s.crew = read_enum<CrewQuality>(r);
  s.damage_pct = r.i32();
  read_chars(r, s.ship_class);
  read_chars(r, s.name);
  read_waypoints(r, s.waypoints);
  s.convoy_group = r.i32();
  s.zigzag = r.i32();
  s.alert = r.i32();
  return s;
}

ScnAircraft read_aircraft(ByteReader& r, std::uint8_t version) {
  ScnAircraft a;
  a.mission = read_enum<AircraftMission>(r);
  a.crew = read_enum<CrewQuality>(r);
  read_chars(r, a.aircraft_type);
  a.x = r.i32();
  a.y = r.i32();
  a.orbit_radius = r.i32();
  a.strafes = r.i32();
  a.bomb_load = r.i32();
  read_waypoints(r, a.waypoints);
  a.escort_group = r.i32();
  if (version >= 6) {
    a.relaunch = r.i32();
  }
  return a;
}

ScnEnemySub read_enemy_sub(ByteReader& r, std::uint8_t version) {
  ScnEnemySub e;
  e.x = r.i32();
  e.y = r.i32();
  e.depth = r.i32();
  e.heading = r.i32();
  e.speed = r.i32();
  e.orders = read_enum<ShipOrders>(r);
  e.crew = read_enum<CrewQuality>(r);
  e.damage_pct = r.i32();
  read_chars(r, e.sub_class);
  read_waypoints(r, e.waypoints);
  e.target_x = r.i32();
  e.target_y = r.i32();
  e.alert = r.i32();
  if (version >= 5) {
    e.side = r.i32();
  }
  return e;
}

// Appends little-endian fields to a file image.
class ScnWriter {
 public:
  void u8(std::uint8_t v) { out_.push_back(v); }
  void u32(std::uint32_t v) {
    for (int shift = 0; shift < 32; shift += 8) {
      out_.push_back(static_cast<std::uint8_t>(v >> shift));
    }
  }
  void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
  template <typename E>
    requires std::is_enum_v<E>
  void value(E v) {
    i32(static_cast<std::int32_t>(v));
  }
  void bytes(std::span<const std::uint8_t> b) { out_.insert(out_.end(), b.begin(), b.end()); }
  void chars(std::span<const char> c) {
    std::transform(c.begin(), c.end(), std::back_inserter(out_), [](char ch) { return static_cast<std::uint8_t>(ch); });
  }
  void waypoints(const Waypoints& w) {
    for (const std::int32_t v : w.x) {
      i32(v);
    }
    for (const std::int32_t v : w.y) {
      i32(v);
    }
    i32(w.count);
  }

  // The objects' `write_scn` slots (`sbplayer_write_scn` .. `sbmission_write_scn`).
  void record(const ScnPlayerSub& p) {
    tag(Tag::kPlayerSub);
    i32(p.x);
    i32(p.y);
    i32(p.depth);
    i32(p.heading);
    i32(p.engine_order);
    i32(p.preserved_14);
    chars(p.sub_class);
    chars(p.name);
    value(p.crew);
  }
  void record(const ScnShip& s) {
    tag(Tag::kShip);
    i32(s.x);
    i32(s.y);
    i32(s.heading);
    i32(s.speed);
    value(s.orders);
    value(s.crew);
    i32(s.damage_pct);
    chars(s.ship_class);
    chars(s.name);
    waypoints(s.waypoints);
    i32(s.convoy_group);
    i32(s.zigzag);
    i32(s.alert);
  }
  void record(const ScnAircraft& a) {
    tag(Tag::kAircraft);
    value(a.mission);
    value(a.crew);
    chars(a.aircraft_type);
    i32(a.x);
    i32(a.y);
    i32(a.orbit_radius);
    i32(a.strafes);
    i32(a.bomb_load);
    waypoints(a.waypoints);
    i32(a.escort_group);
    i32(a.relaunch);
  }
  void record(const ScnEnemySub& e) {
    tag(Tag::kEnemySub);
    i32(e.x);
    i32(e.y);
    i32(e.depth);
    i32(e.heading);
    i32(e.speed);
    value(e.orders);
    value(e.crew);
    i32(e.damage_pct);
    chars(e.sub_class);
    waypoints(e.waypoints);
    i32(e.target_x);
    i32(e.target_y);
    i32(e.alert);
    i32(e.side);
  }
  void record(const BomberGroupRecord& b) {
    tag(Tag::kBomberGroup);
    i32(b.base);
    value(b.size);
    i32(b.launch_delay_min);
    i32(b.hold_min);
  }
  void record(const PhotoReconRecord& p) {
    tag(Tag::kPhotoRecon);
    i32(p.base);
    i32(p.delay_min);
    i32(p.duration_min);
  }

  void tag(Tag t) { u8(static_cast<std::uint8_t>(t)); }

  std::vector<std::uint8_t> take() { return std::move(out_); }

 private:
  std::vector<std::uint8_t> out_;
};

}  // namespace

std::string scn_text(std::span<const char> chars) {
  return std::string(chars.begin(), std::find(chars.begin(), chars.end(), '\0'));
}

ScnFile read_scn_file(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  ScnFile f;
  f.version = read_format_header(r, "ASCN", 2);
  read_chars(r, f.briefing);
  f.special_mission = r.i32();
  read_chars(r, f.world);
  f.weather = read_enum<Weather>(r);
  f.sea_state = read_enum<SeaState>(r);
  f.fog = r.i32();
  f.gradient = r.i32();
  f.month = r.i32();
  f.day = r.i32();
  f.year = r.i32();
  f.hour = r.i32();
  f.minute = r.i32();
  if (f.version >= 4) {
    f.end_on_lost_contact = r.i32();
    f.randomize = r.i32();
    f.random_seed = r.i32();
    const auto raw = r.bytes(kScnPasswordSize);
    std::copy(raw.begin(), raw.end(), f.password.begin());
  } else {
    f.special_mission = 0;  // scen_load_ascn clears it after the records
  }
  if (f.version >= 7) {
    f.required_memory = r.i32();
  }
  for (auto& t : f.tube_torpedoes) {
    t = r.i32();
  }
  for (auto& reload : f.reloads) {
    reload.type = r.i32();
    reload.aft = r.i32();
    reload.forward = r.i32();
  }
  f.equipment = r.u32();
  for (;;) {
    const std::uint8_t tag = r.u8();
    switch (static_cast<Tag>(tag)) {
      case Tag::kEnd:
        return f;
      case Tag::kPlayerSub:
        f.records.emplace_back(read_player_sub(r, f.version));
        break;
      case Tag::kShip:
        f.records.emplace_back(read_ship(r));
        break;
      case Tag::kAircraft:
        f.records.emplace_back(read_aircraft(r, f.version));
        break;
      case Tag::kEnemySub:
        f.records.emplace_back(read_enemy_sub(r, f.version));
        break;
      case Tag::kBomberGroup:
        f.records.emplace_back(BomberGroupRecord{
            .base = r.i32(),
            .size = read_enum<BomberGroupSize>(r),
            .launch_delay_min = r.i32(),
            .hold_min = r.i32(),
        });
        break;
      case Tag::kPhotoRecon:
        f.records.emplace_back(PhotoReconRecord{
            .base = r.i32(),
            .delay_min = r.i32(),
            .duration_min = r.i32(),
        });
        break;
      default:
        r.fail(std::format("unknown record tag {}", tag));
    }
  }
}

std::vector<std::uint8_t> write_scn_file(const ScnFile& f) {
  ScnWriter w;
  constexpr std::array<std::uint8_t, 5> kMagic = {'A', 'S', 'C', 'N', 0};
  w.bytes(kMagic);
  w.u8(kScnWriteVersion);
  w.chars(f.briefing);
  w.i32(f.special_mission);
  w.chars(f.world);
  w.value(f.weather);
  w.value(f.sea_state);
  w.i32(f.fog);
  w.i32(f.gradient);
  w.i32(f.month);
  w.i32(f.day);
  w.i32(f.year);
  w.i32(f.hour);
  w.i32(f.minute);
  w.i32(f.end_on_lost_contact);
  w.i32(f.randomize);
  w.i32(f.random_seed);
  w.bytes(f.password);
  w.i32(f.required_memory);
  for (const std::int32_t t : f.tube_torpedoes) {
    w.i32(t);
  }
  for (const TorpedoReload& reload : f.reloads) {
    w.i32(reload.type);
    w.i32(reload.aft);
    w.i32(reload.forward);
  }
  w.u32(f.equipment);
  for (const ScnRecord& record : f.records) {
    std::visit([&w](const auto& r) { w.record(r); }, record);
  }
  w.tag(Tag::kEnd);
  return w.take();
}

ScnPasswordKey read_scn_password_key(const ExeImage& se_exe) {
  constexpr std::uint32_t kAddress = 0x9bcb4;
  ScnPasswordKey key{};
  std::array<bool, kScnPasswordSize> seen{};
  for (std::uint32_t i = 0; i < key.size(); ++i) {
    key[i] = se_exe.u8(kAddress + i);
    if (key[i] >= kScnPasswordSize || seen[key[i]] || (i == 0 && key[i] != 0)) {
      throw DataError(std::format("SE.EXE: unexpected password key at {:#x}", kAddress));
    }
    seen[key[i]] = true;
  }
  return key;
}

std::array<std::uint8_t, kScnPasswordSize> encode_scn_password(std::string_view text, const ScnPasswordKey& key) {
  if (text.size() >= kScnPasswordSize) {
    throw std::invalid_argument(std::format("password of {} characters (14 at most)", text.size()));
  }
  std::array<std::uint8_t, kScnPasswordSize> out{};
  if (text.empty()) {
    return out;  // cmd_set_password stores an empty entry unencoded
  }
  std::copy(text.begin(), text.end(), out.begin());
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<std::uint8_t>(out[i] + key[i]);
  }
  return out;
}

std::string decode_scn_password(std::span<const std::uint8_t, kScnPasswordSize> stored, const ScnPasswordKey& key) {
  std::string out;
  for (std::size_t i = 0; i < stored.size(); ++i) {
    const auto c = static_cast<char>(static_cast<std::uint8_t>(stored[i] - key[i]));
    if (c == '\0') {
      break;
    }
    out.push_back(c);
  }
  return out;
}

}  // namespace sh
