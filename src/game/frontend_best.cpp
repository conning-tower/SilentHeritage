#include "game/frontend_best.h"

#include <algorithm>

#include "core/byte_reader.h"
#include "game/saves_options.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

constexpr std::size_t kNameSize = 21;
constexpr std::size_t kResultsSize = 0x11c;
// Version 1 records lack the pilots and photo recons.
constexpr std::size_t kResultsSizeV1 = 0x114;
constexpr std::size_t kSunkRecordSize = 0x53;
constexpr std::size_t kSubSize = 100;
constexpr std::size_t kClassSize = 30;
constexpr std::size_t kAreaSize = 40;
constexpr std::size_t kSunkNameSize = 25;

// Appends little-endian fields to a buffer.
class Writer {
 public:
  explicit Writer(std::vector<std::uint8_t>& out) : out_(out) {}
  void u8(std::uint8_t v) { out_.push_back(v); }
  void i32(std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int shift = 0; shift < 32; shift += 8) {
      out_.push_back(static_cast<std::uint8_t>(u >> shift));
    }
  }
  // A NUL-padded `n`-byte string field (truncated to n - 1 characters).
  void str(const std::string& s, std::size_t n) {
    const std::size_t len = std::min(s.size(), n - 1);
    out_.insert(out_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
    out_.insert(out_.end(), n - len, 0);
  }
  void zeros(std::size_t n) { out_.insert(out_.end(), n, 0); }

 private:
  std::vector<std::uint8_t>& out_;
};

// A latitude or longitude of the results record: 13 bytes.
LogAngle read_record_angle(ByteReader& r) {
  LogAngle a{.degrees = r.i32(), .minutes = r.i32(), .hemisphere = static_cast<char>(r.u8())};
  r.skip(4);
  return a;
}

// A latitude or longitude of a sunk-list record: 9 bytes.
LogAngle read_angle(ByteReader& r) {
  return {.degrees = r.i32(), .minutes = r.i32(), .hemisphere = static_cast<char>(r.u8())};
}

void write_angle(Writer& w, const LogAngle& a) {
  w.i32(a.degrees);
  w.i32(a.minutes);
  w.u8(static_cast<std::uint8_t>(a.hemisphere));
}

SunkListEntry read_sunk(ByteReader r) {
  SunkListEntry e;
  r.skip(4);  // the stale next pointer
  e.log.day = r.i32();
  e.log.month = r.i32();
  e.log.year = r.i32();
  e.log.hour = r.i32();
  e.log.minute = r.i32();
  e.log.latitude = read_angle(r);
  e.log.longitude = read_angle(r);
  e.log.type = r.i32();
  e.log.name = r.fixed_string(kSunkNameSize);
  e.log.tonnage = r.i32();
  e.status = static_cast<SunkStatus>(r.i32());
  e.damage_pct = r.i32();
  return e;
}

void write_sunk(Writer& w, const SunkListEntry& e) {
  w.i32(0);
  w.i32(e.log.day);
  w.i32(e.log.month);
  w.i32(e.log.year);
  w.i32(e.log.hour);
  w.i32(e.log.minute);
  write_angle(w, e.log.latitude);
  write_angle(w, e.log.longitude);
  w.i32(e.log.type);
  w.str(e.log.name, kSunkNameSize);
  w.i32(e.log.tonnage);
  w.i32(static_cast<std::int32_t>(e.status));
  w.i32(e.damage_pct);
}

MissionResults read_results(ByteReader r, bool has_tasks) {
  MissionResults m;
  m.month = r.i32();
  m.day = r.i32();
  m.year = r.i32();
  m.hour = r.i32();
  m.minute = r.i32();
  m.sub = r.fixed_string(kSubSize);
  m.class_name = r.fixed_string(kClassSize);
  m.fate = static_cast<BoatFate>(r.i32());
  m.patrol_area = r.fixed_string(kAreaSize);
  m.latitude = read_record_angle(r);
  m.longitude = read_record_angle(r);
  m.ships_sunk = r.i32();
  m.tonnage = r.i32();
  m.score = r.i32();
  m.crew = r.i32();
  m.hull_damage_pct = r.i32();
  m.system_damage_pct = r.i32();
  m.minutes_elapsed = r.i32();
  m.fuel = r.i32();
  m.boat_lost = r.i32();
  m.warships_sunk = r.i32();
  m.sampans_sunk = r.i32();
  m.difficulty = r.i32();
  m.realism_pct = r.i32();
  r.skip(4);  // the stale sunk-list pointer
  if (has_tasks) {
    m.pilots_rescued = r.i32();
    m.photo_recons = r.i32();
  }
  return m;
}

