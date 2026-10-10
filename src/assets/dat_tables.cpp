#include "assets/dat_tables.h"

#include <algorithm>
#include <cctype>
#include <format>

#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {

namespace {

constexpr std::size_t kIndexNameSize = 28;

// Field accessors over one fixed-size record, by offset within it.
class Rec {
 public:
  explicit Rec(std::span<const std::uint8_t> data) : data_(data) {}

  std::uint8_t u8(std::size_t off) const { return at(off, 1)[0]; }
  std::uint32_t u32(std::size_t off) const { return read_u32le(data_, off); }
  std::int32_t i32(std::size_t off) const { return static_cast<std::int32_t>(u32(off)); }
  Fixed16 fx(std::size_t off) const { return Fixed16::from_raw(i32(off)); }
  // Returns the NUL-terminated string of the `n`-byte field at `off`.
  std::string str(std::size_t off, std::size_t n) const {
    const auto field = at(off, n);
    return std::string(field.begin(), std::find(field.begin(), field.end(), std::uint8_t{0}));
  }
  template <std::size_t N>
  std::array<std::int32_t, N> i32s(std::size_t off) const {
    std::array<std::int32_t, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
      out[i] = i32(off + 4 * i);
    }
    return out;
  }
  template <std::size_t N>
  std::array<Fixed16, N> fxs(std::size_t off) const {
    std::array<Fixed16, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
      out[i] = fx(off + 4 * i);
    }
    return out;
  }
  template <std::size_t N>
  std::array<std::uint8_t, N> raw() const {
    std::array<std::uint8_t, N> out{};
    std::ranges::copy(at(0, N), out.begin());
    return out;
  }

 private:
  std::span<const std::uint8_t> at(std::size_t off, std::size_t n) const {
    if (off + n > data_.size()) {
      throw DataError(std::format("record field at +{} past the {}-byte record", off, data_.size()));
    }
    return data_.subspan(off, n);
  }

  std::span<const std::uint8_t> data_;
};

// One record of an indexed table: the index entry's name and the record.
struct IndexedRecord {
  std::string index_name;
  Rec rec;
};

// Reads the "indexed" framing: u8 count, count x {char[28] name, u32 offset},
// fixed-size records at the offsets (`load_subs_dat` reads `record_size`
// bytes at the offset of the matching entry).
std::vector<IndexedRecord> read_indexed(std::span<const std::uint8_t> data, std::size_t record_size,
                                        std::string_view context) {
  ByteReader r(data, context);
  const std::uint8_t count = r.u8();
  std::vector<IndexedRecord> out;
  for (std::uint8_t i = 0; i < count; ++i) {
    std::string name = r.fixed_string(kIndexNameSize);
    const std::uint32_t offset = r.u32();
    if (offset > data.size() || data.size() - offset < record_size) {
      r.fail(std::format("record {} at {:#x} runs past the end", i, offset));
    }
    out.push_back(IndexedRecord{std::move(name), Rec(data.subspan(offset, record_size))});
  }
  return out;
}

// Reads a bare array of `record_size` records (count = size / stride, as the
// loaders compute it from the file size).
std::vector<Rec> read_flat(std::span<const std::uint8_t> data, std::size_t record_size, std::string_view context) {
  if (data.size() % record_size != 0) {
    throw DataError(std::format("{}: {} bytes is not a multiple of the {}-byte record", context, data.size(),
                                record_size));
  }
  std::vector<Rec> out;
  for (std::size_t off = 0; off < data.size(); off += record_size) {
    out.emplace_back(data.subspan(off, record_size));
  }
  return out;
}

// Reads one "counted" section (u32 count, then the records) from `r`.
std::vector<Rec> read_counted(ByteReader& r, std::size_t record_size) {
  const std::uint32_t count = r.u32();
  if (std::size_t{count} > r.remaining() / record_size) {
    r.fail(std::format("{} records of {} bytes do not fit", count, record_size));
  }
  std::vector<Rec> out;
  for (std::uint32_t i = 0; i < count; ++i) {
    out.emplace_back(r.bytes(record_size));
  }
  return out;
}

template <typename T>
const T* find_by(const std::vector<T>& records, std::string_view name, std::string T::*key) {
  const auto it = std::ranges::find_if(records, [&](const T& rec) { return equals_ignore_case(rec.*key, name); });
  return it == records.end() ? nullptr : &*it;
}

}  // namespace

bool equals_ignore_case(std::string_view a, std::string_view b) {
  return std::ranges::equal(a, b, [](char x, char y) {
    return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
  });
}

