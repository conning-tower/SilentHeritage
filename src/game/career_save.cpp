#include "game/career_save.h"

#include <algorithm>
#include <format>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "game/mission_rules.h"
#include "game/saves_options.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

constexpr std::size_t kHeaderSize = 1 + CareerRecord::kSize;  // 0x452
constexpr std::size_t kLogOffsetV5 = kHeaderSize + CareerSummary::kSize;  // 0x4c6
constexpr std::size_t kUsedOffset = 0;
constexpr std::size_t kRecordOffset = 4;
constexpr std::size_t kCountOffset = 4 + CareerRecord::kSize;  // 0x455
constexpr std::size_t kLogOffset = kCountOffset + 4;           // 0x459

// SH.EXE strings of `career_end_summary`.
namespace addr {
constexpr std::uint32_t kStatuses = 0x10eac0;   // char*[7] career status
constexpr std::uint32_t kMonths = 0x10e910;     // char*[12] "January"..
constexpr std::uint32_t kShipTypes = 0x10ea04;  // {char*, char*}[24] "Aircraft Carrier"..
constexpr std::uint32_t kSunkStatuses = 0x10eb10;  // char*[3] "Escaped"..
constexpr std::uint32_t kAwards = 0x10336c;     // char*[8] "Bronze Star"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;   // i32 1930
constexpr std::uint32_t kName = 0xf1090;        // "Name: "
constexpr std::uint32_t kStatus = 0xf10c3;      // "Status: "
constexpr std::uint32_t kStartDate = 0xf1110;   // "Start Date: "
constexpr std::uint32_t kEndDate = 0xf11a6;     // "End Date: "
constexpr std::uint32_t kSub = 0xf1252;         // "Sub: "
constexpr std::uint32_t kClass = 0xf1284;       // "Class: "
constexpr std::uint32_t kPatrols = 0xf12d0;     // "Patrols: "
constexpr std::uint32_t kShipsSunk = 0xf1306;   // "Ships sunk: "
constexpr std::uint32_t kTonnage = 0xf1357;     // "Tonnage: "
constexpr std::uint32_t kPilots = 0xf138d;      // "Pilots rescued: "
constexpr std::uint32_t kRecons = 0xf13e2;      // "Photo recons: "
constexpr std::uint32_t kScore = 0xf141d;       // "Score: "
constexpr std::uint32_t kAwardsTitle = 0xf146a;  // "Awards:\n\n"
constexpr std::uint32_t kLocation = 0xf1517;    // "Location"
constexpr std::uint32_t kShipType = 0xf1536;    // "Ship Type (CLASS)"
constexpr std::uint32_t kTonnageHead = 0xf155e;  // "Tonnage"
constexpr std::uint32_t kStatusHead = 0xf157c;  // "Status"
constexpr std::uint32_t kEnd = 0xf1780;         // "***END OF CAREER SUMMARY***"
}  // namespace addr

std::int32_t i32_at(std::span<const std::uint8_t> data, std::size_t offset) {
  return static_cast<std::int32_t>(read_u32le(data, offset));
}

void set_i32(std::span<std::uint8_t> data, std::size_t offset, std::int32_t v) {
  const auto u = static_cast<std::uint32_t>(v);
  for (std::size_t i = 0; i < 4; ++i) {
    data[offset + i] = static_cast<std::uint8_t>(u >> (8 * i));
  }
}

// The summary `career_list_rosters` makes up for a file before version 5.
CareerSummary old_summary(const CareerRecord& record) {
  CareerSummary s;
  s.preferred_command = -1;
  s.current_command = -1;
  s.patrols_since_transfer = record.patrols_from_port;
  s.raw = s.serialize();
  return s;
}

// The version rules of `career_list_rosters` on the record and summary.
void apply_version_rules(std::uint8_t version, CareerRecord& record, CareerSummary& summary) {
  if (version < 6) {
    summary.fog_pct = 0;
  }
  if (version < 2) {
    record.pilots_rescued = 0;
    record.photo_recons = 0;
    record.boats_lost = 0;
    record.raids_this_patrol = 0;
  }
}

