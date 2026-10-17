#include "game/torpedo_load.h"

#include <algorithm>
#include <span>
#include <string>

#include "core/error.h"

namespace sh {

namespace {

const Torpedo& find_torpedo(const GameTables& tables, std::string_view name) {
  const Torpedo* t = tables.find_torpedo(name);
  if (t == nullptr) {
    throw DataError("WEAPONS.DAT: no torpedo " + std::string(name));
  }
  return *t;
}

// Returns the first `count` entries of `racks` (the class's rack size).
std::span<std::int32_t> racks_of(std::array<std::int32_t, TorpedoLoad::kRackCapacity>& racks, std::uint32_t count) {
  return std::span<std::int32_t>(racks).first(std::min<std::size_t>(count, racks.size()));
}

// The Mark 27's power (per mille of the 18-1's), for 12 kn in the motion
// model, and run countdown, which burns the 18-1's 1.2 a second: 741 s,
// 5000 yd (tests/torpedo_types_test.cpp).
constexpr std::int64_t kMark27Power = 280;
constexpr std::int32_t kMark27RunTime = 889;

// Returns whether `t` is in service at `year` (since 1930) / `month`, with
// the type table's comparison of its month field against the 0-based month.
bool in_service(const Torpedo& t, std::int32_t year, std::int32_t month) {
  const std::int32_t t_year = static_cast<std::int32_t>(t.year) - kScenarioYearBase;
  return t_year <= year && (t_year != year || static_cast<std::int32_t>(t.month) <= month);
}

// Selects the type of the first loaded tube that the table lists (else
// keeps the selection).
void select_first_tube_type(TorpedoTypes& table, const TorpedoLoad& load, const SubClass& sub) {
  const std::size_t tubes = std::min<std::size_t>(sub.bow_tubes + sub.stern_tubes, load.tubes.size());
  for (std::size_t i = 0; i < tubes; ++i) {
    const std::int32_t id = load.tubes[i];
    if (id <= 0) {
      continue;
    }
    const auto it = std::find_if(table.types.begin(), table.types.end(),
                                 [id](const TorpedoTypeChoice& c) { return c.id == id; });
    if (it != table.types.end()) {
      table.selected = static_cast<std::int32_t>(it - table.types.begin());
      break;
    }
  }
}

// The straight runners from the oldest to the newest.
constexpr std::array<std::string_view, 4> kStraightRunners{"MARK10", "MARK14", "MARK18-1", "MARK18-2"};

// The type table of the torpedo rules: the types carried in id order,
// then the straight runners not carried (unavailable) to fill its three
// entries.
TorpedoTypes carried_types_table(const TorpedoLoad& load, const SubClass& sub, const GameTables& tables) {
  TorpedoTypes table;
  std::vector<const Torpedo*> entries;
  for (const std::uint32_t id : carried_torpedo_types(load, sub)) {
    if (const Torpedo* t = tables.find_torpedo_by_id(id); t != nullptr && entries.size() < table.types.size()) {
      entries.push_back(t);
    }
  }
  const std::size_t carried = entries.size();
  for (const std::string_view name : kStraightRunners) {
    const Torpedo& t = find_torpedo(tables, name);
    if (entries.size() < table.types.size() && std::ranges::find(entries, &t) == entries.end()) {
      entries.push_back(&t);
    }
  }
  for (std::size_t i = 0; i < table.types.size(); ++i) {
    table.types[i] = {entries[i]->short_name, static_cast<std::int32_t>(entries[i]->id), i < carried};
  }
  if (carried == 0) {
    table.types[0].available = true;
  }
  table.selected = carried == 0 ? 0 : static_cast<std::int32_t>(carried) - 1;
  table.single_type = carried <= 1;
  select_first_tube_type(table, load, sub);
  return table;
}

// `tdc_upgrade_mark18_by_date` under the torpedo rules (not in the
// original); `gradual`: `rules.mixed_mark18`.
TorpedoTypes mixed_upgrade(TorpedoLoad& load, const SubClass& sub, const GameTables& tables, std::int32_t year,
                           std::int32_t month, bool gradual) {
  const Torpedo& mk18_1 = find_torpedo(tables, "MARK18-1");
  const Torpedo& mk18_2 = find_torpedo(tables, "MARK18-2");
  const std::size_t tubes = std::min<std::size_t>(sub.bow_tubes + sub.stern_tubes, load.tubes.size());
  const std::array<std::span<std::int32_t>, 3> groups{std::span<std::int32_t>(load.tubes).first(tubes),
                                                       racks_of(load.bow_racks, sub.bow_reloads),
                                                       racks_of(load.stern_racks, sub.stern_reloads)};
  // Months since the 18-2's first (its 1-based month field).
  const std::int32_t since = year * 12 + month - ((static_cast<std::int32_t>(mk18_2.year) - kScenarioYearBase) * 12 +
                                                  static_cast<std::int32_t>(mk18_2.month) - 1);
  if (since >= 0) {
    const auto old_id = static_cast<std::int32_t>(mk18_1.id);
    const auto new_id = static_cast<std::int32_t>(mk18_2.id);
    for (const std::span<std::int32_t> group : groups) {
      bool swap = true;
      for (std::int32_t& id : group) {
        if (id == old_id) {
          if (!gradual || since >= kMark18Changeover || swap) {
            id = new_id;
          }
          swap = !swap;
        }
      }
    }
  }
  // At most three types: the oldest straight runner becomes the newest one
  // carried, as long as there are more.
  for (std::vector<std::uint32_t> carried = carried_torpedo_types(load, sub); carried.size() > kMaxTorpedoTypes;
       carried = carried_torpedo_types(load, sub)) {
    std::vector<std::int32_t> runners;
    for (const std::string_view name : kStraightRunners) {
      const auto id = static_cast<std::int32_t>(find_torpedo(tables, name).id);
      if (std::ranges::find(carried, static_cast<std::uint32_t>(id)) != carried.end()) {
        runners.push_back(id);
      }
    }
    if (runners.size() < 2) {
      break;
    }
    for (const std::span<std::int32_t> group : groups) {
      std::replace(group.begin(), group.end(), runners.front(), runners.back());
    }
  }
  return carried_types_table(load, sub, tables);
}

}  // namespace

TorpedoLoad scenario_torpedo_load(const ScenarioHeader& header) {
  TorpedoLoad load;
  load.tubes = header.tube_torpedoes;
  std::size_t bow = 0;
  std::size_t stern = 0;
  for (const TorpedoReload& r : header.reloads) {
    if (r.type == -1) {
      break;
    }
    if (bow + static_cast<std::size_t>(std::max(r.forward, 0)) > load.bow_racks.size() ||
        stern + static_cast<std::size_t>(std::max(r.aft, 0)) > load.stern_racks.size()) {
      throw DataError("scenario reloads overflow the torpedo racks");
    }
    for (std::int32_t i = 0; i < r.forward; ++i) {
      load.bow_racks[bow++] = r.type;
    }
    for (std::int32_t i = 0; i < r.aft; ++i) {
      load.stern_racks[stern++] = r.type;
    }
  }
  return load;
}

std::vector<std::uint32_t> carried_torpedo_types(const TorpedoLoad& load, const SubClass& sub) {
  std::vector<std::uint32_t> ids;
  const auto add = [&ids](std::span<const std::int32_t> group) {
    for (const std::int32_t id : group) {
      if (id > 0 && std::ranges::find(ids, static_cast<std::uint32_t>(id)) == ids.end()) {
        ids.push_back(static_cast<std::uint32_t>(id));
      }
    }
  };
  add(std::span(load.tubes).first(std::min<std::size_t>(sub.bow_tubes + sub.stern_tubes, load.tubes.size())));
  add(std::span(load.bow_racks).first(std::min<std::size_t>(sub.bow_reloads, load.bow_racks.size())));
  add(std::span(load.stern_racks).first(std::min<std::size_t>(sub.stern_reloads, load.stern_racks.size())));
  std::ranges::sort(ids);
  return ids;
}

std::int32_t mark27_first_month(SubCommand command) {
  constexpr std::int32_t kYear = 1944 - kScenarioYearBase;
  return kYear * 12 + (command == SubCommand::kComSubPac ? 5 : 8);
}

bool mark27_in_service(std::int32_t year, std::int32_t month, SubCommand command) {
  return year * 12 + month >= mark27_first_month(command);
}

void add_extended_torpedoes(GameTables& tables) {
  if (tables.find_torpedo(kMark27Name) != nullptr) {
    return;
  }
  Torpedo t = find_torpedo(tables, "MARK18-1");
  t.short_name = std::string(kMark27Name);
  t.long_name = "MARK 27";
  t.id = kMark27Id;
  t.month = 6;
  t.year = 1944;
  t.range_long_yd = 5000;
  t.range_short_yd = 5000;
  t.speed_low_kts = 12;
  t.speed_high_kts = 12;
  t.diameter_in = 19;
  t.warhead_lb = 95;
  t.length_ft = 8;
  t.speed_settings = 1;
  for (std::size_t s = 0; s < t.power.size(); ++s) {
    t.power[s] = Fixed16::from_raw(static_cast<std::int32_t>(static_cast<std::int64_t>(t.power[s].raw()) * kMark27Power / 1000));
    t.run_time_s[s] = kMark27RunTime;
  }
  tables.weapons.torpedoes.push_back(std::move(t));
}

TorpedoTypes kept_torpedo_types(const TorpedoLoad& load, const SubClass& sub, const GameTables& tables) {
  return carried_types_table(load, sub, tables);
}

TorpedoTypes tdc_upgrade_mark18_by_date(TorpedoLoad& load, const SubClass& sub, const GameTables& tables,
                                        std::int32_t year, std::int32_t month, const MissionRules& rules) {
  if (rules.mixed_mark18 || rules.mark27) {
    return mixed_upgrade(load, sub, tables, year, month, rules.mixed_mark18);
  }
  // The month field (`Torpedo::variant`, +44) is the 1-based service month.
  const Torpedo& mk18_2 = find_torpedo(tables, "MARK18-2");
  const std::int32_t mk18_2_year = static_cast<std::int32_t>(mk18_2.year) - kScenarioYearBase;
  const bool upgrade = mk18_2_year < year ||
                       (mk18_2_year == year && static_cast<std::int32_t>(mk18_2.month) - 1 <= month);
  const std::span<std::int32_t> bow_racks = racks_of(load.bow_racks, sub.bow_reloads);
  const std::span<std::int32_t> stern_racks = racks_of(load.stern_racks, sub.stern_reloads);
  if (upgrade) {
    const auto old_id = static_cast<std::int32_t>(find_torpedo(tables, "MARK18-1").id);
    const auto new_id = static_cast<std::int32_t>(mk18_2.id);
    for (std::span<std::int32_t> group : {std::span<std::int32_t>(load.tubes), bow_racks, stern_racks}) {
      std::replace(group.begin(), group.end(), old_id, new_id);
    }
  }

  TorpedoTypes table;
  table.types = {{{"MARK10"}, {"MARK14"}, {upgrade ? "MARK18-2" : "MARK18-1"}}};
  std::int32_t available = 0;
  for (std::size_t i = 0; i < table.types.size(); ++i) {
    TorpedoTypeChoice& choice = table.types[i];
    const Torpedo& t = find_torpedo(tables, choice.name);
    choice.id = static_cast<std::int32_t>(t.id);
    std::ptrdiff_t carried = 0;
    for (std::span<std::int32_t> group : {std::span<std::int32_t>(load.tubes), bow_racks, stern_racks}) {
      carried += std::count(group.begin(), group.end(), choice.id);
    }
    if (carried != 0 || ((i < 1 || sub.modern_torpedoes) && in_service(t, year, month))) {
      choice.available = true;
      table.selected = static_cast<std::int32_t>(i);
      ++available;
    }
  }
  table.single_type = available == 1;
  select_first_tube_type(table, load, sub);
  return table;
}

}  // namespace sh
