// The career patrol mission: its scenario, the boat's start, the end of
// the patrol and a headless patrol run.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>

#include "assets/patrol_zones.h"
#include "assets/world.h"
#include "game/mission.h"
#include "game/patrol.h"
#include "game/saves_options.h"
#include "game/torpedo_load.h"
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

TEST_CASE("With the rules' corrections a boat low on fuel is interned only nearly empty", "[career][rules]") {
  const ZoneData zone = two_bases();
  const MissionRules fixed = MissionRules::extended();
  WatcomRandom random(7);
  // Near the Japanese base, the reserve 2000 - 666 = 1334: 1000 is 74 % of
  // it, 260 19 %.
  for (const std::int32_t fuel : {1000, 270, 260, 150}) {
    const ReturnFuel original = resolve_return_fuel(5, fuel, 2000, 100, 0x2000000, 0, zone, random);
    const ReturnFuel corrected = resolve_return_fuel(5, fuel, 2000, 100, 0x2000000, 0, zone, random, fixed);
    CHECK(original.end_code == patrol_end::kInterned);
    CHECK(corrected.end_code == (fuel * 100 / 1334 <= 20 ? patrol_end::kInterned : patrol_end::kTowed));
    CHECK(corrected.fuel == 0);
  }
  // Near the Allied base: towed for both.
  CHECK(resolve_return_fuel(5, 260, 2000, 100, 0, 0, zone, random, fixed).end_code == patrol_end::kTowed);
}

TEST_CASE("the patrol scenario comes from the career", "[career]") {
  SH_REQUIRE_INSTALL(install);
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

TEST_CASE("a patrol starts in the home port's harbour when the map has it", "[career][content]") {
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

TEST_CASE("a patrol from elsewhere starts in deep water near an open edge", "[career][content]") {
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

TEST_CASE("With the rules' corrections a patrol starts only from the zone's open edges", "[career][rules][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<PatrolZone> zones = load_zones(install);
  const World world = parse_world(install.read("DATA/WORLD-10.WLD"), "WORLD-10.WLD");
  const WorldSeaFloor floor(world);
  const std::int32_t cell = world.cell_size();
  const std::int32_t width = world.grid.width * cell;
  const std::int32_t height = world.grid.height * cell;
  // The zones of WORLD-10 with some edge closed: a start must be near an
  // open one with the corrections (the original may use the other row
  // edge, which these zones and seeds do not show).
  for (std::size_t z = 0; z < zones.size(); ++z) {
    const PatrolZone& pz = zones[z];
    if (pz.world != "WORLD-10.WLD" || std::ranges::all_of(pz.start_edges, [](std::int32_t e) { return e != 0; })) {
      continue;
    }
    const ZoneData zone = ZoneData::load(install, pz);
    for (std::uint32_t seed = 1; seed < 60; ++seed) {
      {
        WatcomRandom random(seed);
        const MissionRules rules = MissionRules::extended();
        const PlayerStart start = career_player_start(zone, "Nowhere", pz.start_edges, world, floor, random, rules);
        // The edges: 0 south (the last row, low z), 1 north, 2 west, 3 east.
        const std::array<bool, 4> near{start.z < height / 3, start.z > height * 2 / 3, start.x < width / 3,
                                       start.x > width * 2 / 3};
        bool open = false;
        for (std::size_t e = 0; e < 4; ++e) {
          open = open || (pz.start_edges[e] != 0 && near[e]);
        }
        CHECK(open);
      }
    }
  }
}

TEST_CASE("a career patrol runs headless", "[career]") {
  SH_REQUIRE_INSTALL(install);
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
TEST_CASE("A career with the extended rules mixes the Mark 18s on patrol", "[career][rules][torpedo_types]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const std::vector<PatrolZone> zones = load_zones(install);
  // September 1944, in the Mark 18-2's changeover months.
  const auto career_at_1944 = [](std::optional<MissionRules> rules) {
    Career c = test_career();
    c.record.month = 8;
    c.record.year = 14;
    c.rules = rules;
    return c;
  };
  std::set<std::uint32_t> seen_extended;
  std::set<std::uint32_t> seen_original;
  std::uint32_t all_18_1 = 0;
  for (std::uint32_t seed = 1; seed <= 40; ++seed) {
    for (const bool extended : {false, true}) {
      Career career = career_at_1944(extended ? std::optional(MissionRules::extended()) : std::nullopt);
      WatcomRandom random(seed);
      const PatrolPlan plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
      TorpedoLoad load = scenario_torpedo_load(plan.scenario.header);
      const SubClass& balao = *tables.find_sub("BALAO");
      tdc_upgrade_mark18_by_date(load, balao, tables, 14, 8, career.rules.value_or(MissionRules{}));
      const std::vector<std::uint32_t> carried = carried_torpedo_types(load, balao);
      CHECK(carried.size() <= kMaxTorpedoTypes);
      (extended ? seen_extended : seen_original).insert(carried.begin(), carried.end());
      if (extended && all_18_1 == 0 && plan.scenario.header.tube_torpedoes[0] == 3 &&
          plan.scenario.header.reloads[1].type != 2) {
        all_18_1 = seed;
      }
    }
  }
  // The original's rules: the 18-1 all became 18-2, no Mark 27.
  CHECK(seen_original.count(3) == 0);
  CHECK(seen_original.count(kMark27Id) == 0);
  // The career's extended rules: 18-1 and 18-2 together, and the Mark 27.
  CHECK(seen_extended.count(3) == 1);
  CHECK(seen_extended.count(4) == 1);
  CHECK(seen_extended.count(kMark27Id) == 1);

  // The patrol's mission takes the career's rules (as the career flow's
  // request does) and carries both Mark 18s.
  REQUIRE(all_18_1 != 0);
  Career career = career_at_1944(MissionRules::extended());
  WatcomRandom random(all_18_1);
  PatrolPlan plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
  Patrol patrol(install, tables, career, std::move(plan));
  MissionSetup setup;
  setup.options = option::kModeCareer | RealismFile::kDefaultBits;
  setup.random = random;
  setup.rules = *career.rules;
  const std::unique_ptr<Mission> mission = patrol.create_mission(std::move(setup));
  CHECK(mission->sim().conditions().rules.mixed_mark18);
  const std::vector<std::uint32_t>& start = mission->start_torpedo_types();
  CHECK(std::ranges::find(start, 3u) != start.end());
  CHECK(std::ranges::find(start, 4u) != start.end());
}

}  // namespace sh
