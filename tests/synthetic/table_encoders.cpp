#include "synthetic/table_encoders.h"

#include <bit>
#include <stdexcept>

#include "synthetic/encoders.h"

namespace sh::test {

namespace {

constexpr std::size_t kIndexNameSize = 28;
constexpr std::size_t kBankEntrySize = 0x24;

// Field writers over one fixed-size record, by offset within it (the
// parsers' `Rec`).
template <std::size_t N>
class RecWriter {
 public:
  explicit RecWriter(const std::array<std::uint8_t, N>& raw) : data_(raw) {}

  void u8(std::size_t off, std::uint8_t v) { data_.at(off) = v; }
  void u32(std::size_t off, std::uint32_t v) {
    for (std::size_t i = 0; i < 4; ++i) {
      data_.at(off + i) = static_cast<std::uint8_t>(v >> (8 * i));
    }
  }
  void i32(std::size_t off, std::int32_t v) { u32(off, static_cast<std::uint32_t>(v)); }
  void fx(std::size_t off, Fixed16 v) { i32(off, v.raw()); }
  // Writes `s` and its NUL; the field's bytes after the NUL keep the raw
  // record's.
  void str(std::size_t off, std::size_t n, std::string_view s) {
    if (s.size() >= n) {
      throw std::invalid_argument("string longer than its record field");
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
      data_.at(off + i) = static_cast<std::uint8_t>(s[i]);
    }
    data_.at(off + s.size()) = 0;
  }
  // Writes a flag the parser reads as `!= 0`: the raw record's value when
  // it agrees, else 1 or 0.
  void flag(std::size_t off, bool v) {
    const std::uint32_t raw = static_cast<std::uint32_t>(data_.at(off)) | data_.at(off + 1) << 8 |
                              data_.at(off + 2) << 16 | static_cast<std::uint32_t>(data_.at(off + 3)) << 24;
    if ((raw != 0) != v) {
      u32(off, v ? 1 : 0);
    }
  }
  template <typename T, std::size_t M>
  void i32s(std::size_t off, const std::array<T, M>& v) {
    for (std::size_t i = 0; i < M; ++i) {
      i32(off + 4 * i, static_cast<std::int32_t>(v[i]));
    }
  }
  template <std::size_t M>
  void fxs(std::size_t off, const std::array<Fixed16, M>& v) {
    for (std::size_t i = 0; i < M; ++i) {
      fx(off + 4 * i, v[i]);
    }
  }
  const std::array<std::uint8_t, N>& data() const { return data_; }