std::vector<ShipClass> parse_ships_dat(std::span<const std::uint8_t> data) {
  std::vector<ShipClass> out;
  for (const auto& [index_name, r] : read_indexed(data, ShipClass::kSize, "SHIPS.DAT")) {
    out.push_back(ShipClass{
        .index_name = index_name,
        .name = r.str(0, 25),
        .ship_type = static_cast<ShipType>(r.u8(25)),
        .role = static_cast<ShipRole>(r.u8(26)),
        .sprite1 = r.str(27, 13),
        .sprite2 = r.str(40, 13),
        .idbook_picture = r.str(53, 13),
        .start_year = r.u32(68),
        .end_year = r.u32(72),
        .start_month = r.u32(76),
        .end_month = r.u32(80),
        .selection_weight = r.i32(84),
        .render_size = r.u32(88),
        .bow_wave_size = r.u32(92),
        .category = r.u32(96),
        .screw_sound = static_cast<ScrewSound>(r.u32(100)),
        .fire_order = r.u32(104),
        .hull_strength = r.u32(108),
        .max_power = r.fx(112),
        .submerged_power = r.fx(116),
        .depth_charges = r.u32(120),
        .dc_racks = r.u32(124),
        .dc_throwers = r.u32(128),
        .has_sonar = r.i32(132) != 0,
        .main_gun_count = r.u32(136),
        .main_gun_caliber_in = r.str(140, 8),
        .gun_mounts = {r.i32(148) != 0, r.i32(152) != 0},
        .gun_id = r.u32(156),
        .top_speed_kts = r.str(160, 8),
        .submerged_speed_kts = r.str(168, 8),
        .length_ft = r.u32(176),
        .beam_ft = r.u32(180),
        .draft_ft = r.u32(184),
        .visual_size = r.u32(188),
        .deck_height_ft = r.u32(192),
        .displacement_tons = r.u32(196),
        .ballast_neutral = r.u32(200),
        .ballast_max = r.u32(204),
        .drag_cubic = r.fx(208),
        .drag_cubic_submerged = r.fx(212),
        .drag_linear = r.fx(216),
        .drag_linear_submerged = r.fx(220),
        .lateral_drag = r.fx(224),
        .astern_drag_factor = r.fx(228),
        .turn_factor = r.fx(232),
        .raw = r.raw<ShipClass::kSize>(),
    });
  }
  return out;
}

std::vector<SubClass> parse_subs_dat(std::span<const std::uint8_t> data) {
  std::vector<SubClass> out;
  for (const auto& [index_name, r] : read_indexed(data, SubClass::kSize, "SUBS.DAT")) {
    SubClass s{
        .index_name = index_name,
        .name = r.str(0, 10),
        .display_name = r.str(10, 26),
        .class_index = r.u32(36),
        .picture_frame = r.u8(40),
        .sprite_silhouette = r.str(41, 14),
        .sprite_profile = r.str(55, 17),
        .intro_month = r.u32(72),
        .intro_year = r.u32(76),
        .end_month = r.u32(80),
        .end_year = r.u32(84),
        .range_nm = r.u32(100),
        .range_at_kts = r.u32(104),
        .bow_tubes = r.u32(116),
        .stern_tubes = r.u32(120),
        .bow_reloads = r.u32(124),
        .stern_reloads = r.u32(128),
        .modern_torpedoes = r.i32(132) != 0,
        .deck_gun_count = r.u32(136),
        .deck_gun_caliber_in = r.u32(140),
        .single_gun_mount = r.u32(144),
        .length_ft = r.u32(148),
        .beam_ft = r.u32(152),
        .draft_ft = r.u32(156),
        .test_depth_ft = r.u32(160),
        .periscope_depth_ft = r.u32(164),
        .radar_depth_ft = r.u32(168),
        .periscope_eye_ft = r.u32(172),
        .bridge_eye_ft = r.u32(176),
        .awash_depth_ft = r.u32(180),
        .noise_factor_pct = r.u32(184),
        .surface_speed_kts = r.str(188, 10),
        .submerged_speed_kts = r.str(198, 10),
        .displacement_surfaced = r.u32(208),
        .displacement_submerged = r.u32(212),
        .displacement_max = r.u32(216),
        .diesel_hp = r.u32(220),
        .motor_hp = r.u32(224),
        .drag_cubic = r.fxs<2>(228),
        .drag_linear = r.fxs<2>(236),
        .lateral_drag = r.fx(244),
        .astern_drag_factor = r.fx(248),
        .turn_factor = r.fx(252),
        .pitch_factor = r.fx(256),
        .diesel_count = r.u32(260),
        .motor_count = r.u32(264),
        .fuel_capacity = r.u32(268),
        .fuel_use = r.u32(272),
        .deck_gun_rounds = r.u32(276),
        .power_surfaced = r.i32s<SubClass::kTelegraphSettings>(280),
        .power_submerged = r.i32s<SubClass::kTelegraphSettings>(324),
        .description = r.str(368, 984),
        .compartment_rects = {},
        .raw = r.raw<SubClass::kSize>(),
    };
    const auto rects = r.i32s<SubClass::kCompartments * 4>(1352);
    for (std::size_t i = 0; i < SubClass::kCompartments; ++i) {
      s.compartment_rects[i] = SubClass::Rect{rects[4 * i], rects[4 * i + 1], rects[4 * i + 2], rects[4 * i + 3]};
    }
    out.push_back(std::move(s));
  }
  return out;
}