// Reads the record and summary of a roster file, without the log.
RosterFile parse_header(std::span<const std::uint8_t> data, std::size_t& log_offset) {
  if (data.empty()) {
    throw DataError("empty roster file");
  }
  RosterFile f;
  f.version = data[0];
  f.career.record = CareerRecord::parse(data.subspan(1));
  if (f.version < 5) {
    f.career.summary = old_summary(f.career.record);
    log_offset = kHeaderSize;
  } else {
    f.career.summary = CareerSummary::parse(data.subspan(kHeaderSize));
    log_offset = kLogOffsetV5;
  }
  apply_version_rules(f.version, f.career.record, f.career.summary);
  return f;
}

std::vector<CareerLogEntry> parse_log(std::span<const std::uint8_t> data, std::size_t offset, std::int32_t count,
                                      std::size_t& end) {
  std::vector<CareerLogEntry> log;
  end = offset;
  for (std::int32_t i = 0; i < count && end + CareerLogEntry::kSize <= data.size(); ++i) {
    log.push_back(CareerLogEntry::parse(data.subspan(end, CareerLogEntry::kSize)));
    end += CareerLogEntry::kSize;
  }
  return log;
}

// The entries `career_save_log_entries` writes.
std::int32_t sunk_count(const std::vector<CareerLogEntry>& log) {
  return static_cast<std::int32_t>(
      std::ranges::count_if(log, [](const CareerLogEntry& e) { return e.event.status == SunkStatus::kSunk; }));
}

const HomePort* find_home_port(const GameTables& tables, std::string_view name) {
  for (const HomePort& p : tables.home_ports) {
    if (equals_ignore_case(p.name, name)) {
      return &p;
    }
  }
  return nullptr;
}

}  // namespace

std::string roster_file_name(std::int32_t slot) { return std::format("ROSTER{}.SAV", slot); }

std::string career_rules_file_name(std::int32_t slot) { return std::format("ROSTER{}.EXT", slot); }

std::optional<MissionRules> load_career_rules(const SaveDir& saves, std::int32_t slot) {
  const std::optional<std::vector<std::uint8_t>> data = saves.read(career_rules_file_name(slot));
  if (!data) {
    return std::nullopt;
  }
  return parse_mission_rules_config({reinterpret_cast<const char*>(data->data()), data->size()},
                                    MissionRules::extended());
}

RosterFile parse_roster(std::span<const std::uint8_t> data) {
  std::size_t offset = 0;
  RosterFile f = parse_header(data, offset);
  std::size_t end = offset;
  f.career.log = parse_log(data, offset, f.career.record.log_count, end);
  if (f.career.record.on_patrol != 0) {
    f.career.patrol_state.assign(data.begin() + static_cast<std::ptrdiff_t>(end), data.end());
    f.career.patrol_state_version = f.version;
  }
  return f;
}

std::vector<std::uint8_t> serialize_roster(const Career& career) {
  CareerRecord record = career.record;
  record.log_count = sunk_count(career.log);
  std::vector<std::uint8_t> out{kRosterVersion};
  const auto rec = record.serialize();
  out.insert(out.end(), rec.begin(), rec.end());
  const auto sum = career.summary.serialize();
  out.insert(out.end(), sum.begin(), sum.end());
  for (const CareerLogEntry& e : career.log) {
    if (e.event.status == SunkStatus::kSunk) {
      const auto bytes = e.serialize();
      out.insert(out.end(), bytes.begin(), bytes.end());
    }
  }
  if (record.on_patrol != 0) {
    out.insert(out.end(), career.patrol_state.begin(), career.patrol_state.end());
  }
  return out;
}

