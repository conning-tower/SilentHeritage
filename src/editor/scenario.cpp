#include "editor/scenario.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include "core/error.h"
#include "editor/environment.h"
#include "game/stations/chart_view.h"

namespace sh::editor {
namespace {

// The role group of the orders menu: merchants count as warships.
ShipRole role_group(const ShipClass& c) { return c.role == ShipRole::kMerchant ? ShipRole::kWarship : c.role; }

bool is_submarine_class(const ShipClass& c) { return c.ship_type == ShipType::kSubmarine; }

// The SE chart view (0xa4e30): the screen above the status bar.
constexpr int kViewX1 = 639;
constexpr int kViewY1 = 458;

// `world_find_nearest_water` (0x45544) tables: the cells tested around a
// point (0x9e908 / 0x9e918) and the square spiral's steps (0x9e928 /
// 0x9e938).
constexpr std::array<std::int32_t, 4> kSampleColumn = {0, 0, 1, 1};
constexpr std::array<std::int32_t, 4> kSampleRow = {1, 0, 0, 1};
constexpr std::array<std::int32_t, 4> kSpiralColumn = {-1, 0, 1, 0};
constexpr std::array<std::int32_t, 4> kSpiralRow = {0, 1, 0, -1};
// The deepest grid value of a cell `world_find_nearest_water` treats as land.
constexpr std::uint8_t kWaterMax = 0x1d;

// `world_find_nearest_water`: a point shallower than 80 ft moves to the
// centre of the nearest cell whose 2x2 block is all water, searched along a
// square spiral.
WorldPoint nearest_water(const WorldData& w, WorldPoint p) {
  if (-w.floor().height_at(p.x, p.y) >= 0x500) {
    return p;
  }
  const TerrainGrid& grid = w.grid();
  const std::int32_t cell = grid.cell_size();
  std::int32_t column = p.x / cell;
  std::int32_t row = (grid.height() * cell - p.y) / cell;
  std::int32_t direction = 0;
  std::int32_t run = 1;
  std::int32_t steps = 0;
  std::int32_t turns = 0;
  for (;;) {
    bool water = true;
    for (std::size_t i = 0; i < kSampleColumn.size(); ++i) {
      if (grid.value(column + kSampleColumn[i], row + kSampleRow[i]) > kWaterMax) {
        water = false;
        break;
      }
    }
    if (water) {
      break;
    }
    ++steps;
    row += kSpiralRow[static_cast<std::size_t>(direction)];
    column += kSpiralColumn[static_cast<std::size_t>(direction)];
    if (steps >= run) {
      direction = (direction + 1) % 4;
      steps = 0;
      ++turns;
    }
    if (turns > 1) {
      turns = 0;
      ++run;
    }
  }
  return {column * cell + cell / 2, cell * grid.height() - (row * cell + cell / 2)};
}

}  // namespace

// ------------------------------------------------------------ helpers

bool orders_fit_class(const ShipClass& c, ShipOrders orders) {
  if (orders == ShipOrders::kNone) {
    return true;
  }
  if (is_submarine_class(c)) {
    return orders == ShipOrders::kFollowPath || orders == ShipOrders::kIntercept;
  }
  switch (c.role) {
    case ShipRole::kMerchant:
    case ShipRole::kWarship:
      return orders == ShipOrders::kConvoyLeader || orders == ShipOrders::kConvoyMember ||
             orders == ShipOrders::kFollowPath;
    case ShipRole::kEscort:
      return orders == ShipOrders::kEscort || orders == ShipOrders::kPatrol || orders == ShipOrders::kFollowPath;
    case ShipRole::kSmallCraft:
      return orders == ShipOrders::kFishing || orders == ShipOrders::kFollowPath;
    case ShipRole::kSubmarine:
      break;
  }
  return false;
}

WarPeriod war_period(const ScenarioDate& d) {
  const auto key = [](const ScenarioDate& x) { return std::array{x.year, x.month, x.day}; };
  if (key(d) > key(kWarEnd)) {
    return WarPeriod::kAfter;
  }
  return key(d) < key(kWarStart) ? WarPeriod::kBefore : WarPeriod::kDuring;
}

std::int32_t days_in_month(std::int32_t month, std::int32_t year) {
  constexpr std::array<std::int32_t, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 1 && (year + kScenarioYearBase) % 4 == 0) {
    return 29;  // the table covers 1930-1946: no century years
  }
  return kDays[static_cast<std::size_t>(month)];
}

bool sub_in_service(const SubClass& sub, std::int32_t month, std::int32_t year) {
  const std::int32_t y = year + kScenarioYearBase;
  const std::int32_t m = month + 1;
  const auto intro_month = static_cast<std::int32_t>(sub.intro_month);
  const auto end_month = static_cast<std::int32_t>(sub.end_month);
  const auto intro_year = static_cast<std::int32_t>(sub.intro_year);
  const auto end_year = static_cast<std::int32_t>(sub.end_year);
  const bool started = intro_year < y || (intro_year == y && intro_month <= m);
  const bool not_ended = y < end_year || (y == end_year && m <= end_month);
  return started && not_ended;
}