std::vector<AircraftType> parse_aircraft_dat(std::span<const std::uint8_t> data) {
  std::vector<AircraftType> out;
  for (const auto& [index_name, r] : read_indexed(data, AircraftType::kSize, "AIRCRAFT.DAT")) {
    out.push_back(AircraftType{
        .index_name = index_name,
        .name = r.str(0, 25),
        .long_name = r.str(25, 25),
        .name2 = r.str(50, 25),
        .role = static_cast<AircraftRole>(r.u8(75)),
        .sprite = r.str(76, 16),
        .intro_year = r.u32(92),
        .end_year = r.u32(96),
        .start_month = r.u32(100),
        .end_month = r.u32(104),
        .render_size = r.u32(108),
        .aircraft_id = r.u32(116),
        .cruise_speed_mph = r.u32(120),
        .max_speed_mph = r.u32(124),
        .ceiling_ft = r.u32(128),
        .length_ft = r.u32(132),
        .span_ft = r.u32(136),
        .height_ft = r.u32(140),
        .range_mi = r.u32(144),
        .guns = r.u32(148),
        .bombs = r.u32(152),
        .torpedoes = r.u32(156),
        .roll_rate = r.i32(160),
        .raw = r.raw<AircraftType::kSize>(),
    });
  }
  return out;
}

Weapons parse_weapons_dat(std::span<const std::uint8_t> data) {
  ByteReader reader(data, "WEAPONS.DAT");
  Weapons out;
  for (const Rec& r : read_counted(reader, Torpedo::kSize)) {
    out.torpedoes.push_back(Torpedo{
        .short_name = r.str(0, 10),
        .long_name = r.str(10, 26),
        .id = r.u32(36),
        .nation = static_cast<Nation>(r.u32(40)),
        .month = r.u32(44),
        .year = r.u32(48),
        .range_long_yd = r.u32(52),
        .range_short_yd = r.u32(56),
        .speed_low_kts = r.u32(60),
        .speed_high_kts = r.u32(64),
        .diameter_in = r.u32(68),
        .warhead_lb = r.u32(72),
        .length_ft = r.u32(76),
        .beam_ft = r.u32(80),
        .propulsion = static_cast<TorpedoPropulsion>(r.u8(84)),
        .mk14_18_flag = r.u8(85),
        .speed_settings = r.u32(88),
        .displacement = r.u32(92),
        .reach_ft = r.i32s<2>(96),
        .power = r.fxs<2>(104),
        .drag_cubic = r.fxs<2>(112),
        .drag_linear = r.fxs<2>(120),
        .lateral_drag = r.fxs<2>(128),
        .turn_factor = r.fx(136),
        .run_time_s = r.i32s<2>(140),
        .consumption = r.fxs<2>(148),
        .raw = r.raw<Torpedo::kSize>(),
    });
  }
  for (const Rec& r : read_counted(reader, Gun::kSize)) {
    out.guns.push_back(Gun{
        .long_name = r.str(0, 32),
        .id = r.u32(32),
        .caliber = r.str(36, 12),
        .shell_caliber_in = r.u32(48),
        .max_range_yd = r.u32(52),
        .muzzle_velocity_fps = r.u32(56),
        .reload_time_s = r.u32(60),
        .raw = r.raw<Gun::kSize>(),
    });
  }
  if (!reader.at_end()) {
    reader.fail("trailing bytes after the gun section");
  }
  return out;
}

std::vector<CarsubBoat> parse_carsub_dat(std::span<const std::uint8_t> data) {
  std::vector<CarsubBoat> out;
  for (const Rec& r : read_flat(data, CarsubBoat::kSize, "CARSUB.DAT")) {
    out.push_back(CarsubBoat{
        .name = r.str(0, 20),
        .hull_number = r.u32(20),
        .avail_year = r.u32(24),
        .avail_month = r.u32(28),
        .end_year = r.u32(32),
        .end_month = r.u32(36),
        .class_index = r.u8(40),
        .raw = r.raw<CarsubBoat::kSize>(),
    });
  }
  return out;
}

