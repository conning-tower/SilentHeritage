// Simulation regressions: behaviours where the port departed
// from the original (crashes, random-draw order, update order, quirks).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <utility>
#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "core/random.h"
#include "game/class_params.h"
#include "game_data.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/ai_ship.h"
#include "sim/deck_gun.h"
#include "sim/depth_charge.h"
#include "sim/enemy_sub.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/radar.h"
#include "sim/sensors.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/ship_weapons.h"
#include "sim/sighting.h"
#include "sim/simulation.h"
#include "sim/tdc.h"
#include "sim/torpedo.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kOrigin = 4000000;
constexpr std::int32_t kYard = kUnitsPerYard;

// A mission with the player's boat of class `boat` stopped at the origin
// heading north, `depth_ft` deep, with its combat state.
struct BoatRig {
  BoatRig(const GameTables& t, const char* boat = "GATO", std::int32_t depth_ft = 0, std::uint32_t options = 0)
      : tables(t), sub(*t.find_sub(boat)), sim(conditions(options), kDeepSea, 7), combat(t, sub) {
    sim.clock().init(12, 5, 1, 12, 0);
    sim.set_player_combat(&combat);
    player = &sim.add(std::make_unique<PlayerSub>(sub_class_params(sub),
                                                  fx::Vec3{kOrigin, -depth_ft * kUnitsPerFoot, kOrigin}, Angle10(0), 0));
  }

  static MissionConditions conditions(std::uint32_t options) {
    MissionConditions env;
    env.options = options;
    return env;
  }

  // Adds a ship of `name` at (dx, dz) yards from the boat.
  Ship& add_ship(const char* name, std::int32_t dx, std::int32_t dz, Angle10 heading, std::int32_t knots,
                 ShipOrder orders) {
    const ShipClass& cls = *tables.find_ship(name);
    auto ship = std::make_unique<Ship>(ship_motion_params(cls), fx::Vec3{kOrigin + dx * kYard, 0, kOrigin + dz * kYard},
                                       heading, Fixed16::from_int(knots), orders);
    ship->setup().ship_class = &cls;
    ship->setup().hit_points = static_cast<std::int32_t>(cls.hull_strength);
    return sim.add(std::move(ship));
  }

  template <typename T>
  std::vector<const T*> all() const {
    std::vector<const T*> out;
    for (const auto& v : sim.vehicles()) {
      if (const T* t = dynamic_cast<const T*>(v.get())) {
        out.push_back(t);
      }
    }
    return out;
  }

  std::int32_t player_shells() const {
    std::int32_t n = 0;
    for (const Shell* s : all<Shell>()) {
      n += s->owner() == player ? 1 : 0;
    }
    return n;
  }

  const GameTables& tables;
  const SubClass& sub;
  Simulation sim;
  PlayerCombat combat;
  PlayerSub* player = nullptr;
};

}  // namespace

TEST_CASE("A TYPE C escort throws its twelve throwers' pattern", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  Ship& escort = p.add_ship("TYPE C", 0, 3000, Angle10(0), 0, ShipOrder::kNone);
  REQUIRE(escort.ai().cls.rack_charges == 1);
  REQUIRE(escort.ai().cls.thrower_charges == 12);
  escort.ai().depth_charges = 20;
  ShipWeapons weapons(tables);
  SimContext ctx = p.sim.context();
  weapons.drop_depth_charges(escort, ctx, 100);
  CHECK(escort.ai().depth_charges == 7);
  const std::vector<const DepthCharge*> charges = p.all<DepthCharge>();
  REQUIRE(charges.size() == 13);
  // Astern, then the throwers' angles of 0x102fe8 off the stern.
  const std::array<std::int32_t, 12> angles = {45, -45, 90, -90, 25, -25, 65, -65, 10, -10, 80, -80};
  CHECK(charges[0]->flight_heading().tenths() == 1800);
  for (std::size_t k = 0; k < angles.size(); ++k) {
    CHECK(charges[k + 1]->flight_heading().tenths() == 1800 + angles[k] * 10);
  }
}

