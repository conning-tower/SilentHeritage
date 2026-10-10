// The strategic contact groups of a career patrol over the Southern Japan
// zone (SJAPAN.CR, MAP1BASE.DAT): route spawns, group kinds, base groups,
// sampans, materialization through the scenario generator and
// dematerialization, with a synthetic spawner.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "assets/world.h"
#include "game/patrol_contacts.h"
#include "game/scengen.h"
#include "game_data.h"
#include "sim/geometry.h"

namespace sh {

namespace {

// A vehicle standing for a spawned unit.
class FakeVehicle final : public Vehicle {
 public:
  VehicleClass class_id() const override { return VehicleClass::kShip; }
  void init(SimContext& /*ctx*/) override {}
  std::int32_t update_pass() const override { return 0; }
  void update(SimContext& /*ctx*/, Fixed16 /*dt*/) override {}
  fx::Vec3 position() const override { return {}; }
};

// Records the scenarios spawned and keeps one fake vehicle per unit.
class FakeSpawner final : public ContactSpawner {
 public:
  std::int32_t free_convoy_group() const override { return 1; }
  std::vector<Vehicle*> spawn(const Scenario& units) override {
    spawned.push_back(units);
    std::vector<Vehicle*> out;
    const std::size_t n = units.ships.size() + units.enemy_subs.size() + units.aircraft.size();
    for (std::size_t i = 0; i < n; ++i) {
      live.push_back(std::make_unique<FakeVehicle>());
      out.push_back(live.back().get());
    }
    return out;
  }
  void remove(Vehicle& vehicle) override {
    std::erase_if(live, [&](const std::unique_ptr<FakeVehicle>& v) { return v.get() == &vehicle; });
    ++removed;
  }
  bool contains(const Vehicle* vehicle) const override {
    return std::ranges::any_of(live, [&](const std::unique_ptr<FakeVehicle>& v) { return v.get() == vehicle; });
  }
  void limit_compression(std::int32_t max) override { cap = max; }