std::vector<IjnShip> parse_ijnships_dat(std::span<const std::uint8_t> data) {
  std::vector<IjnShip> out;
  for (const Rec& r : read_flat(data, IjnShip::kSize, "IJNSHIPS.DAT")) {
    out.push_back(IjnShip{
        .ship_name = r.str(0, 25),
        .class_name = r.str(25, 25),
        .commission_year = r.u32(50),
        .commission_month = r.u32(54),
        .end_year = r.u32(58),
        .end_month = r.u32(62),
        .raw = r.raw<IjnShip::kSize>(),
    });
  }
  return out;
}

std::vector<Commander> parse_cmdlist_dat(std::span<const std::uint8_t> data) {
  ByteReader reader(data, "CMDLIST.DAT");
  std::vector<Commander> out;
  for (const Rec& r : read_counted(reader, Commander::kSize)) {
    out.push_back(Commander{
        .name = r.str(0, 50),
        .tonnage_by_month = r.i32s<kCampaignMonths>(50),
        .raw = r.raw<Commander::kSize>(),
    });
  }
  return out;
}

std::vector<HomePort> parse_homeport_dat(std::span<const std::uint8_t> data) {
  std::vector<HomePort> out;
  for (const Rec& r : read_flat(data, HomePort::kSize, "HOMEPORT.DAT")) {
    HomePort p{
        .name = r.str(0, 20),
        .flags = r.u32(0x14),
        .open_date = PortDate{r.i32(0x18), r.i32(0x1c), r.i32(0x20)},
        .close_date = PortDate{r.i32(0x24), r.i32(0x28), r.i32(0x2c)},
        .transfer_weight = r.i32s<kCampaignMonths>(0x30),
        .zone_weight = {},
        .zone_distance_nm = r.i32s<HomePort::kZones>(0xef4),
        .command = static_cast<SubCommand>(r.i32(0xf44)),
        .map_point = r.i32s<2>(0xf48),
        .label_offset = r.i32s<2>(0xf50),
        .raw = r.raw<HomePort::kSize>(),
    };
    for (std::size_t z = 0; z < HomePort::kZones; ++z) {
      p.zone_weight[z] = r.i32s<kCampaignMonths>(0xe4 + z * kCampaignMonths * 4);
    }
    out.push_back(std::move(p));
  }
  return out;
}

std::vector<std::string> parse_rmsg_dat(std::span<const std::uint8_t> data) {
  ByteReader r(data, "RMSG.DAT");
  const std::uint32_t text_size = r.u32();  // the loader's single allocation
  const std::uint32_t count = r.u32();
  std::vector<std::string> out;
  std::size_t total = 0;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t length = r.u32();
    const auto text = r.bytes(length);
    if (length == 0 || text.back() != 0) {
      r.fail(std::format("template {} is not NUL-terminated", i));
    }
    out.emplace_back(text.begin(), text.end() - 1);
    total += length;
  }
  if (!r.at_end() || total != text_size) {
    r.fail(std::format("templates take {} bytes, header says {}", total, text_size));
  }
  return out;
}

GameTables GameTables::load(const Install& install) {
  return GameTables{
      .ships = parse_ships_dat(install.read("DATA/SHIPS.DAT")),
      .subs = parse_subs_dat(install.read("DATA/SUBS.DAT")),
      .aircraft = parse_aircraft_dat(install.read("DATA/AIRCRAFT.DAT")),
      .weapons = parse_weapons_dat(install.read("DATA/WEAPONS.DAT")),
      .carsub = parse_carsub_dat(install.read("DATA/CARSUB.DAT")),
      .ijn_ships = parse_ijnships_dat(install.read("DATA/IJNSHIPS.DAT")),
      .commanders = parse_cmdlist_dat(install.read("DATA/CMDLIST.DAT")),
      .home_ports = parse_homeport_dat(install.read("DATA/HOMEPORT.DAT")),
      .radio_messages = parse_rmsg_dat(install.read("DATA/RMSG.DAT")),
  };
}

const ShipClass* GameTables::find_ship(std::string_view name) const {
  return find_by(ships, name, &ShipClass::index_name);
}

const SubClass* GameTables::find_sub(std::string_view name) const { return find_by(subs, name, &SubClass::index_name); }

const AircraftType* GameTables::find_aircraft(std::string_view name) const {
  return find_by(aircraft, name, &AircraftType::index_name);
}

const Torpedo* GameTables::find_torpedo(std::string_view name) const {
  return find_by(weapons.torpedoes, name, &Torpedo::short_name);
}

const Torpedo* GameTables::find_torpedo_by_id(std::uint32_t id) const {
  const auto it = std::ranges::find(weapons.torpedoes, id, &Torpedo::id);
  return it == weapons.torpedoes.end() ? nullptr : &*it;
}

}  // namespace sh