TEST_CASE("A bomber group radios its strike once, its report gone or not", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  std::vector<BaseRecord> bases(1);
  bases[0].station = {kOrigin + 10000 * kYard, 0, kOrigin};
  bases[0].harbor = {kOrigin + 11000 * kYard, 0, kOrigin};
  std::vector<Location> locations(1);
  locations[0] = {LocationType::kPortFacilities, kOrigin + 9000 * kYard, 0, kOrigin, Angle10(0)};
  AirTheatre theatre;
  theatre.bases = bases;
  theatre.locations = locations;
  theatre.grid_width = 200;
  theatre.grid_height = 200;
  theatre.scale_shift = 0;
  theatre.air_edges = {0, 0, 1, 0};
  AirGroup& group = p.sim.add(std::make_unique<AirGroup>(theatre, 0, 6, 0, 0));
  std::int32_t strikes = 0;
  bool report_gone = false;
  bool alive = true;
  for (int frame = 0; frame < 400000 && alive; ++frame) {
    p.sim.step(10);
    for (const RadioReport& r : p.sim.take_events().radio_reports) {
      strikes += r.kind == RadioReport::Kind::kAirStrike ? 1 : 0;
    }
    alive = false;
    for (const auto& v : p.sim.vehicles()) {
      alive = alive || v.get() == &group;
    }
    if (alive && group.strike_done() && group.report() == nullptr) {
      report_gone = true;
      CHECK(group.report_created());
    }
  }
  // airgroup_fly removes the report after the strike; +0x1a8 stays set.
  CHECK(report_gone);
  CHECK(strikes == 1);
}

TEST_CASE("The AI's thinking frame follows the vehicle's current list index", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  Ship& first = p.add_ship("LFREIGHT", 0, 20000, Angle10(0), 0, ShipOrder::kNone);
  Ship& second = p.add_ship("LFREIGHT", 0, 21000, Angle10(0), 0, ShipOrder::kNone);
  CHECK(second.list_index() == 2);
  first.remove();
  p.sim.step(10);
  p.sim.step(10);
  // veh_simulate_pass writes +0xc4 = index before every update.
  CHECK(p.sim.vehicles().size() == 2);
  CHECK(second.list_index() == 1);
}

TEST_CASE("The vehicle after a removed one misses its hydrophone turn", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  Ship& a = p.add_ship("LFREIGHT", 0, 20000, Angle10(0), 0, ShipOrder::kNone);
  Ship& b = p.add_ship("LFREIGHT", 0, 21000, Angle10(0), 0, ShipOrder::kNone);
  p.add_ship("LFREIGHT", 0, 22000, Angle10(0), 0, ShipOrder::kNone);
  a.remove();
  b.remove();
  p.sim.step(10);
  // The original's deletion loop skips the vehicle moved into the freed
  // place: `b` goes one frame later.
  CHECK(p.sim.vehicles().size() == 3);
  CHECK(p.sim.vehicles()[1].get() == &b);
  p.sim.step(10);
  CHECK(p.sim.vehicles().size() == 2);
}

TEST_CASE("Manned AA guns fire at a near aircraft and are secured under water", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  p.sim.sensors().environment().light_pct = 100;
  auto plane = std::make_unique<Aircraft>();
  plane->setup().type = tables.find_aircraft("VAL");
  plane->setup().crew = 1;
  plane->setup().mission = AircraftMissionCode::kPatrol;
  plane->setup().orbit_x = kOrigin;
  plane->setup().orbit_z = kOrigin;
  plane->setup().orbit_radius = 300 * kYard;
  plane->setup().sorties = 10000;
  Aircraft& a = p.sim.add(std::move(plane));
  SimContext ctx = p.sim.context();
  a.take_off(ctx, {kOrigin, 0x12c0, kOrigin + 500 * kYard}, 30000);
  p.combat.crew = 2;
  p.combat.aa_guns.manned = true;
  std::int32_t shots = 0;
  for (int frame = 0; frame < 2000 && shots == 0; ++frame) {
    p.sim.step(5);
    shots = p.player_shells();
  }
  REQUIRE(shots > 0);
  const Shell& round = *p.all<Shell>().back();
  CHECK(round.caliber() == 0);
  // 0.5 s for an elite crew plus rand(0.5 s), less the sub-steps since.
  CHECK(p.combat.aa_guns.fire_timer <= 0x10000);
  CHECK(p.combat.aa_guns.fire_timer > 0x8000 - 0x1000);

  // Under water: secured with crew message #63.
  BoatRig deep(tables, "GATO", 100);
  deep.combat.aa_guns.manned = true;
  deep.sim.step(5);
  CHECK_FALSE(deep.combat.aa_guns.manned);
  bool secured = false;
  for (const CrewReport& r : deep.sim.take_events().crew_messages) {
    secured = secured || r.id == 63;
  }
  CHECK(secured);
}