void write_results(Writer& w, const MissionResults& m) {
  w.i32(m.month);
  w.i32(m.day);
  w.i32(m.year);
  w.i32(m.hour);
  w.i32(m.minute);
  w.str(m.sub, kSubSize);
  w.str(m.class_name, kClassSize);
  w.i32(static_cast<std::int32_t>(m.fate));
  w.str(m.patrol_area, kAreaSize);
  write_angle(w, m.latitude);
  w.zeros(4);
  write_angle(w, m.longitude);
  w.zeros(4);
  for (const std::int32_t v : {m.ships_sunk, m.tonnage, m.score, m.crew, m.hull_damage_pct, m.system_damage_pct,
                               m.minutes_elapsed, m.fuel, m.boat_lost, m.warships_sunk, m.sampans_sunk, m.difficulty,
                               m.realism_pct, 0, m.pilots_rescued, m.photo_recons}) {
    w.i32(v);
  }
}

}  // namespace

std::optional<BestMissionsTable> BestMissionsTable::parse(std::span<const std::uint8_t> data) {
  // best_missions_load (0xa6eae): any short read discards the table.
  if (data.empty()) {
    return std::nullopt;
  }
  const bool has_tasks = data[0] >= 2;
  const std::size_t results_size = has_tasks ? kResultsSize : kResultsSizeV1;
  const std::size_t entry_size = 4 + kNameSize + results_size + 4 + kSunkSlots * kSunkRecordSize;
  if (data.size() < 1 + kEntries * entry_size) {
    return std::nullopt;
  }
  BestMissionsTable table;
  ByteReader r(data, kFileName);
  r.skip(1);
  for (std::optional<BestMissionEntry>& slot : table.entries) {
    ByteReader e = r.sub(entry_size);
    if (e.i32() == 0) {
      continue;
    }
    BestMissionEntry entry;
    entry.name = e.fixed_string(kNameSize);
    entry.results = read_results(e.sub(results_size), has_tasks);
    const std::int32_t count = e.i32();
    for (std::int32_t i = 0; i < std::min<std::int32_t>(count, static_cast<std::int32_t>(kSunkSlots)); ++i) {
      entry.results.sunk.push_back(read_sunk(e.sub(kSunkRecordSize)));
    }
    slot = std::move(entry);
  }
  return table;
}

std::vector<std::uint8_t> BestMissionsTable::serialize() const {
  std::vector<std::uint8_t> out;
  out.reserve(1 + kEntries * kEntrySize);
  Writer w(out);
  w.u8(kVersion);
  for (const std::optional<BestMissionEntry>& slot : entries) {
    if (!slot) {
      w.zeros(kEntrySize);
      continue;
    }
    w.i32(-1);
    w.str(slot->name, kNameSize);
    write_results(w, slot->results);
    const std::size_t count = std::min(slot->results.sunk.size(), kSunkSlots);
    w.i32(static_cast<std::int32_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
      write_sunk(w, slot->results.sunk[i]);
    }
    w.zeros((kSunkSlots - count) * kSunkRecordSize);
  }
  return out;
}

std::optional<BestMissionsTable> BestMissionsTable::load(const SaveDir& saves) {
  // Two tries: a missing file is created empty, then read again.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (const std::optional<std::vector<std::uint8_t>> data = saves.read(kFileName)) {
      return parse(*data);
    }
    if (attempt == 0) {
      BestMissionsTable{}.save(saves);
    }
  }
  return std::nullopt;
}

bool BestMissionsTable::save(const SaveDir& saves) const { return saves.write(kFileName, serialize()); }

std::optional<std::size_t> BestMissionsTable::rank_of(std::int32_t score) const {
  for (std::size_t i = 0; i < kEntries; ++i) {
    if (!entries[i] || score >= entries[i]->results.score) {
      return i;
    }
  }
  return std::nullopt;
}

void BestMissionsTable::insert(std::size_t rank, BestMissionEntry entry) {
  std::move_backward(entries.begin() + static_cast<std::ptrdiff_t>(rank), entries.end() - 1, entries.end());
  if (entry.results.sunk.size() > kSunkSlots) {
    entry.results.sunk.resize(kSunkSlots);
  }
  entries[rank] = std::move(entry);
}

std::vector<std::string> best_missions_rows(const BestMissionsTable& table, const Font& font) {
  // best_missions_build_list (0xa6baa) with text_pad_to_width (0xa713d),
  // which measures the whole row.
  std::vector<std::string> rows;
  for (const std::optional<BestMissionEntry>& e : table.entries) {
    if (!e) {
      continue;
    }
    std::string row = std::to_string(rows.size() + 1);
    ui::menu_pad_text_to_width(row, 0x2a, font);
    row += e->name;
    ui::menu_pad_text_to_width(row, 0x173, font);
    row += std::to_string(e->results.score);
    ui::menu_pad_text_to_width(row, 0x1be, font);
    row += std::to_string(e->results.tonnage);
    rows.push_back(std::move(row));
  }
  return rows;
}

}  // namespace sh