std::array<std::int32_t, 2> view_home_center(const World& world) {
  // sb_setup_world (0x45259): 0xa4e20 = declared width - 1, 0xa4e24 =
  // height - 1.
  return {(world.chart.width - 2) / 2 << 16, (world.chart.height - 1) / 2 << 16};
}

std::int32_t ship_top_speed(const ShipClass& ship) {
  return static_cast<std::int32_t>(std::lrint(std::strtod(ship.top_speed_kts.c_str(), nullptr)));
}

// ------------------------------------------------------------ Scenario

Scenario::Scenario(const Install& install, const EditorData& data, const EditorOptions& options, Clock clock)
    : reload_aft(data.torpedo_choices.count, 0),
      reload_forward(data.torpedo_choices.count, 0),
      install_(install),
      data_(data),
      options_(options),
      clock_(std::move(clock)) {
  set_scn_text(header.world, data_.se.str(se_addr::kDefaultWorld));
  world_ = WorldData::load(install_, data_, scn_text(header.world));
  new_scenario();
  player_record_ = player().rec;
}

PlayerSub& Scenario::player() const { return static_cast<PlayerSub&>(*objects_.front()); }

std::size_t Scenario::index_of(const EditorObject& object) const {
  const auto it = std::ranges::find_if(objects_, [&](const auto& o) { return o.get() == &object; });
  if (it == objects_.end()) {
    throw std::invalid_argument("object not in the scenario");
  }
  return static_cast<std::size_t>(it - objects_.begin());
}

template <class T>
T& Scenario::append(std::unique_ptr<T> object) {
  T& ref = *object;
  objects_.push_back(std::move(object));
  return ref;
}

void Scenario::setup_world() {
  // sb_setup_world (0x45259); the chart view reset belongs to the chart.
  world_->update_owners(header.day, header.month, header.year);
  memory_budget_ = editor::memory_budget(world_->memory_size());
}

void Scenario::load_world(std::string_view name) {
  // scenario_reload_world (0x2a4fa)
  world_ = WorldData::load(install_, data_, name);
  setup_world();
}

void Scenario::update_base_owners() { world_->update_owners(header.day, header.month, header.year); }

void Scenario::change_world(std::string_view world_name) {
  set_scn_text(header.world, world_name);
  load_world(scn_text(header.world));
  reset(false);
  create_default_player();
  selected_ = objects_.front().get();
}

void Scenario::reset(bool full) {
  objects_.clear();
  selected_ = nullptr;
  header.required_memory = 0;
  cache_.clear();
  if (full) {
    modified = false;
    header.weather = Weather::kClear;
    header.sea_state = SeaState::kCalm;
    header.fog = 0;
    header.gradient = 0;
    header.month = kWarStart.month;
    header.day = kWarStart.day;
    header.year = kWarStart.year;
    header.hour = 12;
    header.minute = 0;
    header.briefing[0] = '\0';
    header.end_on_lost_contact = options_.end_on_lost_contact;
    // Original bug ("Password storage", fixed): SE zeroes the first byte
    // only, so a scenario saved after New kept the previous password's
    // encoded bytes 1-14.
    header.password = {};
    header.randomize = options_.randomize;
    sighting_fog_pct = 0;
    header.random_seed = clock_() + 1;
    header.equipment &= ~0x3fu;  // the low byte & 0xc0
  }
  setup_world();
}

void Scenario::new_scenario() {
  reset(true);
  create_default_player();
  selected_ = objects_.front().get();  // sb_center_on_player
  reset_torpedo_load();
  refresh_environment(*this);
}

void Scenario::create_default_player() {
  // sb_create_default_player (0x45338)
  ScnPlayerSub rec;
  const std::vector<SubClass>& subs = data_.tables.subs;
  std::size_t cls = 0;
  for (std::size_t i = subs.size(); i-- > 0;) {
    if (sub_in_service(subs[i], header.month, header.year)) {
      cls = i;
      break;
    }
  }
  const SubClass& sub = subs[cls];
  set_scn_text(rec.sub_class, sub.name);
  for (const CarsubBoat& boat : data_.tables.carsub) {
    if (boat.class_index == cls) {
      std::string name(data_.se.str(se_addr::kHullPrefix));
      name += std::to_string(boat.hull_number);
      name += data_.se.str(se_addr::kHullSuffix);
      name += boat.name;
      set_scn_text(rec.name, name);
      break;
    }
  }
  rec.depth = static_cast<std::int32_t>(sub.draft_ft);
  // The centre of the chart view, reset by sb_setup_world to the map's
  // centre at zoom 1.
  ChartView view(world_->world(), 0, 0, kViewX1, kViewY1);
  view.set_zoom(0x10000);
  const std::array<std::int32_t, 2> home = view_home_center(world_->world());
  view.set_center(home[0], home[1]);
  const std::array<std::int32_t, 2> centre = view.screen_to_world((kViewX1 + 1) / 2, (kViewY1 + 1) / 2);
  const WorldPoint p = nearest_water(*world_, {centre[0], centre[1]});
  rec.x = p.x;
  rec.y = p.y;
  append(std::make_unique<PlayerSub>(rec));
}

