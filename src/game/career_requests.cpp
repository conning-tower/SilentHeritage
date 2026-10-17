#include "game/career_requests.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <map>
#include <utility>

#include "core/error.h"
#include "game/career_boats.h"
#include "game/extended_config.h"
#include "game/torpedo_load.h"

namespace sh {

namespace {

// The ROSTER<n>.EXT keys.
constexpr std::string_view kBoatName = "boat.name";
constexpr std::string_view kBoatClass = "boat.class";
constexpr std::string_view kGunMount = "boat.gun_mount";
constexpr std::string_view kSecondGun = "boat.second_gun";
constexpr std::string_view kTubes = "request.tubes";
constexpr std::string_view kBowRacks = "request.bow_racks";
constexpr std::string_view kSternRacks = "request.stern_racks";
constexpr std::string_view kPatrolZone = "request.patrol_zone";

// The supply of a short type, by name.
struct ShortSupply {
  std::string_view name;
  std::int32_t limit;
};
constexpr std::array<ShortSupply, 3> kShortSupply{{{"MARK18-1", 4}, {"MARK18-2", 4}, {kMark27Name, 2}}};

const Torpedo& find_torpedo(const GameTables& tables, std::string_view name) {
  const Torpedo* t = tables.find_torpedo(name);
  if (t == nullptr) {
    throw DataError("WEAPONS.DAT: no torpedo " + std::string(name));
  }
  return *t;
}

// The places of each group of `sub`, within the load's arrays.
std::size_t tube_count(const SubClass& sub) {
  return std::min<std::size_t>(sub.bow_tubes + sub.stern_tubes, TorpedoLoad::kTubes);
}
std::size_t rack_count(std::uint32_t reloads) { return std::min<std::size_t>(reloads, TorpedoLoad::kRackCapacity); }

// Calls `f(id)` for every place of `sub` in `load`, in order.
template <class F>
void for_each_place(const TorpedoLoad& load, const SubClass& sub, F f) {
  for (std::size_t i = 0; i < tube_count(sub); ++i) {
    f(load.tubes[i]);
  }
  for (std::size_t i = 0; i < rack_count(sub.bow_reloads); ++i) {
    f(load.bow_racks[i]);
  }
  for (std::size_t i = 0; i < rack_count(sub.stern_reloads); ++i) {
    f(load.stern_racks[i]);
  }
}

std::int32_t count_of(const TorpedoLoad& load, const SubClass& sub, std::uint32_t id) {
  std::int32_t n = 0;
  for_each_place(load, sub, [&](std::int32_t v) { n += v == static_cast<std::int32_t>(id) ? 1 : 0; });
  return n;
}

std::string id_list(std::span<const std::int32_t> ids) {
  std::string out;
  for (const std::int32_t id : ids) {
    out += (out.empty() ? "" : ",") + std::to_string(id);
  }
  return out;
}

// Parses `text` into `out` (comma-separated, exactly its size); false when
// malformed.
template <std::size_t N>
bool parse_ids(std::string_view text, std::array<std::int32_t, N>& out) {
  std::size_t i = 0;
  while (true) {
    const std::size_t comma = std::min(text.find(','), text.size());
    const std::string_view item = text.substr(0, comma);
    std::int32_t v = 0;
    const auto [end, ec] = std::from_chars(item.data(), item.data() + item.size(), v);
    if (ec != std::errc() || end != item.data() + item.size() || i >= N || v < 0) {
      return false;
    }
    out[i++] = v;
    if (comma == text.size()) {
      break;
    }
    text.remove_prefix(comma + 1);
  }
  return i == N;
}

}  // namespace

bool in_good_standing(const CareerRecord& record) {
  const bool decorated = record.award(Award::kSilverStar) > 0 || record.award(Award::kNavyCross) > 0 ||
                         record.award(Award::kDsm) > 0 || record.award(Award::kMedalOfHonor) > 0;
  return (decorated || record.rank >= 2) && record.poor_patrol_streak == 0;
}

bool requests_enabled(const Career& career) { return career.rules && career.rules->career_requests; }

bool requests_for_boat(const CareerRequests& requests, const CareerRecord& record) {
  return requests.boat == record.boat && requests.class_name == record.class_name;
}

CareerRequests& current_requests(Career& career) {
  CareerRequests& r = career.requests;
  if (!requests_for_boat(r, career.record)) {
    r = CareerRequests{.boat = career.record.boat, .class_name = career.record.class_name, .fit = {}, .torpedo_load = {}};
  }
  return r;
}

BoatFit career_boat_fit(const Career& career) {
  if (!requests_enabled(career) || !requests_for_boat(career.requests, career.record)) {
    return {};
  }
  return career.requests.fit;
}

std::string career_requests_config(const CareerRequests& requests) {
  if (requests.fit.empty() && !requests.torpedo_load && !requests.patrol_zone) {
    return {};
  }
  std::string text = std::format("{} = {}\n{} = {}\n", kBoatName, requests.boat, kBoatClass, requests.class_name);
  if (requests.fit.gun_mount) {
    text += std::format("{} = {}\n", kGunMount, *requests.fit.gun_mount);
  }
  if (requests.fit.second_gun) {
    text += std::format("{} = 1\n", kSecondGun);
  }
  if (const std::optional<TorpedoLoad>& load = requests.torpedo_load) {
    text += std::format("{} = {}\n{} = {}\n{} = {}\n", kTubes, id_list(load->tubes), kBowRacks,
                        id_list(load->bow_racks), kSternRacks, id_list(load->stern_racks));
  }
  if (requests.patrol_zone) {
    text += std::format("{} = {}\n", kPatrolZone, *requests.patrol_zone);
  }
  return text;
}

CareerRequests parse_career_requests(std::string_view text) {
  const auto values = parse_config(text);
  const auto get = [&values](std::string_view key) -> std::optional<std::string_view> {
    const auto it = values.find(key);
    return it == values.end() ? std::nullopt : std::optional<std::string_view>(it->second);
  };
  CareerRequests r;
  r.boat = std::string(get(kBoatName).value_or(""));
  r.class_name = std::string(get(kBoatClass).value_or(""));
  if (const auto mount = get(kGunMount); mount == "0" || mount == "1") {
    r.fit.gun_mount = *mount == "1" ? 1u : 0u;
  }
  r.fit.second_gun = get(kSecondGun) == "1";
  TorpedoLoad load;
  const auto tubes = get(kTubes);
  const auto bow = get(kBowRacks);
  const auto stern = get(kSternRacks);
  if (tubes && bow && stern && parse_ids(*tubes, load.tubes) && parse_ids(*bow, load.bow_racks) &&
      parse_ids(*stern, load.stern_racks)) {
    r.torpedo_load = load;
  }
  if (const auto zone = get(kPatrolZone)) {
    std::int32_t z = 0;
    const auto [end, ec] = std::from_chars(zone->data(), zone->data() + zone->size(), z);
    if (ec == std::errc() && end == zone->data() + zone->size() && z >= 0) {
      r.patrol_zone = z;
    }
  }
  return r;
}

// ------------------------------------------------------- torpedo load

std::vector<TorpedoOffer> torpedo_offers(const SubClass& sub, const GameTables& tables, std::int32_t year,
                                         std::int32_t month, SubCommand command, const MissionRules& rules,
                                         bool good_standing, const TorpedoLoad& generated) {
  const std::int32_t now = year * 12 + month;
  // Months since `t` is in service (its 1-based WEAPONS.DAT month).
  const auto since = [now](const Torpedo& t) {
    return now - ((static_cast<std::int32_t>(t.year) - kScenarioYearBase) * 12 + static_cast<std::int32_t>(t.month) - 1);
  };
  std::vector<std::pair<const Torpedo*, std::int32_t>> types{{&find_torpedo(tables, "MARK10"), kNewTorpedoMonths}};
  if (sub.modern_torpedoes) {
    types.emplace_back(&find_torpedo(tables, "MARK14"), kNewTorpedoMonths);
    for (const std::string_view name : {"MARK18-1", "MARK18-2"}) {
      const Torpedo& t = find_torpedo(tables, name);
      if (since(t) >= 0) {
        types.emplace_back(&t, since(t));
      }
    }
    const Torpedo* mk27 = tables.find_torpedo(kMark27Name);
    if (rules.mark27 && mk27 != nullptr && mark27_in_service(year, month, command)) {
      types.emplace_back(mk27, now - mark27_first_month(command));
    }
  }
  std::vector<TorpedoOffer> offers;
  for (const auto& [t, months] : types) {
    TorpedoOffer o{.id = t->id, .limit = std::nullopt};
    const auto it = std::ranges::find(kShortSupply, t->short_name, &ShortSupply::name);
    if (it != kShortSupply.end() && months < kNewTorpedoMonths) {
      o.limit = std::max(it->limit + (good_standing ? kGoodStandingSupply : 0), count_of(generated, sub, t->id));
    }
    offers.push_back(o);
  }
  std::ranges::sort(offers, {}, &TorpedoOffer::id);
  return offers;
}

bool torpedo_load_allowed(const TorpedoLoad& load, const SubClass& sub, std::span<const TorpedoOffer> offers) {
  TorpedoLoad inside;
  std::copy_n(load.tubes.begin(), tube_count(sub), inside.tubes.begin());
  std::copy_n(load.bow_racks.begin(), rack_count(sub.bow_reloads), inside.bow_racks.begin());
  std::copy_n(load.stern_racks.begin(), rack_count(sub.stern_reloads), inside.stern_racks.begin());
  if (inside != load) {
    return false;
  }
  const std::vector<std::uint32_t> carried = carried_torpedo_types(load, sub);
  if (carried.size() > kMaxTorpedoTypes) {
    return false;
  }
  for (const std::uint32_t id : carried) {
    const auto o = std::ranges::find(offers, id, &TorpedoOffer::id);
    if (o == offers.end() || (o->limit && count_of(load, sub, id) > *o->limit)) {
      return false;
    }
  }
  return true;
}

TorpedoLoad generated_torpedo_load(const ScenarioHeader& header, const SubClass& sub, const GameTables& tables,
                                   const MissionRules& rules) {
  TorpedoLoad load = scenario_torpedo_load(header);
  tdc_upgrade_mark18_by_date(load, sub, tables, header.year, header.month, rules);
  return load;
}

TorpedoLoadEditor::TorpedoLoadEditor(const SubClass& sub, const TorpedoLoad& generated,
                                     std::vector<TorpedoOffer> offers, const std::optional<TorpedoLoad>& chosen)
    : sub_(&sub), generated_(generated), offers_(std::move(offers)), load_(generated) {
  if (chosen && torpedo_load_allowed(*chosen, sub, offers_)) {
    load_ = *chosen;
  }
}

std::size_t TorpedoLoadEditor::places(TorpedoPlace::Group group) const {
  switch (group) {
    case TorpedoPlace::Group::kTube:
      return tube_count(*sub_);
    case TorpedoPlace::Group::kBowRack:
      return rack_count(sub_->bow_reloads);
    case TorpedoPlace::Group::kSternRack:
      break;
  }
  return rack_count(sub_->stern_reloads);
}

std::int32_t& TorpedoLoadEditor::slot(TorpedoPlace place) {
  switch (place.group) {
    case TorpedoPlace::Group::kTube:
      return load_.tubes.at(place.index);
    case TorpedoPlace::Group::kBowRack:
      return load_.bow_racks.at(place.index);
    case TorpedoPlace::Group::kSternRack:
      break;
  }
  return load_.stern_racks.at(place.index);
}

std::int32_t TorpedoLoadEditor::at(TorpedoPlace place) const {
  return place.index < places(place.group) ? const_cast<TorpedoLoadEditor*>(this)->slot(place) : 0;
}

std::int32_t TorpedoLoadEditor::count(std::uint32_t id) const {
  return count_of(load_, *sub_, id) + (held_ == id && id != 0 ? 1 : 0);
}

std::optional<std::int32_t> TorpedoLoadEditor::limit(std::uint32_t id) const {
  const auto o = std::ranges::find(offers_, id, &TorpedoOffer::id);
  return o == offers_.end() ? std::nullopt : o->limit;
}

std::vector<std::uint32_t> TorpedoLoadEditor::types() const {
  std::vector<std::uint32_t> ids = carried_torpedo_types(load_, *sub_);
  if (held_ != 0 && std::ranges::find(ids, held_) == ids.end()) {
    ids.push_back(held_);
    std::ranges::sort(ids);
  }
  return ids;
}

std::optional<TorpedoPlace> TorpedoLoadEditor::first_empty() const {
  for (const TorpedoPlace::Group g :
       {TorpedoPlace::Group::kTube, TorpedoPlace::Group::kBowRack, TorpedoPlace::Group::kSternRack}) {
    for (std::size_t i = 0; i < places(g); ++i) {
      if (at({g, i}) == 0) {
        return TorpedoPlace{g, i};
      }
    }
  }
  return std::nullopt;
}

TorpedoLoadEditor::Refusal TorpedoLoadEditor::can_take(std::uint32_t id) const {
  const auto o = std::ranges::find(offers_, id, &TorpedoOffer::id);
  if (held_ != 0 || o == offers_.end()) {
    return Refusal::kNotOffered;
  }
  if (o->limit && count(id) >= *o->limit) {
    return Refusal::kSupply;
  }
  const std::vector<std::uint32_t> now = types();
  if (std::ranges::find(now, id) == now.end() && now.size() >= kMaxTorpedoTypes) {
    return Refusal::kTypes;
  }
  if (!first_empty()) {
    return Refusal::kFull;
  }
  return Refusal::kNone;
}

bool TorpedoLoadEditor::take(std::uint32_t id) {
  if (can_take(id) != Refusal::kNone) {
    return false;
  }
  held_ = id;
  return true;
}

bool TorpedoLoadEditor::pick(TorpedoPlace place) {
  if (held_ != 0 || place.index >= places(place.group) || at(place) == 0) {
    return false;
  }
  held_ = static_cast<std::uint32_t>(std::exchange(slot(place), 0));
  return true;
}

bool TorpedoLoadEditor::put(TorpedoPlace place) {
  if (held_ == 0 || place.index >= places(place.group)) {
    return false;
  }
  held_ = static_cast<std::uint32_t>(std::exchange(slot(place), static_cast<std::int32_t>(held_)));
  return true;
}

bool TorpedoLoadEditor::add(std::uint32_t id) {
  if (can_take(id) != Refusal::kNone) {
    return false;
  }
  slot(*first_empty()) = static_cast<std::int32_t>(id);
  return true;
}

bool TorpedoLoadEditor::remove(std::uint32_t id) {
  if (held_ != 0) {
    return false;
  }
  for (const TorpedoPlace::Group g :
       {TorpedoPlace::Group::kSternRack, TorpedoPlace::Group::kBowRack, TorpedoPlace::Group::kTube}) {
    for (std::size_t i = places(g); i-- > 0;) {
      if (at({g, i}) == static_cast<std::int32_t>(id)) {
        slot({g, i}) = 0;
        return true;
      }
    }
  }
  return false;
}

void TorpedoLoadEditor::reset() {
  load_ = generated_;
  held_ = 0;
}

std::optional<TorpedoLoad> TorpedoLoadEditor::chosen() const {
  if (load_ == generated_) {
    return std::nullopt;
  }
  return load_;
}

// --------------------------------------------------------- patrol area

bool can_request_patrol_area(const Career& career, const CampaignData& data) {
  return requests_enabled(career) && in_good_standing(career.record) &&
         !patrol_zone_candidates(career, data).empty();
}

std::optional<PatrolAssignment> assign_requested_patrol(Career& career, const CampaignData& data,
                                                        WatcomRandom& random) {
  // The candidates before the draw, which moves the career's last zone.
  const bool may_ask = requests_for_boat(career.requests, career.record) && career.requests.patrol_zone &&
                       can_request_patrol_area(career, data);
  const std::vector<std::int32_t> candidates =
      may_ask ? patrol_zone_candidates(career, data) : std::vector<std::int32_t>{};
  std::optional<PatrolAssignment> a = assign_patrol(career, data, random);
  const std::optional<std::int32_t> asked = career.requests.patrol_zone;
  if (!a || !asked || std::ranges::find(candidates, *asked) == candidates.end()) {
    return a;
  }
  CareerRecord& r = career.record;
  const Transit t = compute_transit(*find_home_port(data, r, r.home_port), *asked, r);
  a->zone_index = *asked;
  a->zone = data.zones.at(static_cast<std::size_t>(*asked));
  a->transit_hours = t.hours;
  a->transit_fuel = t.fuel;
  r.last_patrol_zone = *asked;
  return a;
}

// ------------------------------------------------------- boat transfer

bool can_request_boat_transfer(const Career& career, SubCommand home_command, const GameTables& tables) {
  return requests_enabled(career) && career.record.patrols >= 1 &&
         next_boat_class(career.record, career.summary, home_command, career_boat_classes(tables)).has_value();
}

std::int32_t boat_transfer_chance(const CareerRecord& record) {
  if (record.poor_patrol_streak != 0) {
    return 0;
  }
  const std::int32_t award = record.award(Award::kMedalOfHonor) > 0                                     ? 4
                             : record.award(Award::kNavyCross) > 0 || record.award(Award::kDsm) > 0 ? 3
                             : record.award(Award::kSilverStar) > 0                                     ? 2
                             : record.award(Award::kBronzeStar) > 0                                     ? 1
                                                                                                        : 0;
  const std::int32_t chance =
      10 * award + 10 * std::max(record.rank, 0) + 5 * std::max(record.patrols_since_new_boat - 2, 0);
  return std::min(chance, kMaxTransferChance);
}

bool boat_transfer_granted(const CareerRecord& record) {
  WatcomRandom draw(record.seed ^ (static_cast<std::uint32_t>(record.patrols) * 0x9e3779b9u));
  return draw.random_int(100) < boat_transfer_chance(record);
}

// ----------------------------------------------------------- deck gun

bool can_move_deck_gun(const SubClass& sub, const BoatFit& fit) {
  return fleet_boat_with_one_gun(sub) && !fit.second_gun;
}

bool can_fit_second_gun(const SubClass& sub, const BoatFit& fit, const CareerRecord& record) {
  return fleet_boat_with_one_gun(sub) && !fit.second_gun && in_good_standing(record);
}

}  // namespace sh
