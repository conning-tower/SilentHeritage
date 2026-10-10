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

// Returns whether `t` is in service at `year` (since 1930) / `month`, with
// the type table's comparison of its month field against the 0-based month.
bool in_service(const Torpedo& t, std::int32_t year, std::int32_t month) {
  const std::int32_t t_year = static_cast<std::int32_t>(t.year) - kScenarioYearBase;
  return t_year <= year && (t_year != year || static_cast<std::int32_t>(t.month) <= month);
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

TorpedoTypes tdc_upgrade_mark18_by_date(TorpedoLoad& load, const SubClass& sub, const GameTables& tables,
                                        std::int32_t year, std::int32_t month) {
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
  return table;
}

}  // namespace sh