void Scenario::reset_torpedo_load() {
  // sb_reset_torpedo_load (0x4569b). An unknown player class reads past
  // SUBS.DAT in the original; it gets the MARK10 and no reloads here.
  const std::optional<std::size_t> cls = data_.find_sub(scn_text(player().rec.sub_class));
  const SubClass* sub = cls ? &data_.tables.subs[*cls] : nullptr;
  const bool modern = sub != nullptr && sub->modern_torpedoes;
  const std::optional<std::size_t> torpedo =
      data_.find_listed_torpedo(data_.se.str(modern ? se_addr::kMark14 : se_addr::kMark10));
  if (!torpedo) {
    return;
  }
  header.tube_torpedoes.fill(static_cast<std::int32_t>(data_.tables.weapons.torpedoes[*torpedo].id));
  for (std::size_t i = 0; i < reload_aft.size(); ++i) {
    const bool loaded = i == *torpedo && sub != nullptr;
    reload_aft[i] = loaded ? static_cast<std::int32_t>(sub->stern_reloads) : 0;
    reload_forward[i] = loaded ? static_cast<std::int32_t>(sub->bow_reloads) : 0;
  }
}

void Scenario::add_models(Vessel& v, std::size_t ship_class) {
  const ShipClass& c = data_.tables.ships[ship_class];
  header.required_memory += cache_.add(c.sprite1, data_.models);
  v.ship_model1 = c.sprite1;
  v.sub_model1 = c.sprite1;
  header.required_memory += cache_.add(c.sprite2, data_.models);
  v.ship_model2 = c.sprite2;
  v.sub_model2 = c.sprite2;
}

void Scenario::spawn(const ScnRecord& record) {
  // The sbfile_spawn_* converters (0x46e68..0x47226); below version 7 each
  // object's cost and new models are added to the estimate.
  const bool count = header.version < kScnWriteVersion;
  std::int32_t models = 0;
  EditorObject* spawned = nullptr;
  if (const auto* ship = std::get_if<ScnShip>(&record)) {
    auto v = std::make_unique<Vessel>();
    v->kind = Vessel::Kind::kShip;
    v->ship = *ship;
    v->sub.x = ship->x;
    v->sub.y = ship->y;
    v->sub.depth = 0;
    v->sub.heading = ship->heading;
    v->sub.speed = ship->speed;
    v->sub.orders = ShipOrders::kNone;
    v->sub.crew = ship->crew;
    v->sub.damage_pct = ship->damage_pct;
    v->sub.waypoints = ship->waypoints;
    v->sub.target_x = -1;
    v->sub.target_y = -1;
    v->sub.alert = 0;
    // The sub part's class and side stay as the heap held them: zeros here.
    if (const std::optional<std::size_t> cls = data_.find_ship(scn_text(ship->ship_class))) {
      const ShipClass& c = data_.tables.ships[*cls];
      models += cache_.add(c.sprite1, data_.models);
      v->ship_model1 = c.sprite1;
      models += cache_.add(c.sprite2, data_.models);
      v->ship_model2 = c.sprite2;
    }
    spawned = &append(std::move(v));
  } else if (const auto* enemy = std::get_if<ScnEnemySub>(&record)) {
    auto v = std::make_unique<Vessel>();
    v->kind = Vessel::Kind::kSubmarine;
    v->sub = *enemy;
    v->ship.x = enemy->x;
    v->ship.y = enemy->y;
    v->ship.heading = enemy->heading;
    v->ship.speed = enemy->speed;
    v->ship.orders = enemy->orders;
    v->ship.crew = enemy->crew;
    v->ship.damage_pct = enemy->damage_pct;
    v->ship.waypoints = enemy->waypoints;
    v->ship.convoy_group = 0;
    v->ship.zigzag = 0;
    v->ship.alert = 0;
    if (const std::optional<std::size_t> cls = data_.find_ship(scn_text(enemy->sub_class))) {
      const ShipClass& c = data_.tables.ships[*cls];
      models += cache_.add(c.sprite1, data_.models);
      v->sub_model1 = c.sprite1;
      models += cache_.add(c.sprite2, data_.models);
      // Original bug (docs/editor-design.md "Enemy-sub model names", fixed): SE copies
      // the second name to the first slot too, so the first model's
      // reference was never released (kept in the estimate after removal).
      v->sub_model2 = c.sprite2;
    }
    spawned = &append(std::move(v));
  } else if (const auto* air = std::get_if<ScnAircraft>(&record)) {
    auto a = std::make_unique<Aircraft>();
    a->rec = *air;
    if (const std::optional<std::size_t> cls = data_.find_aircraft(scn_text(air->aircraft_type))) {
      a->model = data_.tables.aircraft[*cls].sprite;
      models += cache_.add(a->model, data_.models);
    }
    spawned = &append(std::move(a));
  } else if (const auto* strike = std::get_if<BomberGroupRecord>(&record)) {
    spawned = &append(std::make_unique<SpecialMission>(*strike));
  } else if (const auto* recon = std::get_if<PhotoReconRecord>(&record)) {
    spawned = &append(std::make_unique<SpecialMission>(*recon));
  }
  if (count && spawned != nullptr) {
    header.required_memory += spawned->memory_cost() + models;
  }
}