bool save_career(const SaveDir& saves, Career& career) {
  career.record.log_count = sunk_count(career.log);
  const std::string name = roster_file_name(career.record.roster_slot);
  // The original deletes the file it failed to write
  // (`career_delete_roster_file`); the atomic write leaves the last saved
  // roster whole, which is kept.
  if (!saves.write(name, serialize_roster(career))) {
    return false;
  }
  if (career.rules) {
    const std::string text = mission_rules_config(*career.rules);
    return saves.write(career_rules_file_name(career.record.roster_slot),
                       {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
  }
  return true;
}

std::optional<RosterFile> load_roster(const SaveDir& saves, std::int32_t slot) {
  const std::optional<std::vector<std::uint8_t>> data = saves.read(roster_file_name(slot));
  if (!data) {
    return std::nullopt;
  }
  try {
    return parse_roster(*data);
  } catch (const DataError&) {
    return std::nullopt;
  }
}

void delete_roster(const SaveDir& saves, std::int32_t slot) {
  saves.remove(roster_file_name(slot));
  saves.remove(career_rules_file_name(slot));
}

std::int32_t find_free_roster_slot(const SaveDir& saves) {
  std::int32_t slot = 0;
  for (; slot < kRosterSlots; ++slot) {
    const std::optional<std::vector<std::uint8_t>> data = saves.read(roster_file_name(slot));
    if (!data || data->empty()) {
      break;
    }
  }
  return std::min(slot, kRosterSlots - 1);
}

std::vector<RosterEntry> list_rosters(const SaveDir& saves) {
  std::vector<RosterEntry> entries;
  for (const std::string& file : saves.list(".SAV")) {
    // The pattern "ROSTER*.SAV".
    if (!file.starts_with("ROSTER")) {
      continue;
    }
    const std::optional<std::vector<std::uint8_t>> data = saves.read(file);
    if (!data) {
      continue;
    }
    try {
      std::size_t offset = 0;
      RosterFile f = parse_header(*data, offset);
      entries.push_back({file, std::move(f.career.record), std::move(f.career.summary)});
    } catch (const DataError&) {
      // A short file is skipped, as the original's failed reads.
    }
  }
  return entries;
}

Career career_from_roster_entry(const RosterEntry& entry, const GameTables& tables) {
  Career career;
  career.record = entry.record;
  career.summary = entry.summary;
  if (career.summary.current_command < 0) {
    if (const HomePort* port = find_home_port(tables, career.record.home_port)) {
      std::int32_t command = static_cast<std::int32_t>(port->command);
      // homeport_load_dat(-1): ComSubSoWesPac is ComSubsAsiatic before
      // April 1942.
      if (command == static_cast<std::int32_t>(SubCommand::kComSubSoWesPac) &&
          (career.record.year < 12 || (career.record.year == 12 && career.record.month < 3))) {
        command = static_cast<std::int32_t>(SubCommand::kComSubsAsiatic);
      }
      career.summary.current_command = command;
    }
  }
  return career;
}

std::vector<CareerLogEntry> load_career_log(const SaveDir& saves, const CareerRecord& record) {
  if (record.roster_slot < 0 || record.log_count < 1) {
    return {};
  }
  const std::optional<std::vector<std::uint8_t>> data = saves.read(roster_file_name(record.roster_slot));
  if (!data || data->empty()) {
    return {};
  }
  const std::size_t offset = (*data)[0] > 4 ? kLogOffsetV5 : kHeaderSize;
  std::size_t end = offset;
  return parse_log(*data, offset, record.log_count, end);
}

void load_roster_patrol_state(const SaveDir& saves, Career& career) {
  if (career.record.on_patrol == 0) {
    return;
  }
  if (std::optional<RosterFile> f = load_roster(saves, career.record.roster_slot)) {
    career.patrol_state = std::move(f->career.patrol_state);
    career.patrol_state_version = f->career.patrol_state_version;
  }
}

std::int32_t save_career_as(const SaveDir& saves, Career& career, std::string_view name, WatcomRandom& random) {
  career.record.seed = static_cast<std::uint32_t>(random.rand());
  Career copy = career;
  copy.log = load_career_log(saves, career.record);
  copy.record.roster_slot = find_free_roster_slot(saves);
  copy.record.name = std::string(name.substr(0, 20));
  save_career(saves, copy);
  return copy.record.roster_slot;
}

// ------------------------------------------------------------ best careers

BestCareersTable::BestCareersTable() {
  for (std::vector<std::uint8_t>& r : raw) {
    r.assign(kEntrySize, 0);
  }
}

std::optional<BestCareersTable> BestCareersTable::parse(std::span<const std::uint8_t> data) {
  if (data.empty()) {
    return std::nullopt;
  }
  const std::uint8_t version = data[0];
  const std::size_t slots = version < 3 ? kLogSlotsV2 : kLogSlots;
  const std::size_t size = kLogOffset + slots * CareerLogEntry::kSize;
  BestCareersTable t;
  std::size_t pos = 1;
  for (std::size_t i = 0; i < kEntries; ++i) {
    if (pos + size > data.size()) {
      return std::nullopt;
    }
    std::vector<std::uint8_t>& raw = t.raw[i];
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(pos), size, raw.begin());
    pos += size;
    if (version < 2) {
      std::fill_n(raw.begin() + static_cast<std::ptrdiff_t>(kRecordOffset + 0x120), 8, 0);
    }
    if (i32_at(raw, kUsedOffset) == 0) {
      continue;
    }
    BestCareerEntry e;
    e.record = CareerRecord::parse(std::span(raw).subspan(kRecordOffset, CareerRecord::kSize));
    const std::int32_t count = i32_at(raw, kCountOffset);
    const std::size_t n = std::min(static_cast<std::size_t>(std::max(count, 0)), slots);
    for (std::size_t k = 0; k < n; ++k) {
      e.log.push_back(
          CareerLogEntry::parse(std::span(raw).subspan(kLogOffset + k * CareerLogEntry::kSize, CareerLogEntry::kSize)));
    }
    t.entries[i] = std::move(e);
  }
  return t;
}

std::vector<std::uint8_t> BestCareersTable::serialize() const {
  std::vector<std::uint8_t> out{kVersion};
  for (std::size_t i = 0; i < kEntries; ++i) {
    std::vector<std::uint8_t> slot = raw[i];
    if (const std::optional<BestCareerEntry>& e = entries[i]) {
      if (i32_at(slot, kUsedOffset) == 0) {
        set_i32(slot, kUsedOffset, -1);
      }
      const auto rec = e->record.serialize();
      std::ranges::copy(rec, slot.begin() + kRecordOffset);
      set_i32(slot, kCountOffset, static_cast<std::int32_t>(e->log.size()));
      for (std::size_t k = 0; k < e->log.size() && k < kLogSlots; ++k) {
        const auto bytes = e->log[k].serialize();
        std::ranges::copy(bytes, slot.begin() + static_cast<std::ptrdiff_t>(kLogOffset + k * CareerLogEntry::kSize));
      }
    }
    out.insert(out.end(), slot.begin(), slot.end());
  }
  return out;
}

std::optional<BestCareersTable> BestCareersTable::load(const SaveDir& saves) {
  // Two attempts: an empty table is written when the file is missing.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (const std::optional<std::vector<std::uint8_t>> data = saves.read(kFileName)) {
      return parse(*data);
    }
    BestCareersTable().save(saves);
  }
  return std::nullopt;
}

