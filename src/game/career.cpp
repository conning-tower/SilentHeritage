#include "game/career.h"

#include <algorithm>

#include "core/byte_reader.h"

namespace sh {

namespace {

// The fields are written over the bytes they were read from: the strings as
// `strcpy` writes them (the text and its NUL, the stale bytes after it
// kept), the numbers little-endian.
template <std::size_t N>
void put_i32(std::array<std::uint8_t, N>& raw, std::size_t offset, std::int32_t v) {
  const auto u = static_cast<std::uint32_t>(v);
  for (std::size_t i = 0; i < 4; ++i) {
    raw[offset + i] = static_cast<std::uint8_t>(u >> (8 * i));
  }
}

template <std::size_t N>
void put_u16(std::array<std::uint8_t, N>& raw, std::size_t offset, std::uint16_t v) {
  raw[offset] = static_cast<std::uint8_t>(v);
  raw[offset + 1] = static_cast<std::uint8_t>(v >> 8);
}

// A string field of `size` bytes; the text is cut to `size - 1`.
template <std::size_t N>
void put_str(std::array<std::uint8_t, N>& raw, std::size_t offset, std::size_t size, const std::string& s) {
  const std::size_t len = std::min(s.size(), size - 1);
  std::copy_n(s.begin(), len, raw.begin() + static_cast<std::ptrdiff_t>(offset));
  raw[offset + len] = 0;
}

std::int32_t get_i32(ByteReader& r, std::size_t offset) {
  r.seek(offset);
  return r.i32();
}

std::string get_str(ByteReader& r, std::size_t offset, std::size_t size) {
  r.seek(offset);
  return r.fixed_string(size);
}

// A latitude or longitude of the 0x53-byte record: 9 bytes.
LogAngle read_angle(ByteReader& r) {
  return {.degrees = r.i32(), .minutes = r.i32(), .hemisphere = static_cast<char>(r.u8())};
}

template <std::size_t N>
void put_angle(std::array<std::uint8_t, N>& raw, std::size_t offset, const LogAngle& a) {
  put_i32(raw, offset, a.degrees);
  put_i32(raw, offset + 4, a.minutes);
  raw[offset + 8] = static_cast<std::uint8_t>(a.hemisphere);
}

}  // namespace

CareerRecord CareerRecord::parse(std::span<const std::uint8_t> data) {
  ByteReader r(data.first(std::min(data.size(), kSize)), "career record");
  if (data.size() < kSize) {
    r.fail("short career record");
  }
  CareerRecord c;
  std::copy_n(data.begin(), kSize, c.raw.begin());
  c.name = get_str(r, 0x00, 0x15);
  c.start_month = get_i32(r, 0x15);
  c.start_day = get_i32(r, 0x19);
  c.start_year = get_i32(r, 0x1d);
  c.month = get_i32(r, 0x21);
  c.day = get_i32(r, 0x25);
  c.year = get_i32(r, 0x29);
  c.hour = get_i32(r, 0x2d);
  c.minute = get_i32(r, 0x31);
  c.second = get_i32(r, 0x35);
  c.status = static_cast<CareerStatus>(c.raw[0x39]);
  c.roster_slot = get_i32(r, 0x3a);
  c.boat = get_str(r, 0x3e, 30);
  c.class_index = c.raw[0x5c];
  c.class_name = get_str(r, 0x5d, 10);
  c.class_title = get_str(r, 0x67, 25);
  c.fuel_capacity = get_i32(r, 0x80);
  c.fuel_use = get_i32(r, 0x84);
  c.transit_speed = get_i32(r, 0x88);
  c.last_patrol_zone = get_i32(r, 0x8c);
  c.equipment = static_cast<std::uint32_t>(get_i32(r, 0x90));
  c.rank = get_i32(r, 0x94);
  c.home_port = get_str(r, 0x98, 20);
  c.location = get_str(r, 0xac, 20);
  c.patrols = get_i32(r, 0xc0);
  c.promotion_patrols = get_i32(r, 0xc4);
  c.high_realism_patrols = get_i32(r, 0xc8);
  c.poor_patrol_streak = get_i32(r, 0xcc);
  c.fuel_failures = get_i32(r, 0xd0);
  c.patrols_from_port = get_i32(r, 0xd4);
  c.tonnage = get_i32(r, 0xd8);
  c.ships_sunk = get_i32(r, 0xdc);
  c.score = get_i32(r, 0xe0);
  c.unread_score = get_i32(r, 0xe4);
  for (std::size_t i = 0; i < kAwardCount; ++i) {
    c.awards[i] = get_i32(r, 0xe8 + 4 * i);
  }
  c.transit_hours = get_i32(r, 0x108);
  c.sunk_since_bronze_star = get_i32(r, 0x10c);
  c.nuc_streak = get_i32(r, 0x110);
  c.puc_streak = get_i32(r, 0x114);
  c.patrols_since_new_boat = get_i32(r, 0x118);
  c.seed = static_cast<std::uint32_t>(get_i32(r, 0x11c));
  c.pilots_rescued = get_i32(r, 0x120);
  c.photo_recons = get_i32(r, 0x124);
  r.seek(0x128);
  c.raids_this_patrol = r.u16();
  c.boats_lost = r.u16();
  for (std::size_t i = 0; i < kCareerRealismCount; ++i) {
    c.realism[i] = get_i32(r, 0x12c + 4 * i);
  }
  c.on_patrol = get_i32(r, 0x15c);
  std::copy_n(data.begin() + 0x160, kWorldFilesSize, c.world_files.begin());
  c.weather_days_left = get_i32(r, 0x431);
  c.weather = get_i32(r, 0x435);
  c.sea_state = get_i32(r, 0x439);
  c.daily_fog = get_i32(r, 0x43d);
  c.thermal_layer_ft = get_i32(r, 0x441);
  c.sea_picture = get_i32(r, 0x445);
  c.log_count = get_i32(r, 0x449);
  c.patrol_log_count = get_i32(r, 0x44d);
  return c;
}

std::array<std::uint8_t, CareerRecord::kSize> CareerRecord::serialize() const {
  std::array<std::uint8_t, kSize> out = raw;
  put_str(out, 0x00, 0x15, name);
  put_i32(out, 0x15, start_month);
  put_i32(out, 0x19, start_day);
  put_i32(out, 0x1d, start_year);
  put_i32(out, 0x21, month);
  put_i32(out, 0x25, day);
  put_i32(out, 0x29, year);
  put_i32(out, 0x2d, hour);
  put_i32(out, 0x31, minute);
  put_i32(out, 0x35, second);
  out[0x39] = static_cast<std::uint8_t>(status);
  put_i32(out, 0x3a, roster_slot);
  put_str(out, 0x3e, 30, boat);
  out[0x5c] = class_index;
  put_str(out, 0x5d, 10, class_name);
  put_str(out, 0x67, 25, class_title);
  put_i32(out, 0x80, fuel_capacity);
  put_i32(out, 0x84, fuel_use);
  put_i32(out, 0x88, transit_speed);
  put_i32(out, 0x8c, last_patrol_zone);
  put_i32(out, 0x90, static_cast<std::int32_t>(equipment));
  put_i32(out, 0x94, rank);
  put_str(out, 0x98, 20, home_port);
  put_str(out, 0xac, 20, location);
  put_i32(out, 0xc0, patrols);
  put_i32(out, 0xc4, promotion_patrols);
  put_i32(out, 0xc8, high_realism_patrols);
  put_i32(out, 0xcc, poor_patrol_streak);
  put_i32(out, 0xd0, fuel_failures);
  put_i32(out, 0xd4, patrols_from_port);
  put_i32(out, 0xd8, tonnage);
  put_i32(out, 0xdc, ships_sunk);
  put_i32(out, 0xe0, score);
  put_i32(out, 0xe4, unread_score);
  for (std::size_t i = 0; i < kAwardCount; ++i) {
    put_i32(out, 0xe8 + 4 * i, awards[i]);
  }
  put_i32(out, 0x108, transit_hours);
  put_i32(out, 0x10c, sunk_since_bronze_star);
  put_i32(out, 0x110, nuc_streak);
  put_i32(out, 0x114, puc_streak);
  put_i32(out, 0x118, patrols_since_new_boat);
  put_i32(out, 0x11c, static_cast<std::int32_t>(seed));
  put_i32(out, 0x120, pilots_rescued);
  put_i32(out, 0x124, photo_recons);
  put_u16(out, 0x128, raids_this_patrol);
  put_u16(out, 0x12a, boats_lost);
  for (std::size_t i = 0; i < kCareerRealismCount; ++i) {
    put_i32(out, 0x12c + 4 * i, realism[i]);
  }
  put_i32(out, 0x15c, on_patrol);
  std::ranges::copy(world_files, out.begin() + 0x160);
  put_i32(out, 0x431, weather_days_left);
  put_i32(out, 0x435, weather);
  put_i32(out, 0x439, sea_state);
  put_i32(out, 0x43d, daily_fog);
  put_i32(out, 0x441, thermal_layer_ft);
  put_i32(out, 0x445, sea_picture);
  put_i32(out, 0x449, log_count);
  put_i32(out, 0x44d, patrol_log_count);
  return out;
}

CareerSummary CareerSummary::parse(std::span<const std::uint8_t> data) {
  ByteReader r(data.first(std::min(data.size(), kSize)), "career summary");
  if (data.size() < kSize) {
    r.fail("short career summary");
  }
  CareerSummary s;
  std::copy_n(data.begin(), kSize, s.raw.begin());
  s.preferred_command = get_i32(r, 0x00);
  s.current_command = get_i32(r, 0x04);
  s.patrols_since_transfer = get_i32(r, 0x08);
  s.transfer_offered = get_i32(r, 0x0c);
  r.seek(0x10);
  s.same_zone_repeats = r.u16();
  s.fog_pct = get_i32(r, 0x12);
  return s;
}

std::array<std::uint8_t, CareerSummary::kSize> CareerSummary::serialize() const {
  std::array<std::uint8_t, kSize> out = raw;
  put_i32(out, 0x00, preferred_command);
  put_i32(out, 0x04, current_command);
  put_i32(out, 0x08, patrols_since_transfer);
  put_i32(out, 0x0c, transfer_offered);
  put_u16(out, 0x10, same_zone_repeats);
  put_i32(out, 0x12, fog_pct);
  return out;
}

CareerLogEntry CareerLogEntry::parse(std::span<const std::uint8_t> data) {
  ByteReader r(data.first(std::min(data.size(), kSize)), "career log entry");
  if (data.size() < kSize) {
    r.fail("short career log entry");
  }
  CareerLogEntry e;
  std::copy_n(data.begin(), kSize, e.raw.begin());
  r.seek(4);  // the stale next pointer
  PatrolLogEntry& log = e.event.log;
  log.day = r.i32();
  log.month = r.i32();
  log.year = r.i32();
  log.hour = r.i32();
  log.minute = r.i32();
  log.latitude = read_angle(r);
  log.longitude = read_angle(r);
  log.type = r.i32();
  log.name = r.fixed_string(25);
  log.tonnage = r.i32();
  e.event.status = static_cast<SunkStatus>(r.i32());
  e.event.damage_pct = r.i32();
  return e;
}

CareerLogEntry CareerLogEntry::from(const SunkListEntry& event) {
  CareerLogEntry e;
  e.event = event;
  e.raw = e.serialize();
  return e;
}

std::array<std::uint8_t, CareerLogEntry::kSize> CareerLogEntry::serialize() const {
  std::array<std::uint8_t, kSize> out = raw;
  const PatrolLogEntry& log = event.log;
  put_i32(out, 0x04, log.day);
  put_i32(out, 0x08, log.month);
  put_i32(out, 0x0c, log.year);
  put_i32(out, 0x10, log.hour);
  put_i32(out, 0x14, log.minute);
  put_angle(out, 0x18, log.latitude);
  put_angle(out, 0x21, log.longitude);
  put_i32(out, 0x2a, log.type);
  put_str(out, 0x2e, 25, log.name);
  put_i32(out, 0x47, log.tonnage);
  put_i32(out, 0x4b, static_cast<std::int32_t>(event.status));
  put_i32(out, 0x4f, event.damage_pct);
  return out;
}

}  // namespace sh