void Scenario::check_file(const ScnFile& file) {
  const auto in = [](auto value, std::int32_t lo, std::int32_t hi, std::string_view what) {
    const auto v = static_cast<std::int32_t>(value);
    if (v < lo || v > hi) {
      throw DataError(std::format("{} {} out of range", what, v));
    }
  };
  // The ranges of the ENVIRONMENT cycles.
  in(file.weather, 0, 3, "weather");
  in(file.sea_state, 0, 2, "sea state");
  in(file.month, 0, 11, "month");
  in(file.day, 1, 31, "day");
  // The years of the day tables (1930-1946) and the ephemeris.
  in(file.year, 0, 16, "year");
  in(file.hour, 0, 23, "hour");
  in(file.minute, 0, 59, "minute");
  for (const ScnRecord& r : file.records) {
    std::visit(
        [&](const auto& rec) {
          if constexpr (requires { rec.waypoints; }) {
            in(rec.waypoints.count, 0, kMaxCoursePoints, "waypoint count");
          }
          if constexpr (requires { rec.crew; }) {
            in(rec.crew, 0, 2, "crew");
          }
          if constexpr (requires { rec.orders; }) {
            in(rec.orders, 0, 7, "orders");
          }
          if constexpr (requires { rec.heading; }) {
            in(rec.heading, 0, 359, "heading");
          }
          if constexpr (requires { rec.convoy_group; }) {
            in(rec.convoy_group, 0, 8, "convoy");
          }
          if constexpr (requires { rec.escort_group; }) {
            in(rec.escort_group, 0, 8, "convoy");
          }
        },
        r);
  }
}

bool Scenario::load(const ScnFile& file, const std::function<bool(const ScnHeader&)>& accept) {
  check_file(file);
  reset(true);
  const ScnChars<13> previous_world = header.world;
  const std::int32_t seed = header.random_seed;
  // scen_load_ascn reads the header into the globals; an older version
  // leaves the reset's seed, and a password with no first byte (the reset
  // zeroes the others here: "Password storage", fixed).
  header = file;
  if (file.version < 4) {
    header.random_seed = seed;
    header.password = {};
  }
  for (const ScnRecord& r : file.records) {
    if (const auto* p = std::get_if<ScnPlayerSub>(&r)) {
      player_record_ = *p;
    }
  }
  if (accept && !accept(header)) {
    fail_load(previous_world);
    return false;
  }
  // scen_instantiate_units: the player record, then each kind in turn.
  append(std::make_unique<PlayerSub>(player_record_));
  const auto spawn_all = [&]<class T>(std::type_identity<T>) {
    for (const ScnRecord& r : file.records) {
      if (std::holds_alternative<T>(r)) {
        spawn(r);
      }
    }
  };
  spawn_all(std::type_identity<ScnShip>{});
  spawn_all(std::type_identity<ScnEnemySub>{});
  spawn_all(std::type_identity<ScnAircraft>{});
  spawn_all(std::type_identity<BomberGroupRecord>{});
  spawn_all(std::type_identity<PhotoReconRecord>{});
  // sb_finish_scenario_load (0x46df8)
  try {
    load_world(scn_text(header.world));
  } catch (const DataError&) {
    fail_load(previous_world);
    throw;
  }
  selected_ = objects_.front().get();
  refresh_environment(*this);
  update_all_visibility();
  const std::size_t listed = std::min(data_.torpedo_choices.count - 1, header.reloads.size());
  if (corrected_) {
    // Each entry goes to the TORPEDOES list entry of its torpedo type,
    // whatever its position (not in the original).
    const TorpedoChoices& choices = data_.torpedo_choices;
    for (const TorpedoReload& r : header.reloads) {
      for (std::size_t i = 0; r.type != -1 && i < listed && i < choices.torpedoes.size(); ++i) {
        if (static_cast<std::int32_t>(data_.tables.weapons.torpedoes[choices.torpedoes[i]].id) == r.type) {
          reload_aft[i] = r.aft;
          reload_forward[i] = r.forward;
          break;
        }
      }
    }
    return true;
  }
  for (std::size_t i = 0; i < listed; ++i) {
    // Original bug (docs/editor-design.md "Reload table"): copied back by position,
    // not by torpedo type.
    if (header.reloads[i].type != -1) {
      reload_aft[i] = header.reloads[i].aft;
      reload_forward[i] = header.reloads[i].forward;
    }
  }
  return true;
}