bool BestCareersTable::save(const SaveDir& saves) const { return saves.write(kFileName, serialize()); }

std::optional<std::size_t> BestCareersTable::rank_of(std::int32_t score) const {
  if (score == 0) {
    return std::nullopt;
  }
  std::size_t rank = 0;
  while (rank < kEntries && entries[rank] && entries[rank]->record.score > score) {
    ++rank;
  }
  if (rank == kEntries) {
    return std::nullopt;
  }
  return rank;
}

void BestCareersTable::insert(std::size_t rank, BestCareerEntry entry) {
  for (std::size_t i = kEntries - 1; i > rank; --i) {
    entries[i] = std::move(entries[i - 1]);
    raw[i] = raw[i - 1];
  }
  if (entry.log.size() > kLogSlots) {
    entry.log.resize(kLogSlots);
  }
  set_i32(raw[rank], kUsedOffset, -1);
  entries[rank] = std::move(entry);
}

std::vector<std::string> best_careers_rows(const BestCareersTable& table, const Font& font) {
  std::vector<std::string> rows;
  for (const std::optional<BestCareerEntry>& e : table.entries) {
    if (!e) {
      continue;
    }
    std::string row = std::to_string(rows.size() + 1);
    ui::menu_pad_text_to_width(row, 0x2a, font);
    row += e->record.name;
    ui::menu_pad_text_to_width(row, 0x173, font);
    row += std::to_string(e->record.score);
    ui::menu_pad_text_to_width(row, 0x1be, font);
    row += std::to_string(e->record.tonnage);
    rows.push_back(std::move(row));
  }
  return rows;
}