TEST_CASE("Ramming draws its damage before the radar sweeps", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  // The same ram with the SJ radar off and on (a new blip draws rand(3600)).
  auto ram = [&](bool radar) {
    BoatRig p(tables, "GATO", 0, 0x10000);
    Ship& ship = p.add_ship("LFREIGHT", 0, 60, Angle10(900), 0, ShipOrder::kNone);
    RadarSets::Sj& sj = p.sim.sensors().radar().sj();
    sj.mode = radar ? SjMode::kAScope : SjMode::kOff;
    sj.hold = true;
    sj.beam = 0;
    sj.blips.clear();
    p.player->collision().collided = true;
    p.player->collision().partner = &ship;
    p.sim.step(2);
    std::vector<std::int32_t> damage;
    for (const SystemState& s : p.player->systems()) {
      damage.push_back(s.flags);
      damage.push_back(s.severity);
    }
    for (const CompartmentState& c : p.combat.damage.compartments()) {
      damage.push_back(c.flags);
    }
    damage.push_back(p.combat.damage.hull_damage());
    return std::pair(damage, std::pair(p.sim.random().rand(), static_cast<std::int32_t>(sj.blips.size())));
  };
  const auto quiet = ram(false);
  const auto sweeping = ram(true);
  REQUIRE(sweeping.second.second > 0);
  REQUIRE(sweeping.second.first != quiet.second.first);
  CHECK(sweeping.first == quiet.first);
}

TEST_CASE("The player's damage pass runs on the first step", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables, "GATO", 100);
  WatcomRandom replay = p.sim.random();
  SimContext ctx = p.sim.context();
  // player_init sets 0x124ed4 to one second: the crush depth is rolled at once.
  p.combat.update_damage(ctx, *p.player, Fixed16::from_raw(0x51e));
  const std::int32_t test = p.sub.test_depth_ft;
  CHECK(p.combat.damage.crush_depth() == test + test * 60 / 100 + 10 - replay.random_int(40));
}

TEST_CASE("An enemy submarine's power cap wraps as the original's", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  ShipClass cls = *tables.find_ship("KAIDAI TYPE");
  cls.max_power = Fixed16::from_raw(30000000);
  const auto strength = static_cast<std::int32_t>(cls.hull_strength);
  REQUIRE(strength == 10);
  MissionConditions env;
  GameClock clock;
  WatcomRandom random(1);
  MissionEvents events;
  SimContext ctx{clock, random, kDeepSea, env, events, nullptr};
  EnemySub sub(fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), Fixed16::from_int(5), ShipOrder::kNone, 1);
  sub.setup().ship_class = &cls;
  sub.setup().torpedo = tables.find_torpedo("TYPE95-1");
  sub.setup().hit_points = strength;
  sub.init(ctx);
  sub.setup().hit_points = strength - 1;
  sub.update(ctx, Fixed16::from_raw(0x51e));
  // 90 x 30000000 wraps (0x7f9ff).
  const auto wrapped = static_cast<std::int32_t>(90u * 30000000u);
  CHECK(sub.motion().ordered_power == wrapped / 100);
}

TEST_CASE("The deck-gun crew fires a mount on its own solution quality", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const SubClass* two_guns = nullptr;
  for (const SubClass& s : tables.subs) {
    if (s.deck_gun_count == 2 && two_guns == nullptr) {
      two_guns = &s;
    }
  }
  REQUIRE(two_guns != nullptr);
  BoatRig p(tables, two_guns->index_name.c_str());
  Ship& target = p.add_ship("LFREIGHT", 0, 3000, Angle10(900), 0, ShipOrder::kNone);
  DeckGun& gun = p.combat.deck_gun;
  gun.reset(*two_guns, 50, p.player->systems());
  SimContext ctx = p.sim.context();
  gun.set_manned(ctx, *p.player, false, false);
  gun.set_sighting_range(20000);
  gun.set_target(&target);
  // The aft mount is selected and cannot bear (the tower is in the way).
  gun.select(1);
  for (int frame = 0; frame < 4; ++frame) {
    gun.update(ctx, *p.player, p.combat.own_boat(*p.player, ctx), 5, {});
  }
  CHECK(gun.quality() == 100);
  // The forward mount fires on its quality (the original's local), not on
  // the selected mount's (0 while the forward mount is processed).
  CHECK(gun.mount(0).reload > 0);
  CHECK(p.player_shells() > 0);
}