 private:
  std::array<std::uint8_t, N> data_;
};

// Writes the "indexed" framing of `records`: u8 count, {name, offset}
// entries, then the records.
template <std::size_t N>
std::vector<std::uint8_t> indexed(const std::vector<std::pair<std::string, std::array<std::uint8_t, N>>>& records) {
  if (records.size() > 255) {
    throw std::invalid_argument("more than 255 indexed records");
  }
  Bytes out;
  out.u8(static_cast<std::uint8_t>(records.size()));
  std::uint32_t offset = 1 + static_cast<std::uint32_t>(records.size() * (kIndexNameSize + 4));
  for (const auto& [name, rec] : records) {
    out.fixed_string(name, kIndexNameSize);
    out.u32(offset);
    offset += N;
  }
  for (const auto& rec : records) {
    out.bytes(rec.second);
  }
  return out.take();
}

// The header as `.MEL` files and `.BNK` records store it: an unread zero
// word, then the fields.
void write_mel_header(Bytes& out, const MelHeader& h) {
  out.u32(0);
  out.i32(h.rate);
  out.i32(h.rate_max);
  out.i32(h.rate_min);
  out.i32(h.loop_count);
  out.i32(h.volume_offset);
}

void f64(Bytes& out, double v) {
  const auto bits = std::bit_cast<std::uint64_t>(v);
  out.u32(static_cast<std::uint32_t>(bits));
  out.u32(static_cast<std::uint32_t>(bits >> 32));
}

}  // namespace

std::vector<std::uint8_t> encode_ships_dat(std::span<const ShipClass> ships) {
  std::vector<std::pair<std::string, std::array<std::uint8_t, ShipClass::kSize>>> records;
  for (const ShipClass& s : ships) {
    RecWriter<ShipClass::kSize> r(s.raw);
    r.str(0, 25, s.name);
    r.u8(25, static_cast<std::uint8_t>(s.ship_type));
    r.u8(26, static_cast<std::uint8_t>(s.role));
    r.str(27, 13, s.sprite1);
    r.str(40, 13, s.sprite2);
    r.str(53, 13, s.idbook_picture);
    r.u32(68, s.start_year);
    r.u32(72, s.end_year);
    r.u32(76, s.start_month);
    r.u32(80, s.end_month);
    r.i32(84, s.selection_weight);
    r.u32(88, s.render_size);
    r.u32(92, s.bow_wave_size);
    r.u32(96, s.category);
    r.u32(100, static_cast<std::uint32_t>(s.screw_sound));
    r.u32(104, s.fire_order);
    r.u32(108, s.hull_strength);
    r.fx(112, s.max_power);
    r.fx(116, s.submerged_power);
    r.u32(120, s.depth_charges);
    r.u32(124, s.dc_racks);
    r.u32(128, s.dc_throwers);
    r.flag(132, s.has_sonar);
    r.u32(136, s.main_gun_count);
    r.str(140, 8, s.main_gun_caliber_in);
    r.flag(148, s.gun_mounts[0]);
    r.flag(152, s.gun_mounts[1]);
    r.u32(156, s.gun_id);
    r.str(160, 8, s.top_speed_kts);
    r.str(168, 8, s.submerged_speed_kts);
    r.u32(176, s.length_ft);
    r.u32(180, s.beam_ft);
    r.u32(184, s.draft_ft);
    r.u32(188, s.visual_size);
    r.u32(192, s.deck_height_ft);
    r.u32(196, s.displacement_tons);
    r.u32(200, s.ballast_neutral);
    r.u32(204, s.ballast_max);
    r.fx(208, s.drag_cubic);
    r.fx(212, s.drag_cubic_submerged);
    r.fx(216, s.drag_linear);
    r.fx(220, s.drag_linear_submerged);
    r.fx(224, s.lateral_drag);
    r.fx(228, s.astern_drag_factor);
    r.fx(232, s.turn_factor);
    records.emplace_back(s.index_name, r.data());
  }
  return indexed(records);
}

std::vector<std::uint8_t> encode_subs_dat(std::span<const SubClass> subs) {
  std::vector<std::pair<std::string, std::array<std::uint8_t, SubClass::kSize>>> records;
  for (const SubClass& s : subs) {
    RecWriter<SubClass::kSize> r(s.raw);
    r.str(0, 10, s.name);
    r.str(10, 26, s.display_name);
    r.u32(36, s.class_index);
    r.u8(40, s.picture_frame);
    r.str(41, 14, s.sprite_silhouette);
    r.str(55, 17, s.sprite_profile);
    r.u32(72, s.intro_month);
    r.u32(76, s.intro_year);
    r.u32(80, s.end_month);
    r.u32(84, s.end_year);
    r.u32(100, s.range_nm);
    r.u32(104, s.range_at_kts);
    r.u32(116, s.bow_tubes);
    r.u32(120, s.stern_tubes);
    r.u32(124, s.bow_reloads);
    r.u32(128, s.stern_reloads);
    r.flag(132, s.modern_torpedoes);
    r.u32(136, s.deck_gun_count);
    r.u32(140, s.deck_gun_caliber_in);
    r.u32(144, s.single_gun_mount);
    r.u32(148, s.length_ft);
    r.u32(152, s.beam_ft);
    r.u32(156, s.draft_ft);
    r.u32(160, s.test_depth_ft);
    r.u32(164, s.periscope_depth_ft);
    r.u32(168, s.radar_depth_ft);
    r.u32(172, s.periscope_eye_ft);
    r.u32(176, s.bridge_eye_ft);
    r.u32(180, s.awash_depth_ft);
    r.u32(184, s.noise_factor_pct);
    r.str(188, 10, s.surface_speed_kts);
    r.str(198, 10, s.submerged_speed_kts);
    r.u32(208, s.displacement_surfaced);
    r.u32(212, s.displacement_submerged);
    r.u32(216, s.displacement_max);
    r.u32(220, s.diesel_hp);
    r.u32(224, s.motor_hp);
    r.fxs(228, s.drag_cubic);
    r.fxs(236, s.drag_linear);
    r.fx(244, s.lateral_drag);
    r.fx(248, s.astern_drag_factor);
    r.fx(252, s.turn_factor);
    r.fx(256, s.pitch_factor);
    r.u32(260, s.diesel_count);
    r.u32(264, s.motor_count);
    r.u32(268, s.fuel_capacity);
    r.u32(272, s.fuel_use);
    r.u32(276, s.deck_gun_rounds);
    r.i32s(280, s.power_surfaced);
    r.i32s(324, s.power_submerged);
    r.str(368, 984, s.description);
    for (std::size_t i = 0; i < SubClass::kCompartments; ++i) {
      const SubClass::Rect& c = s.compartment_rects[i];
      r.i32s(1352 + 16 * i, std::array<std::int32_t, 4>{c.x1, c.y1, c.x2, c.y2});
    }
    records.emplace_back(s.index_name, r.data());
  }
  return indexed(records);
}

std::vector<std::uint8_t> encode_aircraft_dat(std::span<const AircraftType> aircraft) {
  std::vector<std::pair<std::string, std::array<std::uint8_t, AircraftType::kSize>>> records;
  for (const AircraftType& a : aircraft) {
    RecWriter<AircraftType::kSize> r(a.raw);
    r.str(0, 25, a.name);
    r.str(25, 25, a.long_name);
    r.str(50, 25, a.name2);
    r.u8(75, static_cast<std::uint8_t>(a.role));
    r.str(76, 16, a.sprite);
    r.u32(92, a.intro_year);
    r.u32(96, a.end_year);
    r.u32(100, a.start_month);
    r.u32(104, a.end_month);
    r.u32(108, a.render_size);
    r.u32(116, a.aircraft_id);
    r.u32(120, a.cruise_speed_mph);
    r.u32(124, a.max_speed_mph);
    r.u32(128, a.ceiling_ft);
    r.u32(132, a.length_ft);
    r.u32(136, a.span_ft);
    r.u32(140, a.height_ft);
    r.u32(144, a.range_mi);
    r.u32(148, a.guns);
    r.u32(152, a.bombs);
    r.u32(156, a.torpedoes);
    r.i32(160, a.roll_rate);
    records.emplace_back(a.index_name, r.data());
  }
  return indexed(records);
}

std::vector<std::uint8_t> encode_weapons_dat(const Weapons& weapons) {
  Bytes out;
  out.u32(static_cast<std::uint32_t>(weapons.torpedoes.size()));
  for (const Torpedo& t : weapons.torpedoes) {
    RecWriter<Torpedo::kSize> r(t.raw);
    r.str(0, 10, t.short_name);
    r.str(10, 26, t.long_name);
    r.u32(36, t.id);
    r.u32(40, static_cast<std::uint32_t>(t.nation));
    r.u32(44, t.month);
    r.u32(48, t.year);
    r.u32(52, t.range_long_yd);
    r.u32(56, t.range_short_yd);
    r.u32(60, t.speed_low_kts);
    r.u32(64, t.speed_high_kts);
    r.u32(68, t.diameter_in);
    r.u32(72, t.warhead_lb);
    r.u32(76, t.length_ft);
    r.u32(80, t.beam_ft);
    r.u8(84, static_cast<std::uint8_t>(t.propulsion));
    r.u8(85, t.mk14_18_flag);
    r.u32(88, t.speed_settings);
    r.u32(92, t.displacement);
    r.i32s(96, t.reach_ft);
    r.fxs(104, t.power);
    r.fxs(112, t.drag_cubic);
    r.fxs(120, t.drag_linear);
    r.fxs(128, t.lateral_drag);
    r.fx(136, t.turn_factor);
    r.i32s(140, t.run_time_s);
    r.fxs(148, t.consumption);
    out.bytes(r.data());
  }
  out.u32(static_cast<std::uint32_t>(weapons.guns.size()));
  for (const Gun& g : weapons.guns) {
    RecWriter<Gun::kSize> r(g.raw);
    r.str(0, 32, g.long_name);
    r.u32(32, g.id);
    r.str(36, 12, g.caliber);
    r.u32(48, g.shell_caliber_in);
    r.u32(52, g.max_range_yd);
    r.u32(56, g.muzzle_velocity_fps);
    r.u32(60, g.reload_time_s);
    out.bytes(r.data());
  }
  return out.take();
}

std::vector<std::uint8_t> encode_carsub_dat(std::span<const CarsubBoat> boats) {
  Bytes out;
  for (const CarsubBoat& b : boats) {
    RecWriter<CarsubBoat::kSize> r(b.raw);
    r.str(0, 20, b.name);
    r.u32(20, b.hull_number);
    r.u32(24, b.avail_year);
    r.u32(28, b.avail_month);
    r.u32(32, b.end_year);
    r.u32(36, b.end_month);
    r.u8(40, b.class_index);
    out.bytes(r.data());
  }
  return out.take();
}

std::vector<std::uint8_t> encode_ijnships_dat(std::span<const IjnShip> ships) {
  Bytes out;
  for (const IjnShip& s : ships) {
    RecWriter<IjnShip::kSize> r(s.raw);
    r.str(0, 25, s.ship_name);
    r.str(25, 25, s.class_name);
    r.u32(50, s.commission_year);
    r.u32(54, s.commission_month);
    r.u32(58, s.end_year);
    r.u32(62, s.end_month);
    out.bytes(r.data());
  }
  return out.take();
}

std::vector<std::uint8_t> encode_cmdlist_dat(std::span<const Commander> commanders) {
  Bytes out;
  out.u32(static_cast<std::uint32_t>(commanders.size()));
  for (const Commander& c : commanders) {
    RecWriter<Commander::kSize> r(c.raw);
    r.str(0, 50, c.name);
    r.i32s(50, c.tonnage_by_month);
    out.bytes(r.data());
  }
  return out.take();
}

std::vector<std::uint8_t> encode_homeport_dat(std::span<const HomePort> ports) {
  Bytes out;
  for (const HomePort& p : ports) {
    RecWriter<HomePort::kSize> r(p.raw);
    r.str(0, 20, p.name);
    r.u32(0x14, p.flags);
    r.i32s(0x18, std::array<std::int32_t, 3>{p.open_date.month, p.open_date.day, p.open_date.year});
    r.i32s(0x24, std::array<std::int32_t, 3>{p.close_date.month, p.close_date.day, p.close_date.year});
    r.i32s(0x30, p.transfer_weight);
    for (std::size_t z = 0; z < HomePort::kZones; ++z) {
      r.i32s(0xe4 + z * kCampaignMonths * 4, p.zone_weight[z]);
    }
    r.i32s(0xef4, p.zone_distance_nm);
    r.i32(0xf44, static_cast<std::int32_t>(p.command));
    r.i32s(0xf48, p.map_point);
    r.i32s(0xf50, p.label_offset);
    out.bytes(r.data());
  }
  return out.take();
}

std::vector<std::uint8_t> encode_rmsg_dat(std::span<const std::string> templates) {
  Bytes out;
  std::uint32_t total = 0;
  for (const std::string& t : templates) {
    total += static_cast<std::uint32_t>(t.size() + 1);
  }
  out.u32(total);
  out.u32(static_cast<std::uint32_t>(templates.size()));
  for (const std::string& t : templates) {
    out.u32(static_cast<std::uint32_t>(t.size() + 1));
    out.fixed_string(t, t.size() + 1);
  }
  return out.take();
}

std::vector<std::uint8_t> encode_patrol_zones(std::span<const PatrolZone> zones) {
  Bytes out;
  out.u32(static_cast<std::uint32_t>(zones.size()));
  for (const PatrolZone& z : zones) {
    const std::size_t start = out.size();
    out.fixed_string(z.name, 31);
    out.fixed_string(z.world, 13);
    out.fixed_string(z.routes, 13);
    out.fixed_string(z.bases, 13);
    out.i32(z.first_year);
    out.i32(z.last_year);
    out.i32(z.first_month);
    out.i32(z.last_month);
    const auto i32s = [&out](const auto& values) {
      for (const std::int32_t v : values) {
        out.i32(v);
      }
    };
    const auto u8s = [&out](const auto& values) {
      for (const std::uint8_t v : values) {
        out.u8(v);
      }
    };
    i32s(z.year_weight);
    out.i32(static_cast<std::int32_t>(z.command));
    i32s(z.orders_rect);
    i32s(z.start_edges);
    i32s(z.air_start_edges);
    out.i32(z.sampan_density);
    i32s(z.fog_pct);
    for (const auto& m : z.weather_pct) {
      i32s(m);
    }
    for (const auto& m : z.convoy_size_pct) {
      u8s(m);
    }
    for (const auto& m : z.escort_pct) {
      u8s(m);
    }
    for (const auto& m : z.crew_pct) {
      u8s(m);
    }
    i32s(z.air_cover_pct);
    i32s(z.air_raid_pct);
    if (out.size() - start != PatrolZone::kRecordSize) {
      throw std::logic_error("patrol zone record size");
    }
  }
  return out.take();
}

std::vector<std::uint8_t> encode_routes(std::span<const ConvoyRoute> routes) {
  Bytes out;
  out.i32(static_cast<std::int32_t>(routes.size()));
  for (const ConvoyRoute& r : routes) {
    out.i32(static_cast<std::int32_t>(r.waypoints.size()));
  }
  for (const ConvoyRoute& r : routes) {
    for (const RouteDate* d : {&r.first, &r.last}) {
      out.i32(d->month);
      out.i32(d->day);
      out.i32(d->year);
    }
  }
  for (const ConvoyRoute& r : routes) {
    for (const std::int32_t f : r.frequency) {
      out.i32(f);
    }
  }
  for (const ConvoyRoute& r : routes) {
    for (const std::int32_t p : r.convoy_pct) {
      out.i32(p);
    }
  }
  for (const ConvoyRoute& r : routes) {
    out.u8(r.flags);
  }
  for (const ConvoyRoute& r : routes) {
    for (const RouteBox& b : r.waypoints) {
      for (const std::int32_t v : {b.x0, b.y0, b.z0, b.x1, b.y1, b.z1}) {
        out.i32(v);
      }
    }
  }
  return out.take();
}

std::vector<std::uint8_t> encode_bases(std::span<const BaseRecord> bases) {
  Bytes out;
  for (const BaseRecord& b : bases) {
    out.fixed_string(b.name, 15);
    for (const BasePoint* p : {&b.station, &b.harbor, &b.patrol}) {
      out.i32(p->x);
      out.i32(p->y);
      out.i32(p->z);
    }
    for (const BaseOwnerPeriod& o : b.owners) {
      out.i32(static_cast<std::int32_t>(o.side));
      out.i32(o.month);
      out.i32(o.day);
      out.i32(o.year);
    }
    out.u32(b.flags);
    for (const std::int32_t v : {b.anchored_size, b.anchored_size_cap, b.patrol_param_a, b.patrol_spawn_pct,
                                 b.patrol_param_b, b.pick_weight}) {
      out.i32(v);
    }
  }
  return out.take();
}

std::vector<std::uint8_t> encode_mel(const MelSample& sample) {
  Bytes out;
  write_mel_header(out, sample.header);
  out.bytes(sample.pcm);
  return out.take();
}

std::vector<std::uint8_t> encode_bnk(std::span<const BankEntry> entries) {
  Bytes out;
  for (const BankEntry& e : entries) {
    out.i32(e.sample_id);
    out.u32(e.size);
    out.u32(e.pointer);
    write_mel_header(out, e.header);
  }
  out.i32(-1);
  out.zeros(kBankEntrySize - 4);
  return out.take();
}

std::vector<std::uint8_t> encode_planets_ndx(const std::array<const PlanetTheory*, kPlanetBodies>& bodies) {
  Bytes out;
  std::uint32_t start = 0;
  for (const PlanetTheory* body : bodies) {
    for (std::size_t c = 0; c < kEphemerisCoords; ++c) {
      for (std::size_t p = 0; p < kEphemerisPowers; ++p) {
        const auto count = body == nullptr ? 0u : static_cast<std::uint32_t>(body->series[c][p].size());
        out.u16(static_cast<std::uint16_t>(start));
        out.u16(static_cast<std::uint16_t>(count));
        start += count;
      }
    }
  }
  return out.take();
}

std::vector<std::uint8_t> encode_planet_theory(const PlanetTheory& theory) {
  Bytes out;
  for (std::size_t c = 0; c < kEphemerisCoords; ++c) {
    for (std::size_t p = 0; p < kEphemerisPowers; ++p) {
      for (const EphemerisTerm& t : theory.series[c][p]) {
        f64(out, t.a);
        f64(out, t.b);
        f64(out, t.c);
      }
    }
  }
  return out.take();
}

}  // namespace sh::test
