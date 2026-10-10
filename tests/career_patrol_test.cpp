// The career patrol mission: its scenario, the boat's start, the end of
// the patrol and a headless patrol run.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "assets/patrol_zones.h"
#include "assets/world.h"
#include "game/patrol.h"
#include "game/saves_options.h"
#include "game_data.h"
#include "sim/geometry.h"
#include "sim/terrain.h"

namespace sh {
namespace {

std::vector<PatrolZone> load_zones(const Install& install) {
  return parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
}

// A career in port in June 1943 with a Balao.
Career test_career() {
  Career c;
  CareerRecord& r = c.record;
  r.name = "Tester";
  r.month = 5;
  r.day = 1;
  r.year = 13;
  r.status = CareerStatus::kInPort;
  r.boat = "(SS-337) Carbonero";
  r.class_name = "BALAO";
  r.rank = 1;
  // SD, SJa, SJ-1, PPI, bathythermograph and a bit above the six.
  r.equipment = 0x6f;
  r.home_port = "Darwin";
  r.raids_this_patrol = 2;
  return c;
}

// Two bases: an Allied one at the origin, a Japanese one far east.
ZoneData two_bases() {
  ZoneData z;
  BaseRecord allied;
  allied.name = "Allied";
  BaseRecord japanese;
  japanese.name = "Japanese";
  japanese.station = {0x2000000, 0, 0};
  z.bases = {allied, japanese};
  z.owners = {{BaseSide::kAllied, Angle10(0)}, {BaseSide::kJapanese, Angle10(0)}};
  return z;
}

TEST_CASE("the fuel report comes at each 10 % of the transit fuel", "[career]") {
  // 110 % of 1000 is 1100.
  CHECK(fuel_report_due(1100, 1200, 1000));
  CHECK_FALSE(fuel_report_due(1099, 1100, 1000));
  CHECK(fuel_report_due(1000, 1001, 1000));
  CHECK(fuel_report_due(100, 101, 1000));
  CHECK_FALSE(fuel_report_due(150, 160, 1000));
  // Empty tanks report nothing.
  CHECK_FALSE(fuel_report_due(0, 100, 1000));
  // The first frame of a patrol compares with 1000000.
  CHECK(fuel_report_due(900, 1000000, 1000));
  CHECK_FALSE(fuel_report_due(5000, 1000000, 1000));
}

TEST_CASE("the return fuel decides the end of a patrol", "[career]") {
  const ZoneData zone = two_bases();
  WatcomRandom random(7);
  // Other end codes are kept.
  ReturnFuel f = resolve_return_fuel(3, 5000, 2000, 100, 0, 0, zone, random);
  CHECK(f.end_code == 3);
  CHECK(f.fuel == 5000);
  // Enough fuel: the trip home is spent.
  f = resolve_return_fuel(5, 5000, 2000, 100, 0, 0, zone, random);
  CHECK(f.end_code == 5);
  CHECK(f.fuel == 3000);
  CHECK(f.return_hours == 100);
  // Just enough: the tank holds 1..50.
  WatcomRandom expected(7);
  random = WatcomRandom(7);
  f = resolve_return_fuel(5, 2000, 2000, 100, 0, 0, zone, random);
  CHECK(f.fuel == expected.random_int(50) + 1);
  // Short of it: a slower return, (200 - 166 * 200 / 666) % longer.
  f = resolve_return_fuel(5, 1500, 2000, 100, 0, 0, zone, random);
  CHECK(f.end_code == 5);
  CHECK(f.return_hours == 100 + 151);
  CHECK(f.fuel >= 1);
  CHECK(f.fuel <= 50);
  // Too short: towed near an Allied base, interned near a Japanese one.
  f = resolve_return_fuel(5, 1000, 2000, 100, 0, 0, zone, random);
  CHECK(f.end_code == patrol_end::kTowed);
  CHECK(f.fuel == 0);
  f = resolve_return_fuel(5, 1000, 2000, 100, 0x2000000, 0, zone, random);
  CHECK(f.end_code == patrol_end::kInterned);
  f = resolve_return_fuel(5, 50, 2000, 100, 0, 0, zone, random);
  CHECK(f.end_code == patrol_end::kTowed);
  // An aborted patrol: recalled or interned.
  f = resolve_return_fuel(patrol_end::kAbortPatrol, 5000, 2000, 100, 0, 0, zone, random);
  CHECK(f.end_code == patrol_end::kRecalled);
  CHECK(f.fuel == 5000);
  f = resolve_return_fuel(patrol_end::kAbortPatrol, 5000, 2000, 100, 0x2000000, 0, zone, random);
  CHECK(f.end_code == patrol_end::kInterned);
}

TEST_CASE("the patrol scenario comes from the career", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = load_zones(install);
  Career career = test_career();
  WatcomRandom random(1234);
  WatcomRandom draws(1234);
  const PatrolPlan plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
  const ScenarioHeader& h = plan.scenario.header;
  CHECK(h.world == "WORLD-10.WLD");
  CHECK(h.month == 5);
  CHECK(h.day == 1);
  CHECK(h.year == 13);
  CHECK(h.hour == draws.random_int(24));
  CHECK(h.minute == draws.random_int(60));
  // The SJ-1 replaces the SJa; bits above 5 are dropped.
  CHECK(h.equipment == 0x2d);
  REQUIRE(plan.scenario.player_sub);
  CHECK(plan.scenario.player_sub->sub_class == "BALAO");
  CHECK(plan.scenario.player_sub->name == "(SS-337) Carbonero");
  CHECK(plan.scenario.player_sub->crew == CrewQuality::kVeteran);
  CHECK(plan.scenario.ships.empty());
  CHECK(h.tube_torpedoes[0] != -1);
  // The outbound transit: 20000 give or take 500.
  const auto capacity = static_cast<std::int32_t>(tables.find_sub("BALAO")->fuel_capacity);
  CHECK(plan.start_fuel <= capacity - 20000 + 499);
  CHECK(plan.start_fuel >= capacity - 20000 - 500);
  CHECK(plan.transit_fuel == 20000);
  CHECK(plan.return_hours == 100);
  CHECK(plan.home_port == "Darwin");
  CHECK(career.record.location == "Malaysia");
  CHECK(career.record.status == CareerStatus::kOnPatrol);
  CHECK(career.record.raids_this_patrol == 0);
}

TEST_CASE("a patrol starts in the home port's harbour when the map has it", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<PatrolZone> zones = load_zones(install);
  const ZoneData zone = ZoneData::load(install, zones.at(13));  // Java Sea
  const World world = parse_world(install.read("DATA/WORLD-14.WLD"), "WORLD-14.WLD");
  const WorldSeaFloor floor(world);
  WatcomRandom random(5);
  const PlayerStart start = career_player_start(zone, "SURABAYA", zones.at(13).start_edges, world, floor, random);
  CHECK(start.x == zone.bases.at(2).harbor.x);
  CHECK(start.z == zone.bases.at(2).harbor.z);
  CHECK(start.heading == zone.owners.at(2).harbor_heading);
}

TEST_CASE("a patrol from elsewhere starts in deep water near an open edge", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<PatrolZone> zones = load_zones(install);
  const PatrolZone& malaysia = zones.at(9);  // open south and east
  const ZoneData zone = ZoneData::load(install, malaysia);
  const World world = parse_world(install.read("DATA/WORLD-10.WLD"), "WORLD-10.WLD");
  const WorldSeaFloor floor(world);
  const std::int32_t cell = world.cell_size();
  const std::int32_t width = world.grid.width * cell;
  const std::int32_t height = world.grid.height * cell;
  for (std::uint32_t seed = 1; seed < 20; ++seed) {
    WatcomRandom random(seed);
    const PlayerStart start = career_player_start(zone, "Darwin", malaysia.start_edges, world, floor, random);
    CHECK(floor.height_at(start.x, start.z) <= -0x500);
    // Near the south (low z) or the east edge.
    const bool south = start.z < height / 3;
    const bool east = start.x > width * 2 / 3;
    CHECK((south || east));
    CHECK(start.heading ==
          fx::heading_from_delta(world.grid.width / 2 * cell - start.x, world.grid.height / 2 * cell - start.z));
  }
}

TEST_CASE("a career patrol runs headless", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = load_zones(install);
  Career career = test_career();
  WatcomRandom random(99);
  PatrolPlan plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
  Patrol patrol(install, tables, career, std::move(plan));
  MissionSetup setup;
  setup.options = option::kModeCareer | RealismFile::kDefaultBits;
  setup.random = random;
  std::unique_ptr<Mission> mission = patrol.create_mission(std::move(setup));
  REQUIRE(patrol.groups() != nullptr);
  CHECK(mission->world().region_name == "Malaysia");
  const fx::Vec3 start = mission->player().position();
  CHECK(mission->floor().height_at(start.x, start.z) <= -0x500);
  CHECK(mission->sim().conditions().weather == patrol.weather().state().weather);
  CHECK(mission->sim().conditions().thermal_layer_ft == patrol.weather().state().thermal_layer_ft);
  CHECK(mission->player().fuel() == patrol.plan().start_fuel);
  // The world files land in the career.
  CHECK(std::string(reinterpret_cast<const char*>(career.record.world_files.data() + 3 * 0x90)) == "WORLD-10.WLD");

  for (int i = 0; i < 400 && mission->end_code() == 0; ++i) {
    mission->simulate_frame(10, i == 0);
  }
  CHECK(mission->end_code() == 0);
  CHECK(career.record.on_patrol == -1);
  CHECK(career.record.day == mission->sim().clock().day());
  CHECK(career.record.hour == mission->sim().clock().hour());
  CHECK(career.record.minute == mission->sim().clock().minute());

  mission->set_end_code(patrol_end::kReturn);
  mission->finish();
  const PatrolEnd end = patrol.finish(*mission);
  CHECK(end.end_code == mission->end_code());
  CHECK(patrol.groups() == nullptr);
  // A full tank less the transit out: enough to get home.
  CHECK(end.end_code == patrol_end::kReturn);
  CHECK(end.fuel == mission->player().fuel() - 20000);
}

}  // namespace
}  // namespace sh