  std::vector<Scenario> spawned;
  std::vector<std::unique_ptr<FakeVehicle>> live;
  std::int32_t removed = 0;
  std::int32_t cap = 0;
};

struct Fixture {
  explicit Fixture(const Install& install)
      : tables(GameTables::load(install)),
        zone(ZoneData::load(install, parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT").at(0))),
        world(parse_world(install.read("DATA/WORLD-1.WLD"), "WORLD-1.WLD")) {}

  GameTables tables;
  ZoneData zone;
  World world;
  FakeSpawner spawner;
};

// A view from far off the map: nothing materializes.
ContactView far_view(std::int32_t hundredths) {
  ContactView v;
  v.player_x = -0x40000000;
  v.player_z = -0x40000000;
  v.frame_hundredths = hundredths;
  return v;
}

std::vector<const ContactGroup*> of_kind(const ContactGroups& groups, ContactGroupKind kind) {
  std::vector<const ContactGroup*> out;
  for (const auto& g : groups.groups()) {
    if (g->kind == kind && !g->finished) {
      out.push_back(g.get());
    }
  }
  return out;
}

GameClock clock_on(std::int32_t year, std::int32_t month, std::int32_t day) {
  GameClock c;
  c.init(year, month, day, 12, 0);
  return c;
}

}  // namespace

TEST_CASE("Contact groups: routes spawn their groups on their timers", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.zone.bases.clear();
  f.zone.owners.clear();
  f.zone.zone.sampan_density = 0;
  ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
  WatcomRandom random(1234);
  const GameClock clock = clock_on(13, 2, 15);  // 15 March 1943
  groups.init(clock, random);
  // Every SJAPAN route runs in March 1943; the first group is due within
  // 2 x 0x2a30 s.
  REQUIRE(groups.routes().size() == 19);
  for (const RouteState& s : groups.routes()) {
    CHECK(s.active);
    CHECK(s.spawn_timer >= 0);
    CHECK(s.spawn_timer < 2 * 0x2a30);
  }
  CHECK(groups.groups().empty());
  // A minute later only the routes whose timer ran out have a group.
  std::int32_t due = 0;
  for (const RouteState& s : groups.routes()) {
    due += s.spawn_timer < 61 && s.frequency != 0 ? 1 : 0;
  }
  for (int i = 0; i < 60; ++i) {
    groups.update(clock, far_view(100), random);
  }
  CHECK(static_cast<std::int32_t>(of_kind(groups, ContactGroupKind::kRoute).size()) == due);
  // After 6 hours every route has spawned and re-armed its timer.
  for (int i = 0; i < 6 * 3600; ++i) {
    groups.update(clock, far_view(100), random);
  }
  for (const RouteState& s : groups.routes()) {
    CHECK(s.spawn_timer > 0);
  }
  // A group's waypoints are points of its route's boxes, in order or reversed.
  const auto routes = of_kind(groups, ContactGroupKind::kRoute);
  REQUIRE(!routes.empty());
  for (const ContactGroup* g : routes) {
    const std::vector<RouteBox>& boxes = f.zone.routes[static_cast<std::size_t>(g->route)].waypoints;
    REQUIRE(g->waypoints.size() == boxes.size());
    const auto inside = [](const GroupPoint& p, const RouteBox& b) {
      return std::min(b.x0, b.x1) <= p.x && p.x <= std::max(b.x0, b.x1) && std::min(b.z0, b.z1) <= p.z &&
             p.z <= std::max(b.z0, b.z1);
    };
    const bool forward = inside(g->waypoints.front(), boxes.front()) && inside(g->waypoints.back(), boxes.back());
    for (std::size_t i = 0; i < boxes.size(); ++i) {
      CHECK(inside(g->waypoints[i], boxes[forward ? i : boxes.size() - 1 - i]));
    }
    CHECK(g->speed > 0);
  }
}

TEST_CASE("Contact groups: route group kinds follow the convoy chance", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  // -1: never a convoy (0 still gives one on a roll of 0).
  for (const std::int32_t pct : {100, -1}) {
    Fixture f(install);
    f.zone.bases.clear();
    f.zone.owners.clear();
    f.zone.zone.sampan_density = 0;
    for (ConvoyRoute& r : f.zone.routes) {
      r.convoy_pct.fill(pct);
    }
    ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
    WatcomRandom random(99);
    const GameClock clock = clock_on(13, 2, 15);
    groups.init(clock, random);
    for (int i = 0; i < 6 * 360; ++i) {
      groups.update(clock, far_view(1000), random);
    }
    const auto routes = of_kind(groups, ContactGroupKind::kRoute);
    REQUIRE(routes.size() >= 10);
    std::int32_t subs = 0;
    for (const ContactGroup* g : routes) {
      if (g->route_kind == RouteGroupKind::kSubmarines) {
        ++subs;
        CHECK(g->aircraft == 0);
        CHECK(g->size >= 1);
        CHECK(g->size <= 3);
        continue;
      }
      CHECK(g->route_kind == (pct == 100 ? RouteGroupKind::kConvoy : RouteGroupKind::kTaskForce));
      if (g->route_kind == RouteGroupKind::kConvoy) {
        // 9..11 kn, 14..16 kn when fast.
        CHECK(g->speed >> 16 >= (g->fast_or_intercept ? 14 : 9));
        CHECK(g->speed >> 16 <= (g->fast_or_intercept ? 16 : 11));
      } else {
        CHECK(g->speed >> 16 >= 18);
        CHECK(g->speed >> 16 <= 27);
      }
    }
    // The 2 % roll.
    CHECK(subs * 10 < static_cast<std::int32_t>(routes.size()));
  }
}

TEST_CASE("Contact groups: the bases spawn anchored ships and patrols", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.zone.routes.clear();
  f.zone.zone.sampan_density = 0;
  // Kure held by the Allies: no group there.
  for (BaseOwnerPeriod& p : f.zone.bases[1].owners) {
    p.side = BaseSide::kAllied;
  }
  ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
  WatcomRandom random(7);
  const GameClock clock = clock_on(13, 2, 15);
  groups.init(clock, random);
  groups.update_bases(clock, random, false);
  const auto anchored = of_kind(groups, ContactGroupKind::kAnchored);
  // Every active base but Kure.
  REQUIRE(anchored.size() == f.zone.bases.size() - 1);
  for (const ContactGroup* g : anchored) {
    const auto base = static_cast<std::size_t>(g->base);
    CHECK(base != 1);
    CHECK(g->position.x == f.zone.bases[base].harbor.x);
    CHECK(g->position.z == f.zone.bases[base].harbor.z);
    CHECK(g->heading == f.zone.owners[base].harbor_heading.tenths());
    CHECK(g->guard_size == std::min(f.zone.bases[base].anchored_size, f.zone.bases[base].anchored_size_cap));
    CHECK(g->merchant_pct == (f.zone.bases[base].has(BaseFlag::kSmallGuard) ? 20 : 100));
  }
  // Patrols only from the bases that send them, always at 100 %.
  const auto patrols = of_kind(groups, ContactGroupKind::kPatrol);
  for (const ContactGroup* g : patrols) {
    const BaseRecord& b = f.zone.bases[static_cast<std::size_t>(g->base)];
    CHECK(b.has(BaseFlag::kSpawnsPatrols));
    CHECK(b.patrol_spawn_pct > 0);
    CHECK(g->position.x == b.patrol.x);
    CHECK(g->patrol_size == b.patrol_param_a);
  }
  for (std::size_t i = 0; i < f.zone.bases.size(); ++i) {
    const BaseRecord& b = f.zone.bases[i];
    if (b.has(BaseFlag::kSpawnsPatrols) && b.patrol_spawn_pct >= 100) {
      CHECK(std::ranges::any_of(patrols, [&](const ContactGroup* g) { return g->base == static_cast<std::int32_t>(i); }));
    }
  }
  // The same day again changes nothing; a forced update retires the groups
  // still on the strategic map and spawns new ones.
  const std::size_t before = groups.groups().size();
  groups.update_bases(clock, random, false);
  CHECK(groups.groups().size() == before);
  groups.update_bases(clock, random, true);
  CHECK(of_kind(groups, ContactGroupKind::kAnchored).size() == anchored.size());
}

TEST_CASE("Contact groups: sampans in open water", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.zone.routes.clear();
  f.zone.bases.clear();
  f.zone.owners.clear();
  ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
  WatcomRandom random(5);
  groups.init(clock_on(13, 2, 15), random);
  // density 100: 25 + rand(12) groups.
  const auto sampans = of_kind(groups, ContactGroupKind::kSampans);
  CHECK(sampans.size() >= 25);
  CHECK(sampans.size() < 37);
  const std::int32_t cell = f.world.cell_size();
  for (const ContactGroup* g : sampans) {
    const std::int32_t column = g->position.x / cell;
    const std::int32_t row = f.world.grid.height - g->position.z / cell;
    CHECK(f.world.grid.at(column, row) <= 0x1f);
    CHECK(g->sampan_size >= 0);
    CHECK(g->sampan_size <= 2);
  }
  // The next day's sampans replace them. The finished groups leave the
  // list over several updates: removing by index while counting up skips
  // the group after each one removed.
  groups.update(clock_on(13, 2, 16), far_view(100), random);
  std::size_t old_left = 0;
  for (const auto& g : groups.groups()) {
    if (std::ranges::find(sampans, g.get()) != sampans.end()) {
      ++old_left;
      CHECK(g->finished);
    }
  }
  CHECK(old_left > 0);
  CHECK(old_left < sampans.size());
  CHECK(of_kind(groups, ContactGroupKind::kSampans).size() >= 25);
}

TEST_CASE("Contact groups: anchored ships materialize in range and leave when lost", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.zone.routes.clear();
  f.zone.zone.sampan_density = 0;
  // Hiroshima alone: anchored ships (3, aircraft) and a patrol.
  f.zone.bases.resize(1);
  f.zone.owners.resize(1);
  const BaseRecord& base = f.zone.bases[0];
  ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
  WatcomRandom random(42);
  const GameClock clock = clock_on(13, 2, 15);
  groups.init(clock, random);
  groups.update_bases(clock, random, false);
  REQUIRE(of_kind(groups, ContactGroupKind::kAnchored).size() == 1);
  // The first update clears the new groups' block.
  groups.update(clock, far_view(0), random);
  const ContactGroup& g = *of_kind(groups, ContactGroupKind::kAnchored).front();

  // The boat 9500 yd from the harbour, away from the patrol point, with a
  // 10000 yd radar: inside the range, outside its inner 7/8.
  const Angle10 away = fx::heading_from_delta(base.harbor.x - base.patrol.x, base.harbor.z - base.patrol.z);
  const fx::SinCos r = fx::angle_sincos(away);
  ContactView view;
  view.player_x = base.harbor.x + fx::mul(9500 * 0x30, r.sin);
  view.player_z = base.harbor.z + fx::mul(9500 * 0x30, r.cos);
  view.sj_range_yd = 10000;
  REQUIRE(fx::approx_distance(base.patrol.x - view.player_x, base.patrol.z - view.player_z) / 0x30 > 10000);
  groups.update(clock, view, random);
  REQUIRE(g.materialized);
  REQUIRE(f.spawner.spawned.size() == 1);
  const Scenario first = f.spawner.spawned[0];
  CHECK(g.spawned == static_cast<std::int32_t>(f.spawner.live.size()));
  CHECK(!first.ships.empty());
  // Hiroshima has aircraft with its anchored ships: as many as its size.
  CHECK(first.aircraft.size() == static_cast<std::size_t>(base.anchored_size));
  for (const ShipRecord& s : first.ships) {
    CHECK(f.tables.find_ship(s.ship_class) != nullptr);
    CHECK(fx::approx_distance(s.x - base.harbor.x, s.y - base.harbor.z) / 0x30 < 20000);
  }
  // Seen this frame: it stays.
  for (const auto& v : f.spawner.live) {
    groups.detected(*v);
    break;
  }
  groups.update(clock, view, random);
  CHECK(g.materialized);
  // Lost: its vehicles leave; anchored ships are never finished.
  const auto n = static_cast<std::int32_t>(f.spawner.live.size());
  groups.update(clock, view, random);
  CHECK(!g.materialized);
  CHECK(!g.finished);
  CHECK(f.spawner.removed == n);
  CHECK(f.spawner.live.empty());
  // Back in range: the same ships again (the group's seed).
  groups.update(clock, view, random);
  REQUIRE(g.materialized);
  REQUIRE(f.spawner.spawned.size() == 2);
  const Scenario& second = f.spawner.spawned[1];
  REQUIRE(second.ships.size() == first.ships.size());
  for (std::size_t i = 0; i < first.ships.size(); ++i) {
    CHECK(second.ships[i].ship_class == first.ships[i].ship_class);
    CHECK(second.ships[i].x == first.ships[i].x);
  }
}

TEST_CASE("Contact groups: a patrol mostly sunk is finished", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.zone.routes.clear();
  f.zone.zone.sampan_density = 0;
  f.zone.bases.resize(1);
  f.zone.owners.resize(1);
  const BaseRecord& base = f.zone.bases[0];
  ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
  WatcomRandom random(3);
  const GameClock clock = clock_on(13, 2, 15);
  groups.init(clock, random);
  groups.update_bases(clock, random, false);
  REQUIRE(of_kind(groups, ContactGroupKind::kPatrol).size() == 1);
  groups.update(clock, far_view(0), random);
  const ContactGroup* patrol = of_kind(groups, ContactGroupKind::kPatrol).front();

  ContactView view;
  view.player_x = base.patrol.x + 9500 * 0x30;
  view.player_z = base.patrol.z;
  view.sj_range_yd = 10000;
  // The harbour (anchored ships) must stay out of range.
  REQUIRE(fx::approx_distance(base.harbor.x - view.player_x, base.harbor.z - view.player_z) / 0x30 > 10000);
  groups.update(clock, view, random);
  REQUIRE(patrol->materialized);
  REQUIRE(f.spawner.spawned.size() == 1);
  for (const ShipRecord& s : f.spawner.spawned[0].ships) {
    CHECK(s.orders == ShipOrders::kPatrol);
    CHECK(f.tables.find_ship(s.ship_class)->role == ShipRole::kEscort);
  }
  // Everything sunk, then lost: the patrol is gone. Being within
  // 40000 yd, it capped the time compression first.
  f.spawner.live.clear();
  groups.update(clock, view, random);
  CHECK(f.spawner.cap == 0x200);
  CHECK(of_kind(groups, ContactGroupKind::kPatrol).empty());
}

TEST_CASE("Contact groups: a convoy materializes with its escorts on a free group id", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.zone.bases.clear();
  f.zone.owners.clear();
  f.zone.zone.sampan_density = 0;
  for (ConvoyRoute& r : f.zone.routes) {
    r.convoy_pct.fill(100);
  }
  ContactGroups groups(f.tables, f.zone, f.world, f.spawner);
  WatcomRandom random(11);
  const GameClock clock = clock_on(13, 2, 15);
  groups.init(clock, random);
  const ContactGroup* convoy = nullptr;
  while (convoy == nullptr) {
    groups.update(clock, far_view(1000), random);
    for (const ContactGroup* g : of_kind(groups, ContactGroupKind::kRoute)) {
      if (g->route_kind == RouteGroupKind::kConvoy && !g->blocked) {
        convoy = g;
      }
    }
  }
  ContactView view;
  view.player_x = convoy->position.x + 9500 * 0x30;
  view.player_z = convoy->position.z;
  view.sj_range_yd = 10000;
  groups.update(clock, view, random);
  REQUIRE(convoy->materialized);
  REQUIRE(f.spawner.spawned.size() == 1);
  const Scenario& units = f.spawner.spawned[0];
  std::int32_t leaders = 0;
  for (const ShipRecord& s : units.ships) {
    CHECK(s.convoy_group == 1);
    leaders += s.orders == ShipOrders::kConvoyLeader ? 1 : 0;
    const ShipClass* c = f.tables.find_ship(s.ship_class);
    REQUIRE(c != nullptr);
    CHECK((c->role == ShipRole::kMerchant) == (s.orders != ShipOrders::kEscort));
  }
  CHECK(leaders == 1);
  CHECK(units.aircraft.size() == static_cast<std::size_t>(convoy->aircraft));
  for (const AircraftRecord& a : units.aircraft) {
    CHECK(a.escort_group == 1);
  }
  // Route groups are finished once lost.
  groups.update(clock, view, random);
  const auto left = of_kind(groups, ContactGroupKind::kRoute);
  CHECK(std::ranges::find(left, convoy) == left.end());
}

}  // namespace sh