void Scenario::fail_load(const ScnChars<13>& previous_world) {
  header.world = previous_world;
  load_world(scn_text(header.world));
  new_scenario();
}

ScnFile Scenario::save() {
  header.special_mission = 0;
  for (const auto& o : objects_) {
    if (o->is_special_mission()) {
      header.special_mission = -1;
    }
  }
  const TorpedoChoices& choices = data_.torpedo_choices;
  for (std::size_t i = 0; i < header.reloads.size(); ++i) {
    TorpedoReload& r = header.reloads[i];
    r.type = -1;  // the counts of the -1 entries stay as they were
    if (i + 1 < choices.count && i < choices.torpedoes.size()) {
      r.type = static_cast<std::int32_t>(data_.tables.weapons.torpedoes[choices.torpedoes[i]].id);
      r.aft = reload_aft[i];
      r.forward = reload_forward[i];
    }
  }
  ScnFile file;
  static_cast<ScnHeader&>(file) = header;
  file.version = kScnWriteVersion;
  file.records.reserve(objects_.size());
  for (const auto& o : objects_) {
    file.records.push_back(o->record());
  }
  return file;
}

// ------------------------------------------------------------ editing

std::int32_t Scenario::water_depth_ft(WorldPoint p) const { return -world_->floor().height_at(p.x, p.y) / 16; }

bool Scenario::can_place_vessel(std::size_t ship_class, WorldPoint p) const {
  const auto draft = static_cast<std::int32_t>(data_.tables.ships[ship_class].draft_ft);
  return -world_->floor().height_at(p.x, p.y) > draft * 16;
}

Vessel& Scenario::add_vessel(std::size_t ship_class, WorldPoint p, Vessel::Kind kind) {
  auto v = std::make_unique<Vessel>();
  v->kind = kind;
  const std::string& name = data_.tables.ships[ship_class].name;
  v->ship.x = p.x;
  v->ship.y = p.y;
  set_scn_text(v->ship.ship_class, name);
  v->sub.x = p.x;
  v->sub.y = p.y;
  v->sub.side = 1;
  v->sub.target_x = -1;
  v->sub.target_y = -1;
  set_scn_text(v->sub.sub_class, name);
  Vessel& ref = append(std::move(v));
  header.required_memory += ref.memory_cost();
  add_models(ref, ship_class);
  selected_ = &ref;
  ref.update_detection(detection_env());
  modified = true;
  return ref;
}

Aircraft& Scenario::add_aircraft(std::size_t aircraft_type, AircraftMission mission) {
  const AircraftType& type = data_.tables.aircraft[aircraft_type];
  auto a = std::make_unique<Aircraft>();
  a->rec.mission = mission;
  set_scn_text(a->rec.aircraft_type, type.name);
  a->rec.bomb_load = static_cast<std::int32_t>(type.bombs);
  a->rec.strafes = type.guns != 0 ? 1 : 0;
  a->rec.relaunch = 1;
  a->model = type.sprite;
  Aircraft& ref = append(std::move(a));
  header.required_memory += ref.memory_cost();
  header.required_memory += cache_.add(ref.model, data_.models);
  modified = true;
  return ref;
}

SpecialMission& Scenario::add_airstrike() {
  SpecialMission& ref = append(std::make_unique<SpecialMission>(BomberGroupRecord{.base = -1}));
  header.required_memory += ref.memory_cost();
  modified = true;
  return ref;
}

SpecialMission& Scenario::add_photo_recon() {
  SpecialMission& ref =
      append(std::make_unique<SpecialMission>(PhotoReconRecord{.base = -1, .delay_min = 0, .duration_min = 1440}));
  header.required_memory += ref.memory_cost();
  modified = true;
  return ref;
}

bool Scenario::removal_releases_convoy(const EditorObject& object) const {
  const std::int32_t group = object.convoy_group(true);
  return group != 0 && convoy_has_followers(group);
}

void Scenario::remove(EditorObject& object) {
  if (&object == objects_.front().get()) {
    throw std::invalid_argument("the player cannot be removed");
  }
  if (removal_releases_convoy(object)) {
    const std::int32_t group = object.convoy_group(true);
    release_escorts(group);
    release_members(group);
  }
  std::unique_ptr<EditorObject> removed = unlink(object);
  if (selected_ == removed.get()) {
    selected_ = objects_.front().get();
  }
  header.required_memory -= removed->memory_cost();
  if (!removed->model_name1().empty()) {
    header.required_memory -= cache_.release(removed->model_name1());
  }
  if (!removed->model_name2().empty()) {
    header.required_memory -= cache_.release(removed->model_name2());
  }
  modified = true;
}

