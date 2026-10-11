// The single-mission scenario generator: zone, header, unit lists and the
// torpedo load for fixed seeds and parameters.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <utility>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/world.h"
#include "core/error.h"
#include "core/random.h"
#include "game/mission.h"
#include "game/scengen.h"
#include "game_data.h"
#include "sim/terrain.h"

namespace sh {

namespace {

GeneratorParams params_for(std::int32_t month, std::int32_t year) {
  GeneratorParams p;
  p.day = 15;
  p.month = month;
  p.year = year;
  return p;
}

}  // namespace

TEST_CASE("Equipment fit dates", "[scengen]") {
  SH_REQUIRE_GAME_EXE();
  using E = Equipment;
  const auto bit = [](E e) { return static_cast<std::uint32_t>(e); };
  CHECK(equipment_on_date(11, 11) == 0);
  CHECK(equipment_on_date(0, 12) == bit(E::kSdAirRadar));
  CHECK(equipment_on_date(7, 12) == (bit(E::kSdAirRadar) | bit(E::kSjaRadar)));
  // The SJ-1 replaces the SJa in August 1943, after the bathythermograph
  // (March) and the PPI (July).
  CHECK(equipment_on_date(6, 13) ==
        (bit(E::kSdAirRadar) | bit(E::kSjaRadar) | bit(E::kBathythermograph) | bit(E::kPpiDisplay)));
  CHECK(equipment_on_date(7, 13) ==
        (bit(E::kSdAirRadar) | bit(E::kSj1Radar) | bit(E::kBathythermograph) | bit(E::kPpiDisplay)));
  CHECK(equipment_on_date(4, 14) == 0x3d);
}

TEST_CASE("Campaign day numbers", "[scengen]") {
  CHECK(campaign_day_number(7, 11, 11) == 0);
  CHECK(campaign_day_number(1, 0, 12) == 25);
  // 1944 is a leap year.
  CHECK(campaign_day_number(1, 2, 14) - campaign_day_number(1, 1, 14) == 29);
  CHECK(campaign_day_number(1, 0, -1) == 100000);
  CHECK_THROWS_AS(campaign_day_number(1, 0, 11), DataError);
}

namespace {

struct Fixture {
  const Install& install;
  GameTables tables;
};

// Generates a mission of `type` with the first boat class in service.
GeneratedMission generate(const Fixture& f, MissionType type, GeneratorParams p, std::uint32_t seed) {
  const std::vector<const SubClass*> subs = sub_classes_in_service(f.tables, p.month, p.year);
  REQUIRE_FALSE(subs.empty());
  p.sub_class = subs.back()->index_name;
  WatcomRandom random(seed);
  return generate_single_mission(f.install, f.tables, type, p, 0, random);
}

bool in_service(const ShipClass& c, std::int32_t month, std::int32_t year) {
  const auto y = static_cast<std::uint32_t>(year + kScenarioYearBase);
  const auto m = static_cast<std::uint32_t>(month + 1);
  return c.start_year <= y && (y != c.start_year || c.start_month <= m) && y <= c.end_year &&
         (y != c.end_year || m <= c.end_month);
}

// Checks what every generated scenario must satisfy.
void check_scenario(const Fixture& f, const GeneratedMission& g, const GeneratorParams& p) {
  const Scenario& s = g.scenario;
  const ScenarioHeader& h = s.header;
  CHECK(h.world == g.zone.zone.world);
  CHECK(h.hour == -1);
  CHECK(h.minute == p.time);
  CHECK(h.day == p.day);
  CHECK(h.month == p.month);
  CHECK(h.year == p.year);
  CHECK(static_cast<std::int32_t>(h.weather) == p.weather);
  CHECK(static_cast<std::int32_t>(h.sea_state) == p.sea);
  CHECK(h.fog == p.fog);
  CHECK(h.gradient >= 0);
  CHECK(h.gradient < 5);
  CHECK(h.equipment == equipment_on_date(p.month, p.year));
  REQUIRE(s.player_sub.has_value());
  CHECK(s.player_sub->crew == static_cast<CrewQuality>(p.crew));
  CHECK(s.player_sub->name.starts_with("(SS-"));

  const std::string world_path = "DATA/" + h.world;
  const World world = parse_world(f.install.read(world_path), world_path);
  const std::int32_t map_w = world.grid.width * world.cell_size();
  const std::int32_t map_h = world.grid.height * world.cell_size();
  const auto inside = [&](std::int32_t x, std::int32_t z) { return x > 0 && x < map_w && z > 0 && z < map_h; };
  CHECK(inside(s.player_sub->x, s.player_sub->y));

  // The torpedoes fill the boat's tubes and racks.
  const SubClass& sub = *f.tables.find_sub(s.player_sub->sub_class);
  const std::uint32_t tubes = sub.bow_tubes + sub.stern_tubes;
  for (std::size_t i = 0; i < h.tube_torpedoes.size(); ++i) {
    CHECK((h.tube_torpedoes[i] > 0) == (i < tubes));
  }
  std::int32_t fwd = 0;
  std::int32_t aft = 0;
  for (const TorpedoReload& r : h.reloads) {
    if (r.type == -1) {
      break;
    }
    fwd += r.forward;
    aft += r.aft;
  }
  CHECK(fwd == static_cast<std::int32_t>(sub.bow_reloads));
  CHECK(aft == static_cast<std::int32_t>(sub.stern_reloads));

  std::map<std::int32_t, std::int32_t> leaders;  // convoy group -> leaders
  for (const ShipRecord& ship : s.ships) {
    const ShipClass* c = f.tables.find_ship(ship.ship_class);
    REQUIRE(c != nullptr);
    CHECK(inside(ship.x, ship.y));
    switch (ship.orders) {
      case ShipOrders::kEscort:
      case ShipOrders::kPatrol:
        // A screening box (4, a lone escort 5), a patrol leg (3) or a
        // guard's beat (4), ended by an x of -1.
        CHECK(c->role == ShipRole::kEscort);
        CHECK(in_service(*c, p.month, p.year));
        CHECK(ship.waypoints.size() >= 3);
        CHECK(ship.waypoints.size() <= 5);
        CHECK(ship.waypoints.x[ship.waypoints.size()] == -1);
        if (ship.orders == ShipOrders::kPatrol) {
          CHECK(ship.alert == -1);
        }
        break;
      case ShipOrders::kConvoyLeader:
        ++leaders[ship.convoy_group];
        CHECK(ship.speed > 0);
        CHECK(ship.waypoints.size() >= 2);
        [[fallthrough]];
      case ShipOrders::kConvoyMember:
        CHECK((c->role == ShipRole::kMerchant || c->role == ShipRole::kWarship));
        CHECK(ship.convoy_group == 1);
        break;
      case ShipOrders::kNone:
        CHECK(ship.speed == 0);
        break;
      default:
        FAIL("unexpected ship orders");
    }
  }
  for (const auto& [group, n] : leaders) {
    CHECK(n == 1);
  }
  for (const EnemySubRecord& sub_rec : s.enemy_subs) {
    const ShipClass* c = f.tables.find_ship(sub_rec.sub_class);
    REQUIRE(c != nullptr);
    CHECK(c->role == ShipRole::kSubmarine);
    CHECK(in_service(*c, p.month, p.year));
    CHECK(sub_rec.side == 1);
    CHECK(inside(sub_rec.x, sub_rec.y));
  }
  for (const AircraftRecord& a : s.aircraft) {
    CHECK(f.tables.find_aircraft(a.aircraft_type) != nullptr);
    CHECK(a.orbit_radius >= 0x7bc00);
    CHECK(a.orbit_radius < 0x7bc00 + 0x135600);
  }
}

}  // namespace

TEST_CASE("An oversized patrol (size 3) is capped at 6 ships instead of hanging", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  GeneratorParams params;
  params.patrol_size = 3;
  params.year = 13;
  params.month = 5;
  Scenario scenario;
  WatcomRandom random(5);
  ScenarioGenerator gen(tables, params, scenario, random, 0);
  gen.build_patrol({100000, 100000}, Angle10(0), false, false);
  CHECK(scenario.ships.size() == 6);
}

TEST_CASE("Torpedo load by boat and date", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::int32_t mk10 = static_cast<std::int32_t>(tables.find_torpedo("MARK10")->id);
  const std::int32_t mk14 = static_cast<std::int32_t>(tables.find_torpedo("MARK14")->id);
  const std::int32_t mk18 = static_cast<std::int32_t>(tables.find_torpedo("MARK18-1")->id);
  const SubClass& gato = *tables.find_sub("GATO");

  SECTION("Mark 14 before the Mark 18, no draw for the type") {
    ScenarioHeader h;
    h.year = 12;
    h.month = 5;
    WatcomRandom random(7);
    fill_torpedo_load(h, gato, tables, random);
    for (std::size_t i = 0; i < h.tube_torpedoes.size(); ++i) {
      CHECK(h.tube_torpedoes[i] == (i < gato.bow_tubes + gato.stern_tubes ? mk14 : -1));
    }
    CHECK(h.reloads[0].type == mk14);
    CHECK(h.reloads[0].forward == static_cast<std::int32_t>(gato.bow_reloads));
    CHECK(h.reloads[0].aft == static_cast<std::int32_t>(gato.stern_reloads));
    CHECK(h.reloads[1].type == -1);
    // One random_int(1) per tube.
    WatcomRandom expected(7);
    for (std::uint32_t i = 0; i < gato.bow_tubes + gato.stern_tubes; ++i) {
      expected.rand();
    }
    CHECK(random.rand() == expected.rand());
  }

  SECTION("Old boats carry the Mark 10") {
    const auto old = std::find_if(tables.subs.begin(), tables.subs.end(),
                                  [](const SubClass& s) { return !s.modern_torpedoes; });
    REQUIRE(old != tables.subs.end());
    ScenarioHeader h;
    h.year = 15;
    WatcomRandom random(1);
    fill_torpedo_load(h, *old, tables, random);
    CHECK(h.tube_torpedoes[0] == mk10);
    CHECK(h.reloads[0].type == mk10);
  }

  SECTION("1945: the roll replaces or mixes in the Mark 18-1") {
    std::set<std::int32_t> outcomes;
    for (std::uint32_t seed = 1; seed < 60; ++seed) {
      ScenarioHeader h;
      h.year = 15;
      h.month = 2;
      WatcomRandom random(seed);
      WatcomRandom roll(seed);
      const std::int32_t r = roll.random_int(100);
      fill_torpedo_load(h, gato, tables, random);
      if (r < 60) {
        CHECK(h.reloads[0].type == mk18);
        CHECK(h.reloads[1].type == -1);
        outcomes.insert(0);
      } else if (r < 90) {
        // Mixed: two reload entries sharing the racks, the tubes drawn
        // between both types.
        CHECK(h.reloads[0].type == mk14);
        CHECK(h.reloads[1].type == mk18);
        CHECK(h.reloads[0].forward + h.reloads[1].forward == static_cast<std::int32_t>(gato.bow_reloads));
        CHECK(h.reloads[0].aft + h.reloads[1].aft == static_cast<std::int32_t>(gato.stern_reloads));
        CHECK(h.reloads[2].type == -1);
        for (std::uint32_t i = 0; i < gato.bow_tubes + gato.stern_tubes; ++i) {
          CHECK((h.tube_torpedoes[i] == mk14 || h.tube_torpedoes[i] == mk18));
        }
        outcomes.insert(1);
      } else {
        CHECK(h.reloads[0].type == mk14);
        outcomes.insert(2);
      }
    }
    CHECK(outcomes.size() == 3);
  }
}

TEST_CASE("Submarine classes in service", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const auto names = [&](std::int32_t month, std::int32_t year) {
    std::set<std::string> out;
    for (const SubClass* s : sub_classes_in_service(tables, month, year)) {
      out.insert(s->index_name);
    }
    return out;
  };
  // SUBS.DAT +0x48..+0x54: the Gato from April 1942, the Tench from
  // February 1945.
  CHECK(names(11, 11) == std::set<std::string>{"S CLASS", "BARRACUDA", "NARWHAL", "P CLASS", "SALMON", "T CLASS"});
  CHECK_FALSE(names(2, 12).contains("GATO"));
  CHECK(names(3, 12).contains("GATO"));
  CHECK(names(1, 15).contains("TENCH"));
}

TEST_CASE("Convoy encounter for a fixed seed", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const Fixture f{install, GameTables::load(install)};
  GeneratorParams p = params_for(5, 13);
  p.convoy_size = 1;
  p.escort = 2;
  p.enemy = 1;
  p.air_cover = 1;
  p.crew = 2;
  p.time = 3;
  const GeneratedMission g = generate(f, MissionType::kConvoy, p, 12345);
  check_scenario(f, g, p);
  const Scenario& s = g.scenario;
  // Hand-traced: the first draw is the thermal gradient, the second picks
  // the boat among the class's boats in service (CARSUB.DAT order).
  WatcomRandom trace(12345);
  CHECK(s.header.gradient == trace.random_int(5));
  const SubClass& sub = *f.tables.find_sub(s.player_sub->sub_class);
  std::vector<const CarsubBoat*> boats;
  for (const CarsubBoat& b : f.tables.carsub) {
    const std::uint32_t y = 43;
    const std::uint32_t m = 6;
    if (b.class_index == sub.class_index && b.avail_year <= y && y <= b.end_year &&
        (y != b.avail_year || b.avail_month <= m) && (y != b.end_year || m <= b.end_month)) {
      boats.push_back(&b);
    }
  }
  REQUIRE_FALSE(boats.empty());
  const CarsubBoat& boat = *boats[static_cast<std::size_t>(trace.random_int(static_cast<int>(boats.size())))];
  CHECK(s.player_sub->name == "(SS-" + std::to_string(boat.hull_number) + ") " + boat.name);
  // The boat starts at the route intercept, at sea.
  const World world = parse_world(install.read("DATA/" + s.header.world), s.header.world);
  const WorldSeaFloor floor(world);
  CHECK(floor.height_at(s.player_sub->x, s.player_sub->y) < 0);
  // 5-10 merchants and 4-6 escorts; the leader's speed is the slowest
  // merchant's.
  std::int32_t merchants = 0;
  std::int32_t escorts = 0;
  std::int32_t slowest = 1000;
  std::int32_t leader_speed = 0;
  for (const ShipRecord& ship : s.ships) {
    if (ship.orders == ShipOrders::kEscort) {
      ++escorts;
      CHECK(ship.crew == CrewQuality::kVeteran);
    } else {
      ++merchants;
      slowest = std::min(slowest, std::atoi(f.tables.find_ship(ship.ship_class)->top_speed_kts.c_str()));
      if (ship.orders == ShipOrders::kConvoyLeader) {
        leader_speed = ship.speed;
        CHECK(ship.zigzag == 1);
      }
    }
  }
  CHECK(merchants >= 5);
  CHECK(merchants <= 10);
  CHECK(escorts >= 4);
  CHECK(escorts <= 6);
  CHECK(leader_speed == slowest);
  // The ships come first, in their array order: merchants then escorts.
  CHECK(s.ships.front().orders != ShipOrders::kEscort);
  CHECK(s.ships.back().orders == ShipOrders::kEscort);
  // The boat starts at the intercept; the start move goes ahead of the
  // convoy (route direction + 330..390 deg).
  CHECK(g.start_bearing >= 0);
  CHECK(g.start_bearing < 3600);
  CHECK(s.aircraft.size() == 2);
  for (const AircraftRecord& a : s.aircraft) {
    CHECK(a.mission == AircraftMission::kSearchOrbit);
    CHECK(a.escort_group == 1);
    CHECK(a.relaunch == -1);
  }
  // The same seed gives the same scenario.
  const GeneratedMission again = generate(f, MissionType::kConvoy, p, 12345);
  REQUIRE(again.scenario.ships.size() == s.ships.size());
  for (std::size_t i = 0; i < s.ships.size(); ++i) {
    CHECK(again.scenario.ships[i].x == s.ships[i].x);
    CHECK(again.scenario.ships[i].ship_class == s.ships[i].ship_class);
  }
}

TEST_CASE("Warship encounter: real IJN ships, never repeated", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const Fixture f{install, GameTables::load(install)};
  for (std::int32_t type = 0; type < 4; ++type) {
    for (std::uint32_t seed = 1; seed <= 5; ++seed) {
      GeneratorParams p = params_for(8, 12);
      p.warship_type = type;
      p.escort = 1;
      const GeneratedMission g = generate(f, MissionType::kWarship, p, seed);
      check_scenario(f, g, p);
      std::set<std::string> names;
      std::int32_t warships = 0;
      for (const ShipRecord& ship : g.scenario.ships) {
        if (ship.orders == ShipOrders::kEscort) {
          continue;
        }
        ++warships;
        CHECK(f.tables.find_ship(ship.ship_class)->role == ShipRole::kWarship);
        CHECK_FALSE(ship.name.empty());
        CHECK(names.insert(ship.name).second);
        if (ship.orders == ShipOrders::kConvoyLeader) {
          CHECK(ship.speed <= 27);
        }
      }
      CHECK(warships >= 1);
      CHECK(warships <= 4);
    }
  }
}

TEST_CASE("Patrol encounter: the boat inside the patrol box", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const Fixture f{install, GameTables::load(install)};
  GeneratorParams p = params_for(2, 14);
  p.patrol_size = 2;
  const GeneratedMission g = generate(f, MissionType::kPatrol, p, 99);
  check_scenario(f, g, p);
  CHECK(g.start_bearing == -1);
  CHECK(g.scenario.ships.size() >= 5);
  CHECK(g.scenario.ships.size() <= 6);
  for (const ShipRecord& ship : g.scenario.ships) {
    CHECK(ship.orders == ShipOrders::kPatrol);
    CHECK(ship.speed == 12);
    CHECK(ship.alert == -1);
    CHECK(ship.waypoints.count == 3);
  }
  const SubClass& sub = *f.tables.find_sub(g.scenario.player_sub->sub_class);
  CHECK(g.scenario.player_sub->depth == static_cast<std::int32_t>(sub.periscope_depth_ft));
}

TEST_CASE("Every encounter option generates and loads", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const Fixture f{install, GameTables::load(install)};
  std::uint32_t seed = 1;
  const std::vector<std::pair<std::int32_t, std::int32_t>> dates = {{11, 11}, {3, 12}, {9, 13}, {5, 14}, {6, 15}};
  for (const auto& [month, year] : dates) {
    for (std::int32_t level = 0; level < 3; ++level) {
      for (std::int32_t escort = 0; escort < 4; ++escort) {
        GeneratorParams p = params_for(month, year);
        p.convoy_size = level;
        p.patrol_size = level;
        p.enemy = level;
        p.crew = level;
        p.escort = escort;
        p.warship_type = escort;
        p.air_cover = escort & 1;
        p.fog = level & 1;
        p.time = escort;
        p.weather = escort;
        p.sea = level;
        for (MissionType type : {MissionType::kConvoy, MissionType::kWarship, MissionType::kPatrol,
                                 MissionType::kEnemySub, MissionType::kSpecial}) {
          for (std::int32_t special = 0; special < (type == MissionType::kSpecial ? 2 : 1); ++special) {
            p.special_mission = special;
            INFO("type " << static_cast<int>(type) << " date " << month << "/" << year << " level " << level
                         << " escort " << escort << " special " << special << " seed " << seed);
            const GeneratedMission g = generate(f, type, p, seed++);
            check_scenario(f, g, p);
            if (type == MissionType::kSpecial) {
              CHECK(g.scenario.header.special_mission == -1);
              CHECK(g.scenario.bomber_groups.size() == (special == 0 ? 1u : 0u));
              CHECK(g.scenario.photo_recons.size() == (special == 1 ? 1u : 0u));
            }
            if (type == MissionType::kEnemySub) {
              CHECK(g.scenario.enemy_subs.size() >= 1);
              CHECK(g.scenario.enemy_subs.size() <= 3);
            }
          }
        }
      }
    }
  }
}

TEST_CASE("A generated scenario builds a mission", "[scengen]") {
  SH_REQUIRE_GAME_DATA(install);
  const Fixture f{install, GameTables::load(install)};
  GeneratorParams p = params_for(4, 13);
  p.escort = 1;
  p.air_cover = 1;
  for (MissionType type : {MissionType::kConvoy, MissionType::kWarship, MissionType::kPatrol, MissionType::kEnemySub,
                           MissionType::kSpecial}) {
    GeneratedMission g = generate(f, type, p, 4242);
    Scenario s = std::move(g.scenario);
    if (s.header.hour < 0) {
      // The mission does not resolve the time-of-day start yet.
      s.header.hour = 12;
      s.header.minute = 0;
    }
    const std::size_t ships = s.ships.size();
    MissionSetup setup;
    setup.bases = g.zone.bases;
    setup.air_edges = g.zone.zone.air_start_edges;
    const std::unique_ptr<Mission> mission = Mission::create(install, f.tables, std::move(s), setup);
    CHECK(mission->ships().size() + static_cast<std::size_t>(mission->skipped_ships()) == ships);
  }
}

}  // namespace sh