std::string career_end_summary(const BestCareerEntry& entry, const ExeImage& exe, const Font& font) {
  const CareerRecord& c = entry.record;
  const auto s = [&](std::uint32_t address) { return std::string(exe.c_string(address)); };
  const auto month = [&](std::int32_t m) {
    return std::string(exe.string_pointer(addr::kMonths + static_cast<std::uint32_t>(m) * 4));
  };
  const std::int32_t base_year = exe.i32(addr::kBaseYear);
  const auto pad = [&](std::string& t, int width) { ui::menu_pad_text_to_width(t, width, font); };
  std::string t = s(addr::kName);
  pad(t, 0x50);
  t += c.name;
  pad(t, 0xfa);
  t += s(addr::kStatus);
  pad(t, 0x15e);
  // The status is read as a signed char.
  t += exe.string_pointer(addr::kStatuses +
                          static_cast<std::uint32_t>(static_cast<std::int8_t>(c.status)) * 4);
  t += "\n";
  t += s(addr::kStartDate);
  pad(t, 0x50);
  t += month(c.start_month) + " " + std::to_string(c.start_day) + ", " + std::to_string(c.start_year + base_year);
  pad(t, 0xfa);
  t += s(addr::kEndDate);
  pad(t, 0x15e);
  t += month(c.month) + " " + std::to_string(c.day) + ", " + std::to_string(c.year + base_year);
  t += "\n";
  t += s(addr::kSub);
  pad(t, 0x50);
  t += c.boat;
  pad(t, 0xfa);
  t += s(addr::kClass);
  pad(t, 0x15e);
  t += c.class_title;
  t += "\n";
  t += s(addr::kPatrols);
  pad(t, 0x50);
  t += std::to_string(c.patrols);
  pad(t, 0xfa);
  t += s(addr::kShipsSunk);
  pad(t, 0x15e);
  t += std::to_string(c.ships_sunk);
  t += "\n";
  t += s(addr::kTonnage);
  pad(t, 0x50);
  t += std::to_string(c.tonnage);
  pad(t, 0xfa);
  t += s(addr::kPilots);
  pad(t, 0x15e);
  t += std::to_string(c.pilots_rescued);
  t += "\n";
  t += s(addr::kRecons);
  pad(t, 0x50);
  t += std::to_string(c.photo_recons);
  pad(t, 0xfa);
  t += s(addr::kScore);
  pad(t, 0x15e);
  t += std::to_string(c.score);
  t += "\n\n";
  if (std::ranges::any_of(c.awards, [](std::int32_t n) { return n > 0; })) {
    t += s(addr::kAwardsTitle);
    for (std::size_t i = 0; i < kAwardCount; ++i) {
      if (c.awards[i] == 0) {
        continue;
      }
      t += exe.string_pointer(addr::kAwards + static_cast<std::uint32_t>(i) * 4);
      if (c.awards[i] > 1) {
        t += " (" + std::to_string(c.awards[i]) + ")";
      }
      t += "\n";
    }
    t += "\n";
  }
  if (!entry.log.empty()) {
    t += s(addr::kLocation);
    pad(t, 0x5a);
    t += s(addr::kShipType);
    pad(t, 0x14a);
    t += s(addr::kTonnageHead);
    pad(t, 0x19a);
    t += s(addr::kStatusHead);
    t += "\n\n";
    for (const CareerLogEntry& e : entry.log) {
      const PatrolLogEntry& log = e.event.log;
      // Only the ships: not the rescued pilots and photo recons.
      if (log.type >= PatrolLogEntry::kRescuedPilot) {
        continue;
      }
      t += std::format("{}-{:02}{}, {}-{:02}{}", log.latitude.degrees, log.latitude.minutes, log.latitude.hemisphere,
                       log.longitude.degrees, log.longitude.minutes, log.longitude.hemisphere);
      pad(t, 0x5a);
      t += exe.string_pointer(addr::kShipTypes + static_cast<std::uint32_t>(log.type) * 8);
      if (!log.name.empty()) {
        t += " (" + log.name + ")";
      }
      pad(t, 0x14a);
      t += std::to_string(log.tonnage);
      pad(t, 0x19a);
      t += exe.string_pointer(addr::kSunkStatuses + static_cast<std::uint32_t>(e.event.status) * 4);
      if (e.event.status == SunkStatus::kDamaged) {
        t += " (" + std::to_string(e.event.damage_pct) + "%)";
      }
      t += "\n";
    }
    t += "\n";
  }
  t += s(addr::kEnd);
  return t;
}

}  // namespace sh