std::unique_ptr<EditorObject> Scenario::unlink(EditorObject& object) {
  const std::size_t i = index_of(object);
  std::unique_ptr<EditorObject> out = std::move(objects_[i]);
  objects_.erase(objects_.begin() + static_cast<std::ptrdiff_t>(i));
  return out;
}

bool Scenario::move_object(EditorObject& object, WorldPoint p) {
  const auto inside = [&] {
    const World& w = world_->world();
    const std::int32_t cell = w.cell_size();
    return p.x >= kMapMargin && p.x <= w.grid.width * cell - kMapMargin && p.y >= kMapMargin &&
           p.y <= cell * w.grid.height - kMapMargin;
  };
  if (auto* player = dynamic_cast<PlayerSub*>(&object)) {
    // sbplayer_move_to (0x3ca10)
    const std::optional<std::size_t> cls = data_.find_sub(scn_text(player->rec.sub_class));
    const std::int32_t draft = cls ? static_cast<std::int32_t>(data_.tables.subs[*cls].draft_ft) : 0;
    const std::int32_t depth = water_depth_ft(p);
    if (!inside() || draft + 1 >= depth) {
      return false;
    }
    player->move_to(p, depth);
    update_all_visibility();
  } else if (auto* vessel = dynamic_cast<Vessel*>(&object)) {
    // sbvessel_move_to (0x3cb34)
    const std::optional<std::size_t> cls = data_.find_ship(vessel->class_name());
    const std::int32_t draft = cls ? static_cast<std::int32_t>(data_.tables.ships[*cls].draft_ft) : 0;
    const std::int32_t depth = water_depth_ft(p);
    if (!inside() || draft + 1 >= depth) {
      return false;
    }
    vessel->move_to(p, depth);
    vessel->update_detection(detection_env());
  } else if (auto* aircraft = dynamic_cast<Aircraft*>(&object)) {
    aircraft->move_to(p);
  }
  modified = true;
  return true;
}

bool Scenario::class_change_releases_convoy(const Vessel& vessel, std::size_t new_class) const {
  const std::optional<std::size_t> old_class = data_.find_ship(vessel.class_name());
  if (!old_class || *old_class == new_class) {
    return false;
  }
  const ShipClass& from = data_.tables.ships[*old_class];
  const ShipClass& to = data_.tables.ships[new_class];
  return role_group(from) != role_group(to) && vessel.ship.orders == ShipOrders::kConvoyLeader &&
         to.role != ShipRole::kWarship && convoy_has_followers(vessel.ship.convoy_group);
}

void Scenario::change_vessel_class(Vessel& vessel, std::size_t new_class) {
  // sbchart_change_ship_class (0x44114). An unknown current class reads
  // past SHIPS.DAT in the original; here it releases nothing.
  const std::optional<std::size_t> old_class = data_.find_ship(vessel.class_name());
  const ShipClass& to = data_.tables.ships[new_class];
  if (old_class) {
    const ShipClass& from = data_.tables.ships[*old_class];
    // Corrected, the orders the new class allows stay (not in the original).
    const ShipOrders kept = corrected_ && orders_fit_class(to, vessel.orders()) ? vessel.orders() : ShipOrders::kNone;
    if (*old_class != new_class && role_group(from) != role_group(to) && kept == ShipOrders::kNone) {
      if (vessel.ship.orders == ShipOrders::kConvoyLeader) {
        release_escorts(vessel.ship.convoy_group);
        release_members(vessel.ship.convoy_group);
      }
      vessel.ship.orders = ShipOrders::kNone;
      vessel.sub.orders = ShipOrders::kNone;
    }
    if (is_submarine_class(from) != is_submarine_class(to)) {
      const Waypoints course = vessel.waypoints();
      vessel.kind = is_submarine_class(to) ? Vessel::Kind::kSubmarine : Vessel::Kind::kShip;
      if (corrected_) {
        // The course and the orders kept go to the new live part (not in
        // the original, which shows the other part's).
        vessel.waypoints() = course;
        (vessel.kind == Vessel::Kind::kShip ? vessel.ship.orders : vessel.sub.orders) = kept;
      }
    }
    header.required_memory -= cache_.release(from.sprite1);
    header.required_memory -= cache_.release(from.sprite2);
  }
  if (vessel.kind == Vessel::Kind::kShip) {
    set_scn_text(vessel.ship.ship_class, to.name);
  } else {
    set_scn_text(vessel.sub.sub_class, to.name);
  }
  add_models(vessel, new_class);
}

