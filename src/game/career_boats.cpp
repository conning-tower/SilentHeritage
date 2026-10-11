#include "game/career_boats.h"

#include <cstdlib>
#include <vector>

#include "assets/exe_image.h"
#include "assets/game_exe.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "sim/clock.h"

namespace sh {

namespace {

namespace addr {
constexpr std::uint32_t kRandom = 0x10c4c8;      // char* "Random"
constexpr std::uint32_t kCommands = 0x10eb38;    // char*[4] "ComSubsAsiatic"..
constexpr std::uint32_t kCommander = 0xf1878;    // "Commander "
constexpr std::uint32_t kHullPrefix = 0xf1a50;   // "(SS-"
constexpr std::uint32_t kHullSuffix = 0xf1a81;   // ") "
}  // namespace addr

// The war window of the new-career dates (0x10e3a8..0x10e3bc), month
// 0-based, year since 1930.
constexpr std::int32_t kStartMonth = 11;
constexpr std::int32_t kStartDay = 7;
constexpr std::int32_t kStartYear = 11;
constexpr std::int32_t kEndMonth = 7;
constexpr std::int32_t kEndDay = 15;
constexpr std::int32_t kEndYear = 15;
// The date ComSubsAsiatic becomes ComSubSoWesPac (0x10e450..0x10e458):
// month 3, year 12, read by `campaign_month_index_abs` as given.
constexpr std::int32_t kSoWesPacMonth = 3;
constexpr std::int32_t kSoWesPacYear = 12;

// The command table 0x10c4d4 of the three choices.
constexpr std::array<std::int32_t, 3> kPreferredCommands = {-1, 1, 2};

// `campaign_month_index_abs` (0x1e463): `month` 1-based, `full_year` e.g.
// 1943.
std::int32_t month_index_abs(std::int32_t month, std::int32_t full_year) {
  return full_year == 1941 ? 0 : month + (full_year - 1942) * 12 + 1;
}

// The SUBS.DAT fields the class table reads that `SubClass` does not name.
std::int32_t sub_i32(const SubClass& sub, std::size_t offset) {
  return static_cast<std::int32_t>(read_u32le(sub.raw, offset));
}

// The career's month as the original computes it: the 0-based month passed
// as the 1-based one.
std::int32_t career_month_index(const CareerRecord& record) {
  return month_index_abs(record.month, record.year + kCalendarBaseYear);
}

// Returns CARSUB.DAT's two-digit year (43) as a full year (not in the
// original).
std::int32_t carsub_full_year(std::uint32_t year) {
  return static_cast<std::int32_t>(year < 100 ? 1900 + year : year);
}

// Returns the CARSUB indices of the boats of `c` commissioned by campaign
// month `now`, or all of the class's boats when none is (not in the
// original).
std::vector<std::int32_t> boats_in_service(const GameTables& tables, const CareerBoatClass& c, std::int32_t now) {
  std::vector<std::int32_t> boats;
  std::vector<std::int32_t> all;
  for (std::int32_t b = c.first_boat; b <= c.last_boat; ++b) {
    const CarsubBoat& boat = tables.carsub[static_cast<std::size_t>(b)];
    all.push_back(b);
    if (month_index_abs(static_cast<std::int32_t>(boat.avail_month), carsub_full_year(boat.avail_year)) <= now) {
      boats.push_back(b);
    }
  }
  return boats.empty() ? all : boats;
}

bool delivery_due(const EquipmentDelivery& e, bool supplied, const CareerRecord& record,
                  const CareerSummary& summary) {
  std::int32_t due = month_index_abs(e.month, e.year + kCalendarBaseYear);
  if (!supplied && summary.current_command != 1) {
    due += 3;
  }
  return due <= career_month_index(record);
}

}  // namespace

std::vector<CareerBoatClass> career_boat_classes(const GameTables& tables) {
  std::vector<CareerBoatClass> classes;
  const auto boat_count = static_cast<std::int32_t>(tables.carsub.size());
  for (const SubClass& sub : tables.subs) {
    const std::int32_t flag = sub_i32(sub, 0x58);
    if (flag == 0) {
      continue;
    }
    CareerBoatClass c;
    c.class_index = static_cast<std::int32_t>(sub.class_index);
    c.title = sub.display_name.substr(0, 24);
    c.name = sub.name.substr(0, 9);
    c.career_flag = flag;
    c.first_month = sub_i32(sub, 0x48);
    c.first_year = sub_i32(sub, 0x4c);
    c.last_month = sub_i32(sub, 0x5c);
    c.last_year = sub_i32(sub, 0x60);
    c.fuel_capacity = static_cast<std::int32_t>(sub.fuel_capacity);
    c.fuel_use = static_cast<std::int32_t>(sub.fuel_use);
    c.transit_speed = std::atoi(sub.surface_speed_kts.c_str());
    // The run of CARSUB boats of the class.
    std::int32_t first = 0;
    while (first < boat_count &&
           tables.carsub[static_cast<std::size_t>(first)].class_index != static_cast<std::uint32_t>(c.class_index)) {
      ++first;
    }
    std::int32_t end = first;
    while (end < boat_count &&
           tables.carsub[static_cast<std::size_t>(end)].class_index == static_cast<std::uint32_t>(c.class_index)) {
      ++end;
    }
    c.first_boat = first;
    c.last_boat = end - 1;
    classes.push_back(std::move(c));
  }
  return classes;
}

bool assign_new_boat(CareerRecord& record, const CareerSummary& summary, SubCommand home_command,
                     const GameTables& tables, WatcomRandom& random, const MissionRules& rules) {
  // carsub_assign_new_boat (0x1e124)
  const bool upgrade = record.patrols >= 1;
  // The class index is a signed char.
  const auto current = static_cast<std::int8_t>(record.class_index);
  if (upgrade && current == 8) {
    return false;
  }
  const std::vector<CareerBoatClass> classes = career_boat_classes(tables);
  const std::int32_t now = month_index_abs(record.month + 1, record.year + kCalendarBaseYear);
  std::size_t chosen = 0;
  bool found = false;
  for (std::size_t i = 0; i < classes.size(); ++i) {
    const CareerBoatClass& c = classes[i];
    std::int32_t first = month_index_abs(c.first_month, c.first_year);
    const std::int32_t last = month_index_abs(c.last_month, c.last_year);
    if (summary.current_command != 1 && current != 0) {
      first += 4;
    }
    std::int32_t flag = c.career_flag;
    if (home_command == SubCommand::kComSubPac && c.class_index == 0) {
      flag = 0;
    }
    if (flag == 0 || now < first || last < now || (upgrade && c.class_index <= current)) {
      continue;
    }
    chosen = i;
    found = true;
    break;
  }
  if (!found) {
    return false;
  }
  while (true) {
    const CareerBoatClass& c = classes[chosen];
    if (c.first_boat > c.last_boat) {
      throw DataError("CARSUB.DAT: no boat of class " + c.name);
    }
    record.class_index = static_cast<std::uint8_t>(c.class_index);
    record.class_name = c.name;
    record.class_title = c.title;
    record.fuel_capacity = c.fuel_capacity;
    record.fuel_use = c.fuel_use;
    record.transit_speed = c.transit_speed;
    // The boats in service are counted (with the year and month swapped, so
    // all are) and the count drawn, the draw dropped.
    std::int32_t in_service = 0;
    for (std::int32_t b = c.first_boat; b <= c.last_boat; ++b) {
      const CarsubBoat& boat = tables.carsub[static_cast<std::size_t>(b)];
      if (month_index_abs(static_cast<std::int32_t>(boat.avail_month),
                                   static_cast<std::int32_t>(boat.avail_year)) <= now) {
        ++in_service;
      }
    }
    std::int32_t pick = 0;
    if (rules.fix_obvious_errors) {
      // Not in the original: a boat in service at the date.
      const std::vector<std::int32_t> boats = boats_in_service(tables, c, now);
      pick = boats.at(static_cast<std::size_t>(random.random_int(static_cast<std::int32_t>(boats.size()))));
    } else {
      random.random_int(in_service);
      pick = c.first_boat + random.random_int(c.last_boat + 1 - c.first_boat);
    }
    const CarsubBoat& boat = tables.carsub[static_cast<std::size_t>(pick)];
    // "(SS-<hull>) <name>", from the parts 0xf1a50 and 0xf1a81.
    const ExeImage& exe = game_exe();
    const std::string name = std::string(exe.c_string(addr::kHullPrefix)) + std::to_string(boat.hull_number) +
                             std::string(exe.c_string(addr::kHullSuffix)) + boat.name;
    if (name != record.boat) {
      record.boat = name.substr(0, 29);
      return true;
    }
    if (rules.fix_obvious_errors) {
      // Not in the original: another draw in the same class, unless the
      // boat is the only one it can give.
      if (boats_in_service(tables, c, now).size() < 2) {
        return true;
      }
      continue;
    }
    // The same boat again: the original goes on with the table's first
    // class (its index variable then holds 0).
    chosen = 0;
  }
}

// ------------------------------------------------------------ new career

bool NewCareerChoice::date_in_war() const {
  // new_career_date_in_war (0x5adee)
  if (year > kEndYear || (year == kEndYear && month > kEndMonth) ||
      (year == kEndYear && month == kEndMonth && day > kEndDay)) {
    return false;
  }
  return year > kStartYear || (year == kStartYear && month >= kStartMonth);
}

void NewCareerChoice::next_year() {
  // new_career_next_year (0x5acfe): 1941..1945, wrapping.
  do {
    ++year;
    if (year >= 1946 - kCalendarBaseYear) {
      year = 1941 - kCalendarBaseYear;
    }
  } while (!date_in_war());
  day = year == kStartYear && month == kStartMonth ? kStartDay : 1;
}

void NewCareerChoice::next_month() {
  // new_career_next_month (0x5ad6c)
  do {
    month = month < 11 ? month + 1 : 0;
  } while (!date_in_war());
  day = year == kStartYear && month == kStartMonth ? kStartDay : 1;
}

std::int32_t NewCareerChoice::preferred_command() const {
  return kPreferredCommands[static_cast<std::size_t>(command)];
}

std::array<std::string, 3> new_career_command_names(const NewCareerChoice& choice, const ExeImage& exe) {
  // new_career_update_command_names (0x5ac7f): the 0-based month is read
  // as the 1-based one.
  const bool asiatic = month_index_abs(choice.month, choice.year + kCalendarBaseYear) <
                       month_index_abs(kSoWesPacMonth, kSoWesPacYear + kCalendarBaseYear);
  const std::uint32_t southwest = asiatic ? 0 : 2;
  return {std::string(exe.string_pointer(addr::kRandom)), std::string(exe.string_pointer(addr::kCommands + 4)),
          std::string(exe.string_pointer(addr::kCommands + southwest * 4))};
}

Career career_init_new(const NewCareerChoice& choice, std::uint32_t options, WatcomRandom& random,
                       std::int32_t stale_letter, const MissionRules& rules) {
  // career_init_new (0x5ae56)
  Career career;
  CareerRecord& r = career.record;
  r.name = choice.name.substr(0, 20);
  r.start_month = choice.month;
  r.start_day = choice.day + 1 + random.random_int(3);
  r.start_year = choice.year;
  r.status = CareerStatus::kInPort;
  r.patrols_since_new_boat = 1;
  r.month = r.start_month;
  r.year = r.start_year;
  r.day = r.start_day;
  r.seed = static_cast<std::uint32_t>(random.rand());
  const auto bits = [options](std::uint32_t mask, int shift) {
    return static_cast<std::int32_t>((options & mask) >> shift);
  };
  r.realism = {bits(0xf, 2),     bits(0x3f, 4),     bits(0xff, 7),    bits(0x3ff, 9),
               bits(0x7ff, 10),  bits(0xfff, 11),   bits(0x3fff, 13), bits(0x7fff, 14),
               bits(0x1fff, 12), bits(0xffff, 15),  bits(0x7f, 6),    bits(0x1ff, 8)};
  CareerSummary& s = career.summary;
  s.preferred_command = choice.preferred_command();
  s.current_command = -1;
  s.patrols_since_transfer = 0;
  s.transfer_offered = 0;
  s.same_zone_repeats = 0;
  r.equipment = equipment_for_new_career(r, s, stale_letter, rules);
  return career;
}

// ---------------------------------------------------------- new equipment

const std::array<EquipmentDelivery, 7>& equipment_deliveries() {
  // The table 0x10ca58 (0x28-byte records: bit, month, day, year, name,
  // text, ..., supplied at +0x24) with the dates its static initializer
  // copies from the date table 0x10e408 ({month, day, year}[], the first
  // five) and sets (the last two: the month in the table, the year 1943 /
  // 1944 less the base year 0x10e3a4).
  static const std::array<EquipmentDelivery, 7> kTable = [] {
    constexpr std::uint32_t kRecords = 0x10ca58;
    constexpr std::uint32_t kDates = 0x10e408;
    constexpr std::uint32_t kBaseYear = 0x10e3a4;
    constexpr std::array<std::uint32_t, 5> kDateIndex = {0, 1, 5, 3, 2};
    constexpr std::array<std::int32_t, 2> kTorpedoYears = {1943, 1944};
    const ExeImage& exe = game_exe();
    std::array<EquipmentDelivery, 7> table;
    for (std::uint32_t i = 0; i < table.size(); ++i) {
      const std::uint32_t r = kRecords + 0x28 * i;
      EquipmentDelivery& e = table[i];
      e.bit = exe.u32(r);
      if (i < kDateIndex.size()) {
        e.month = exe.i32(kDates + 12 * kDateIndex[i]);
        e.year = exe.i32(kDates + 12 * kDateIndex[i] + 8);
      } else {
        e.month = exe.i32(r + 4);
        e.year = kTorpedoYears[i - kDateIndex.size()] - exe.i32(kBaseYear);
      }
      e.name = exe.u32(r + 0x10);
      e.text = exe.u32(r + 0x14);
      e.supplied = exe.u32(r + 0x24) != 0;
    }
    return table;
  }();
  return kTable;
}

bool equipment_due(std::size_t index, const CareerRecord& record, const CareerSummary& summary) {
  const EquipmentDelivery& e = equipment_deliveries().at(index);
  return delivery_due(e, e.supplied, record, summary);
}

std::int32_t deliver_next_equipment(CareerRecord& record, const CareerSummary& summary) {
  const auto& table = equipment_deliveries();
  for (std::size_t i = 0; i < table.size(); ++i) {
    if ((table[i].bit & record.equipment) != 0 || !equipment_due(i, record, summary)) {
      continue;
    }
    record.equipment |= table[i].bit;
    return static_cast<std::int32_t>(i);
  }
  return -1;
}

std::uint32_t equipment_for_new_career(const CareerRecord& record, const CareerSummary& summary,
                                       std::int32_t stale_letter, const MissionRules& rules) {
  // 0x5b990: the supply flag of record `stale_letter` for every record; the
  // flag read past the table (index 7) is -10.
  const auto& table = equipment_deliveries();
  const bool supplied = stale_letter < 0 || stale_letter >= static_cast<std::int32_t>(table.size()) ||
                        table[static_cast<std::size_t>(stale_letter)].supplied;
  std::uint32_t bits = record.equipment;
  for (const EquipmentDelivery& e : table) {
    if (delivery_due(e, rules.fix_obvious_errors ? e.supplied : supplied, record, summary)) {
      bits |= e.bit;
    }
  }
  return bits;
}

std::string career_commander_title(const CareerRecord& record, const ExeImage& exe) {
  return std::string(exe.c_string(addr::kCommander)) + record.name;
}

}  // namespace sh