TEST_CASE("The deck guns start trained fore and aft", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  DeckGun& gun = p.combat.deck_gun;
  gun.mount(0).train = Angle10(300);
  gun.mount(1).elevation = Angle10(100);
  gun.mount(1).reload = 500;
  gun.reset(p.sub, 50, p.player->systems());
  CHECK(gun.mount(0).train.tenths() == 0);
  CHECK(gun.mount(1).train.tenths() == 1800);
  CHECK(gun.mount(1).elevation.tenths() == 0);
  CHECK(gun.mount(1).reload == 0);
}

TEST_CASE("A ship's lookout check reads its position after the move", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  Ship& ship = p.add_ship("FUBUKI", 0, 3000, Angle10(1800), 30, ShipOrder::kFollowPath);
  const fx::Vec3 before = ship.position();
  SimContext ctx = p.sim.context();
  ship.update(ctx, Fixed16::from_int(10));
  const fx::Vec3 after = ship.position();
  REQUIRE(after.z < before.z - 100 * kYard);
  const SensorEnvironment& env = p.sim.sensors().environment();
  auto distance_at = [&](const fx::Vec3& pos) {
    return player_sighting_check(player_lookout_state(*p.player, env.crew), {0, 0, 100}, env.grid, pos.x,
                                 ship.visual_height(), pos.z, ship.exposure())
        .distance_yd;
  };
  REQUIRE(distance_at(after) != distance_at(before));
  CHECK(ship.ai().player_distance_yd == distance_at(after));
}

TEST_CASE("A strafing run normalises a heading wound past two turns", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  p.sim.sensors().environment().light_pct = 100;
  auto plane = std::make_unique<Aircraft>();
  plane->setup().type = tables.find_aircraft("VAL");
  plane->setup().crew = 1;
  plane->setup().strafes = true;
  plane->setup().mission = AircraftMissionCode::kPatrol;
  plane->setup().orbit_x = kOrigin;
  plane->setup().orbit_z = kOrigin;
  plane->setup().orbit_radius = 300000;
  plane->setup().sorties = 10000;
  Aircraft& a = p.sim.add(std::move(plane));
  SimContext ctx = p.sim.context();
  // Due south of the boat, nose-on, its heading accumulated two turns over.
  a.take_off(ctx, {kOrigin, 80 * kUnitsPerFoot, kOrigin - 900 * kYard}, 30000);
  a.ai().state = AircraftState::kStrafeRun;
  a.ai().release_offset = 1200;
  a.ai().opening_time = 0;
  AircraftFlight& f = a.flight();
  f.heading_acc = static_cast<std::int64_t>(7200) << 16;
  f.heading = Angle10(7200);
  f.ordered_altitude_ft = 80;
  bool fired = false;
  for (int frame = 0; frame < 8 && !fired; ++frame) {
    p.sim.step(5);
    for (const Shell* s : p.all<Shell>()) {
      fired = fired || s->owner() == &a;
    }
  }
  CHECK(fired);
}

TEST_CASE("The TDC forgets a removed tracked torpedo", "[simreview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  BoatRig p(tables);
  p.combat.torpedo_types = {{{"MARK10", true, 1}, {"MARK14", true, 2}, {"MARK18-1", false, 3}}};
  p.combat.tdc.reset();
  p.combat.tdc.set_type_index(1);
  p.combat.tubes.set_tube(0, 2);
  SimContext ctx = p.sim.context();
  TorpedoVehicle* t = p.combat.fire_tube(ctx, *p.player, 0);
  REQUIRE(t != nullptr);
  REQUIRE(p.combat.tdc.tracked_torpedoes()[0] == t);
  p.combat.forget(*t);
  CHECK(p.combat.tdc.tracked_torpedoes()[0] == nullptr);
}

}  // namespace sh