void Scenario::change_aircraft_type(Aircraft& aircraft, std::size_t aircraft_type) {
  const AircraftType& type = data_.tables.aircraft[aircraft_type];
  set_scn_text(aircraft.rec.aircraft_type, type.name);
  aircraft.rec.bomb_load = static_cast<std::int32_t>(type.bombs);
  aircraft.rec.strafes = type.guns != 0 ? 1 : 0;
  header.required_memory -= cache_.release(aircraft.model);
  aircraft.model = type.sprite;
  header.required_memory += cache_.add(aircraft.model, data_.models);
  modified = true;
}

// ------------------------------------------------------------ convoys

std::int32_t Scenario::first_free_convoy_id() const {
  std::array<bool, 9> used{};
  for (const auto& o : objects_) {
    const std::int32_t g = o->convoy_group(true);
    if (g >= 0 && g <= 8) {
      used[static_cast<std::size_t>(g)] = true;
    }
  }
  for (std::int32_t id = 1; id <= 8; ++id) {
    if (!used[static_cast<std::size_t>(id)]) {
      return id;
    }
  }
  return -1;
}

bool Scenario::other_convoy_exists(std::int32_t id) const {
  for (std::int32_t other = 1; other <= 8; ++other) {
    if (other != id && convoy_leader(other) != nullptr) {
      return true;
    }
  }
  return false;
}

Vessel* Scenario::convoy_leader(std::int32_t id) const {
  if (id == 0) {
    // Every object but a ship answers 0 (the original returns the player:
    // a leader of convoy 0 in a damaged file).
    return nullptr;
  }
  for (const auto& o : objects_) {
    if (o->convoy_group(true) == id) {
      return static_cast<Vessel*>(o.get());  // only ships answer
    }
  }
  return nullptr;
}

bool Scenario::convoy_has_followers(std::int32_t id) const {
  return std::ranges::any_of(objects_,
                             [&](const auto& o) { return o->convoy_group(false) == id || o->escorted_group() == id; });
}

void Scenario::copy_convoy_course(std::int32_t id) {
  const Vessel* leader = convoy_leader(id);
  if (leader == nullptr) {
    return;
  }
  for (const auto& o : objects_) {
    if (o->convoy_group(false) == id) {
      auto& member = static_cast<Vessel&>(*o);
      member.ship.heading = leader->ship.heading;
      member.ship.speed = leader->ship.speed;
    }
  }
}

void Scenario::update_escort_speed(std::int32_t id) {
  const Vessel* leader = convoy_leader(id);
  if (leader == nullptr) {
    return;
  }
  for (const auto& o : objects_) {
    // Original bug (docs/editor-design.md "Escort speed of aircraft", fixed): SE
    // writes every escort as a ship record, so an escorting aircraft's x
    // got twice the leader's speed, capped by the top speed of a "class"
    // read from its bomb load (past the end of SHIPS.DAT): the aircraft
    // jumped to the chart's west edge. Only the escort ships change here.
    auto* escort = dynamic_cast<Vessel*>(o.get());
    if (escort != nullptr && o->escorted_group() == id) {
      escort->ship.speed = std::min(leader->ship.speed * 2, ship_max_speed(*escort));
    }
  }
}

std::int32_t Scenario::first_existing_convoy_id() const {
  for (std::int32_t id = 1; id <= 8; ++id) {
    if (convoy_leader(id) != nullptr) {
      return id;
    }
  }
  return 0;
}

std::int32_t Scenario::convoy_slowest_speed(std::int32_t id) const {
  const Vessel* leader = convoy_leader(id);
  if (leader == nullptr) {
    return 0;  // the original reads through a null leader
  }
  const auto top = [&](const ScnChars<25>& cls) {
    const std::optional<std::size_t> i = data_.find_ship(scn_text(cls));
    return i ? ship_top_speed(data_.tables.ships[*i]) : 0;
  };
  std::int32_t slowest = top(leader->ship.ship_class);
  for (const auto& o : objects_) {
    if (o->convoy_group(false) == id) {
      slowest = std::min(slowest, top(static_cast<const Vessel&>(*o).ship.ship_class));
    }
  }
  return slowest;
}

void Scenario::release_escorts(std::int32_t id) {
  if (id == 0) {
    return;
  }
  for (const auto& o : objects_) {
    if (o->escorted_group() != id) {
      continue;
    }
    if (auto* escort = dynamic_cast<Vessel*>(o.get())) {
      escort->ship.orders = ShipOrders::kPatrol;
      escort->ship.convoy_group = 0;
    } else if (auto* aircraft = dynamic_cast<Aircraft*>(o.get())) {
      aircraft->rec.escort_group = 0;
      aircraft->rec.mission = AircraftMission::kSearchOrbit2;
    }
  }
}

void Scenario::release_members(std::int32_t id) {
  if (id == 0) {
    return;
  }
  for (const auto& o : objects_) {
    if (o->convoy_group(false) == id) {
      auto& member = static_cast<Vessel&>(*o);
      member.ship.orders = ShipOrders::kNone;
      member.ship.convoy_group = 0;
    }
  }
}

std::int32_t Scenario::ship_max_speed(const Vessel& vessel) const {
  if (vessel.ship.orders == ShipOrders::kConvoyLeader) {
    return convoy_slowest_speed(vessel.ship.convoy_group);
  }
  const std::optional<std::size_t> cls = data_.find_ship(scn_text(vessel.ship.ship_class));
  return cls ? ship_top_speed(data_.tables.ships[*cls]) : 0;
}

void Scenario::apply_convoy_membership(Vessel& v) {
  ScnShip& ship = v.ship;
  if (ship.convoy_group == 0) {
    ship.convoy_group = first_existing_convoy_id();
    if (ship.convoy_group == 0) {
      ship.orders = ShipOrders::kNone;
    }
  }
  if (ship.orders == ShipOrders::kConvoyMember && ship.convoy_group != 0) {
    if (const Vessel* leader = convoy_leader(ship.convoy_group)) {
      v.sub.heading = ship.heading = leader->ship.heading;
      v.sub.waypoints.count = 0;
      v.sub.speed = ship.speed = leader->ship.speed;
      // Original bug (docs/editor-design.md "Convoy member join", fixed): SE then
      // zeroes the sub part's speed and copies it into the ship's damage,
      // so a ship joining a convoy lost its damage.
      ship.waypoints.count = v.sub.waypoints.count;
      modified = true;
    }
  }
  if (ship.orders == ShipOrders::kEscort && ship.convoy_group != 0) {
    if (const Vessel* leader = convoy_leader(ship.convoy_group)) {
      v.sub.speed = ship.speed = leader->ship.speed * 2;
      const std::int32_t max = ship_max_speed(v);
      if (max < ship.speed) {
        v.sub.speed = ship.speed = max;
      }
      modified = true;
    }
  }
}

bool Scenario::pick_convoy(EditorObject& selected, const EditorObject& picked) {
  const auto* source = dynamic_cast<const Vessel*>(&picked);
  if (source == nullptr || source->kind != Vessel::Kind::kShip) {
    return false;
  }
  if (auto* ship = dynamic_cast<Vessel*>(&selected)) {
    if (ship->kind != Vessel::Kind::kShip) {
      return false;
    }
    ship->ship.convoy_group = source->ship.convoy_group;
    apply_convoy_membership(*ship);
  } else if (auto* aircraft = dynamic_cast<Aircraft*>(&selected)) {
    aircraft->rec.escort_group = source->ship.convoy_group;
  } else {
    return false;
  }
  modified = true;
  return true;
}

// ------------------------------------------------------------ courses

void Scenario::start_course(Waypoints& course, WorldPoint start) {
  if (course.count == 0) {
    course.x[0] = start.x;
    course.y[0] = start.y;
    course.count = 1;
  }
}

CourseClick Scenario::add_course_point(Waypoints& course, WorldPoint p, bool looped, bool closes_loop) {
  if (course.count >= kMaxCoursePoints) {
    return CourseClick::kFull;
  }
  bool add = true;
  if (!looped && course.count > 2 && closes_loop) {
    looped = true;
    add = false;
  }
  if (add) {
    course.x[static_cast<std::size_t>(course.count)] = p.x;
    course.y[static_cast<std::size_t>(course.count)] = p.y;
    ++course.count;
  }
  course.x[static_cast<std::size_t>(course.count)] = looped ? -1 : 0;
  return add ? CourseClick::kAdded : CourseClick::kClosed;
}

bool Scenario::add_aircraft_point(Aircraft& aircraft, WorldPoint p) {
  Waypoints& path = aircraft.rec.waypoints;
  if (path.count >= kMaxCoursePoints) {
    return false;
  }
  if (path.count == 0) {
    aircraft.rec.x = p.x;
    aircraft.rec.y = p.y;
  }
  path.x[static_cast<std::size_t>(path.count)] = p.x;
  path.y[static_cast<std::size_t>(path.count)] = p.y;
  ++path.count;
  path.x[static_cast<std::size_t>(path.count)] = -1;
  return true;
}

// ------------------------------------------------------------ visibility

DetectionEnv Scenario::detection_env() const {
  return DetectionEnv{
      .data = data_,
      .grid = &world_->grid(),
      .player = player().rec,
      .conditions = SightingConditions{.weather = static_cast<std::int32_t>(header.weather),
                                       .fog_pct = sighting_fog_pct,
                                       .light_pct = sighting_light_pct},
      .equipment = header.equipment,
  };
}

void Scenario::update_all_visibility() {
  const DetectionEnv env = detection_env();
  for (const auto& o : objects_) {
    o->update_detection(env);
  }
}

}  // namespace sh::editor
