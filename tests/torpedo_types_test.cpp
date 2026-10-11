// Tests of the extended variant's torpedo types (not in the original): the
// mixed Mark 18 loads (`MissionRules::mixed_mark18`) and the Mark 27
// (`MissionRules::mark27`), each against the original's behaviour with the
// rules off.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "assets/crew_messages.h"
#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "core/random.h"
#include "game/class_params.h"
#include "game/mission.h"
#include "game/mission_save.h"
#include "game/saves_options.h"
#include "game/scengen.h"
#include "game/torpedo_load.h"
#include "game_data.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/tdc.h"
#include "sim/torpedo.h"
#include "torpedo_rig.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kOrigin = 2000000;
constexpr std::int32_t kYard = kUnitsPerYard;

// The tables with the Mark 27.
GameTables extended_tables(const Install& install) {
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  return tables;
}

MissionRules mixed_rules() {
  MissionRules r;
  r.mixed_mark18 = true;
  return r;
}

MissionRules all_rules() {
  MissionRules r;
  r.mixed_mark18 = true;
  r.mark27 = true;
  return r;
}

// A load of `tube` in every tube and `rack` in every rack slot.
TorpedoLoad uniform_load(std::int32_t tube, std::int32_t rack) {
  TorpedoLoad load;
  load.tubes.fill(tube);
  load.bow_racks.fill(rack);
  load.stern_racks.fill(rack);
  return load;
}

std::ptrdiff_t count_of(const TorpedoLoad& load, const SubClass& sub, std::int32_t id) {
  return std::count(load.tubes.begin(), load.tubes.begin() + sub.bow_tubes + sub.stern_tubes, id) +
         std::count(load.bow_racks.begin(), load.bow_racks.begin() + sub.bow_reloads, id) +
         std::count(load.stern_racks.begin(), load.stern_racks.begin() + sub.stern_reloads, id);
}

// The player's Gato surfaced and stopped at the origin heading north with
// its combat state, at `year` (since 1930) and `month`; its torpedo table
// is `types`.
struct Engagement {
  Engagement(const GameTables& t, std::int32_t year, std::int32_t month, const std::array<TorpedoChoice, 3>& types)
      : tables(t), gato(*t.find_sub("GATO")), sim(MissionConditions{}, kDeepSea, 1), combat(t, gato) {
    sim.clock().init(year, month, 1, 12, 0);
    combat.torpedo_types = types;
    combat.tdc.reset();
    sim.set_player_combat(&combat);
    player =
        &sim.add(std::make_unique<PlayerSub>(sub_class_params(gato), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 0));
  }

  // Adds a ship of `name` at (dx, dz) yards from the boat.
  Ship& add_ship(const char* name, std::int32_t dx, std::int32_t dz, Angle10 heading, std::int32_t knots) {
    const ShipClass& cls = *tables.find_ship(name);
    auto ship = std::make_unique<Ship>(ship_motion_params(cls), fx::Vec3{kOrigin + dx * kYard, 0, kOrigin + dz * kYard},
                                       heading, Fixed16::from_int(knots), ShipOrder::kFollowPath);
    ship->setup().ship_class = &cls;
    ship->setup().hit_points = static_cast<std::int32_t>(cls.hull_strength);
    return sim.add(std::move(ship));
  }

  // Locks `ship` and lets the TDC solve it (novice realism: exact).
  void lock(Ship& ship) {
    combat.tdc.set_target(&ship);
    combat.tdc.set_sighting_range(20000);
    frames(10);
  }

  void frames(int n) {
    for (int i = 0; i < n; ++i) {
      sim.step(10);
      SimContext ctx = sim.context();
      combat.update_frame(ctx, *player);
    }
  }

  // Runs whole seconds of game time.
  void run(std::int32_t seconds) {
    for (std::int32_t s = 0; s < seconds; ++s) {
      for (int i = 0; i < 10; ++i) {
        sim.step(10);
      }
    }
  }

  TorpedoVehicle* fire(std::size_t tube) {
    SimContext ctx = sim.context();
    return combat.fire_tube(ctx, *player, tube);
  }

  const GameTables& tables;
  const SubClass& gato;
  Simulation sim;
  PlayerCombat combat;
  PlayerSub* player = nullptr;
};

// The original's table of 1944 after the 18-2's date, and the rules'
// tables of a mixed load, without and with the Mark 27.
std::array<TorpedoChoice, 3> original_table() {
  return {{{"MARK10", true, 1}, {"MARK14", true, 2}, {"MARK18-2", true, 4}}};
}
std::array<TorpedoChoice, 3> mixed_table() {
  return {{{"MARK14", true, 2}, {"MARK18-1", true, 3}, {"MARK18-2", true, 4}}};
}
std::array<TorpedoChoice, 3> mark27_table() {
  return {{{"MARK14", true, 2}, {"MARK18-2", true, 4}, {"MARK27", true, kMark27Id}}};
}

std::vector<std::string_view> names(const TorpedoTypes& t) {
  std::vector<std::string_view> out;
  for (const TorpedoTypeChoice& c : t.types) {
    out.push_back(c.name);
  }
  return out;
}

// Returns the lead (tenths, right positive) of a torpedo of `speed` knots
// on a target crossing at `target` knots square to the line of sight.
std::int32_t lead_tenths(double target, double speed) {
  return static_cast<std::int32_t>(std::lround(std::asin(target / speed) * 1800 / 3.14159265358979));
}

std::int32_t signed_tenths(Angle10 a) { return a.tenths() > 1800 ? a.tenths() - 3600 : a.tenths(); }

}  // namespace

// ------------------------------------------------------------ the loads

TEST_CASE("The original swaps every Mark 18-1 for the 18-2 at its date", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const SubClass& gato = *tables.find_sub("GATO");
  // August 1944 (the 18-2's month, 1-based 8).
  TorpedoLoad load = uniform_load(3, 3);
  const TorpedoTypes table = tdc_upgrade_mark18_by_date(load, gato, tables, 14, 7);
  CHECK(count_of(load, gato, 3) == 0);
  CHECK(count_of(load, gato, 4) == gato.bow_tubes + gato.stern_tubes + gato.bow_reloads + gato.stern_reloads);
  CHECK(table.types[2].name == "MARK18-2");
}

TEST_CASE("Mixed Mark 18: half the 18-1 change in the first three months, all after", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const SubClass& gato = *tables.find_sub("GATO");
  const auto total = static_cast<std::ptrdiff_t>(gato.bow_tubes + gato.stern_tubes + gato.bow_reloads +
                                                 gato.stern_reloads);

  SECTION("July 1944: none") {
    TorpedoLoad load = uniform_load(3, 3);
    const TorpedoTypes t = tdc_upgrade_mark18_by_date(load, gato, tables, 14, 6, mixed_rules());
    CHECK(count_of(load, gato, 3) == total);
    // The 18-1, then straight runners not carried.
    CHECK(names(t) == std::vector<std::string_view>{"MARK18-1", "MARK10", "MARK14"});
    CHECK(t.types[0].available);
    CHECK_FALSE(t.types[1].available);
    CHECK_FALSE(t.types[2].available);
    CHECK(t.single_type);
    CHECK(t.selected == 0);
  }
  SECTION("August to October 1944: every other one of each group, the first first") {
    for (const std::int32_t month : {7, 9}) {
      TorpedoLoad load = uniform_load(3, 3);
      const TorpedoTypes t = tdc_upgrade_mark18_by_date(load, gato, tables, 14, month, mixed_rules());
      CHECK(load.tubes[0] == 4);
      CHECK(load.tubes[1] == 3);
      CHECK(load.tubes[2] == 4);
      CHECK(load.bow_racks[0] == 4);
      CHECK(load.bow_racks[1] == 3);
      CHECK(load.stern_racks[0] == 4);
      CHECK(count_of(load, gato, 4) + count_of(load, gato, 3) == total);
      CHECK(std::abs(count_of(load, gato, 4) - count_of(load, gato, 3)) <= 3);
      CHECK(names(t) == std::vector<std::string_view>{"MARK18-1", "MARK18-2", "MARK10"});
      CHECK(t.types[0].available);
      CHECK(t.types[1].available);
      CHECK_FALSE(t.types[2].available);
      CHECK_FALSE(t.single_type);
      // The first tube's type.
      CHECK(t.selected == 1);
    }
  }
  SECTION("November 1944: all") {
    TorpedoLoad load = uniform_load(3, 3);
    tdc_upgrade_mark18_by_date(load, gato, tables, 14, 10, mixed_rules());
    CHECK(count_of(load, gato, 4) == total);
  }
  SECTION("Nothing carried: the Mark 10 stays available") {
    TorpedoLoad load = uniform_load(0, 0);
    const TorpedoTypes t = tdc_upgrade_mark18_by_date(load, gato, tables, 14, 10, mixed_rules());
    CHECK(t.types[0].name == "MARK10");
    CHECK(t.types[0].available);
    CHECK(t.selected == 0);
  }
}

TEST_CASE("Three types at most: the oldest straight runner becomes the newest", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  const SubClass& gato = *tables.find_sub("GATO");
  // September 1944: Mark 14 in the tubes, 18-1 in the bow racks (half of
  // them become 18-2), two Mark 27 aft.
  TorpedoLoad load = uniform_load(2, 3);
  load.stern_racks[0] = static_cast<std::int32_t>(kMark27Id);
  load.stern_racks[1] = static_cast<std::int32_t>(kMark27Id);
  const TorpedoTypes t = tdc_upgrade_mark18_by_date(load, gato, tables, 14, 8, all_rules());
  CHECK(carried_torpedo_types(load, gato) == std::vector<std::uint32_t>{3, 4, kMark27Id});
  CHECK(count_of(load, gato, 2) == 0);
  CHECK(load.tubes[0] == 4);
  CHECK(names(t) == std::vector<std::string_view>{"MARK18-1", "MARK18-2", "MARK27"});
  CHECK(std::ranges::all_of(t.types, &TorpedoTypeChoice::available));

  // Without the gradual rule the Mark 27's table swaps all the 18-1.
  MissionRules only27;
  only27.mark27 = true;
  TorpedoLoad other = uniform_load(2, 3);
  other.stern_racks[0] = static_cast<std::int32_t>(kMark27Id);
  const TorpedoTypes u = tdc_upgrade_mark18_by_date(other, gato, tables, 14, 8, only27);
  CHECK(carried_torpedo_types(other, gato) == std::vector<std::uint32_t>{2, 4, kMark27Id});
  CHECK(names(u) == std::vector<std::string_view>{"MARK14", "MARK18-2", "MARK27"});

  // Four straight runners and the Mark 27: the two oldest go.
  TorpedoLoad five = uniform_load(1, 2);
  five.tubes[1] = 3;
  five.tubes[2] = 4;
  five.bow_racks[0] = static_cast<std::int32_t>(kMark27Id);
  tdc_upgrade_mark18_by_date(five, gato, tables, 14, 6, all_rules());
  CHECK(carried_torpedo_types(five, gato) == std::vector<std::uint32_t>{3, 4, kMark27Id});
}

TEST_CASE("The types carried, in id order", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const SubClass& gato = *tables.find_sub("GATO");
  TorpedoLoad load = uniform_load(0, 0);
  load.tubes[3] = 4;
  load.bow_racks[0] = 2;
  load.stern_racks[1] = 3;
  // Past the class's racks: not carried.
  load.bow_racks[gato.bow_reloads] = 1;
  CHECK(carried_torpedo_types(load, gato) == std::vector<std::uint32_t>{2, 3, 4});
}

TEST_CASE("Generated loads: the real Mark 18-2 mix and the Mark 27, with the same draws", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  const SubClass& gato = *tables.find_sub("GATO");
  // September 1944: a seed of the mixed outcome (Mark 14 and "18-2").
  ScenarioHeader h;
  h.year = 14;
  h.month = 8;
  std::uint32_t seed = 1;
  for (;; ++seed) {
    ScenarioHeader probe = h;
    WatcomRandom r(seed);
    fill_torpedo_load(probe, gato, tables, r);
    if (probe.reloads[1].type != -1) {
      break;
    }
    REQUIRE(seed < 1000);
  }
  ScenarioHeader original = h;
  WatcomRandom r0(seed);
  fill_torpedo_load(original, gato, tables, r0);
  ScenarioHeader mixed = h;
  WatcomRandom r1(seed);
  fill_torpedo_load(mixed, gato, tables, r1, mixed_rules());
  // The same draws: the rest of the scenario is generated alike.
  CHECK(r1.state() == r0.state());
  // The original's "18-2" is an 18-1; the rule's is the 18-2.
  CHECK(original.reloads[1].type == 3);
  CHECK(mixed.reloads[1].type == 4);
  CHECK(mixed.reloads[0] == original.reloads[0]);
  CHECK(mixed.reloads[1].aft == original.reloads[1].aft);
  for (std::size_t t = 0; t < 10; ++t) {
    CHECK((mixed.tube_torpedoes[t] == 4) == (original.tube_torpedoes[t] == 3));
  }

  // The Mark 27: two in the stern racks, from June 1944 for ComSubPac.
  ScenarioHeader with27 = h;
  WatcomRandom r2(seed);
  fill_torpedo_load(with27, gato, tables, r2, all_rules());
  CHECK(r2.state() == r0.state());
  REQUIRE(with27.reloads[2].type == static_cast<std::int32_t>(kMark27Id));
  CHECK(with27.reloads[2].aft == kMark27Rack);
  CHECK(with27.reloads[2].forward == 0);
  CHECK(with27.reloads[3].type == -1);
  CHECK(with27.reloads[0].aft + with27.reloads[1].aft + with27.reloads[2].aft ==
        static_cast<std::int32_t>(gato.stern_reloads));

  ScenarioHeader may = h;
  may.month = 4;
  WatcomRandom r3(seed);
  fill_torpedo_load(may, gato, tables, r3, all_rules());
  for (const TorpedoReload& e : may.reloads) {
    CHECK(e.type != static_cast<std::int32_t>(kMark27Id));
  }
  ScenarioHeader asiatic = h;
  asiatic.month = 6;
  WatcomRandom r5(seed);
  fill_torpedo_load(asiatic, gato, tables, r5, all_rules(), SubCommand::kComSubsAsiatic);
  for (const TorpedoReload& e : asiatic.reloads) {
    CHECK(e.type != static_cast<std::int32_t>(kMark27Id));
  }
  // Without the record (the original's tables) the rule adds nothing.
  const GameTables plain = GameTables::load(install);
  ScenarioHeader none = h;
  WatcomRandom r4(seed);
  fill_torpedo_load(none, gato, plain, r4, all_rules());
  CHECK(none.reloads[2].type == -1);
}

TEST_CASE("The Mark 27's supply dates by command", "[torpedo_types]") {
  CHECK_FALSE(mark27_in_service(14, 4, SubCommand::kComSubPac));
  CHECK(mark27_in_service(14, 5, SubCommand::kComSubPac));
  CHECK_FALSE(mark27_in_service(14, 7, SubCommand::kComSubSoWesPac));
  CHECK(mark27_in_service(14, 8, SubCommand::kComSubSoWesPac));
  CHECK(mark27_in_service(15, 0, SubCommand::kComSubsAsiatic));
  CHECK_FALSE(mark27_in_service(13, 11, SubCommand::kComSubPac));
}

TEST_CASE("The Mark 27 record runs 5000 yd at 12 kn", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  GameTables tables = GameTables::load(install);
  CHECK(tables.find_torpedo("MARK27") == nullptr);
  add_extended_torpedoes(tables);
  add_extended_torpedoes(tables);
  CHECK(std::ranges::count(tables.weapons.torpedoes, kMark27Id, &Torpedo::id) == 1);
  const Torpedo& mk27 = *tables.find_torpedo("MARK27");
  CHECK(mk27.propulsion == TorpedoPropulsion::kElectric);
  CHECK(mk27.nation == Nation::kUs);
  MissionConditions env;
  Simulation sim(env, kDeepSea);
  TorpedoVehicle& t = sim.add(std::make_unique<TorpedoVehicle>(mk27, nullptr, fx::Vec3{0, -48, 0}, Angle10(0),
                                                                Angle10(0), Fixed16::from_int(12), 0, Angle10(0), 10));
  std::int32_t seconds = 0;
  std::int32_t yards = 0;
  Fixed16 cruise;
  while (!t.removed() && seconds < 1500) {
    yards = t.run_yards();
    sim.simulate(Fixed16::from_int(1));
    ++seconds;
    if (seconds == 120) {
      cruise = t.speed();
    }
  }
  CHECK(cruise.to_double() == Catch::Approx(12).margin(0.3));
  CHECK(yards == Catch::Approx(5000).epsilon(0.05));
}

// ------------------------------------------------------------ fire control

TEST_CASE("Auto mode fires a tube on its own type's solution when the table lists it", "[torpedo_types][tdc]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  // A freighter 1500 yd north crossing east at 10 kn; the knob on the
  // 18-2 (40 kn), the tube holding an 18-1 (33 kn).
  SECTION("The rules' table: the TDC switches to the 18-1 and leads for 33 kn") {
    Engagement e(tables, 14, 8, mixed_table());
    e.combat.tdc.set_type_index(2);
    e.combat.tubes.set_tube(0, 3);
    Ship& ship = e.add_ship("LFREIGHT", 0, 1500, Angle10(900), 10);
    e.lock(ship);
    CHECK(signed_tenths(e.combat.tdc.gyro(Tdc::kBow)) == Catch::Approx(lead_tenths(10, 40)).margin(15));
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(e.combat.tdc.type_index() == 1);
    CHECK(signed_tenths(t->course()) == Catch::Approx(lead_tenths(10, 33)).margin(15));
    CHECK(t->weapon().id == 3);
  }
  SECTION("The original's table has no 18-1: the 18-2's solution, as the original") {
    Engagement e(tables, 14, 8, original_table());
    e.combat.tdc.set_type_index(2);
    e.combat.tubes.set_tube(0, 3);
    Ship& ship = e.add_ship("LFREIGHT", 0, 1500, Angle10(900), 10);
    e.lock(ship);
    const Angle10 gyro = e.combat.tdc.gyro(Tdc::kBow);
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(e.combat.tdc.type_index() == 2);
    CHECK(t->course() == gyro);
    CHECK(signed_tenths(t->course()) == Catch::Approx(lead_tenths(10, 40)).margin(15));
  }
}

TEST_CASE("Mark 27 knob, straight runner in the tube", "[torpedo_types][tdc]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  Engagement e(tables, 14, 8, mark27_table());
  e.combat.tdc.set_type_index(2);
  e.combat.tubes.set_tube(0, 4);
  SECTION("Auto, a bearing only: not fired, no solution, the knob stays") {
    e.frames(2);
    CHECK(e.combat.tdc.bearing_only());
    e.sim.take_events();
    CHECK(e.fire(0) == nullptr);
    const MissionEvents events = e.sim.take_events();
    REQUIRE(events.crew_messages.size() == 1);
    CHECK(events.crew_messages[0].id == PlayerCombat::kMsgNoSolution);
    CHECK(e.combat.tdc.type_index() == 2);
    CHECK(e.combat.tubes.content(0) == 4);
  }
  SECTION("Auto, a target locked: fired on the straight runner's solution") {
    Ship& ship = e.add_ship("LFREIGHT", 0, 1500, Angle10(900), 10);
    e.lock(ship);
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(e.combat.tdc.type_index() == 1);
    CHECK(signed_tenths(t->course()) == Catch::Approx(lead_tenths(10, 40)).margin(15));
  }
  SECTION("Manual: no switch, fired along the dialled bearing") {
    e.combat.tdc.set_mode(Tdc::Mode::kManual);
    e.combat.tdc.set_bearing(Angle10(200));
    e.frames(2);
    CHECK(e.combat.tdc.gyro(Tdc::kBow) == Angle10(200));
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(e.combat.tdc.type_index() == 2);
    CHECK(t->course() == Angle10(200));
    CHECK(t->weapon().id == 4);
  }
}

TEST_CASE("Straight runner knob, Mark 27 in the tube", "[torpedo_types][tdc]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  Engagement e(tables, 14, 8, mark27_table());
  e.combat.tdc.set_type_index(0);
  e.combat.tubes.set_tube(0, static_cast<std::int32_t>(kMark27Id));
  SECTION("Auto, a target locked: the lead for 12 kn") {
    Ship& ship = e.add_ship("LFREIGHT", 0, 1500, Angle10(900), 6);
    e.lock(ship);
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(e.combat.tdc.type_index() == 2);
    CHECK_FALSE(e.combat.tdc.no_intercept());
    CHECK(signed_tenths(t->course()) == Catch::Approx(lead_tenths(6, 12)).margin(25));
    CHECK(t->homing() != nullptr);
  }
  SECTION("Auto, no lock: the loudest high-speed screws heard") {
    Ship& ship = e.add_ship("LFREIGHT", 800, 800, Angle10(900), 10);
    e.combat.heard = {{&ship, Angle10(450), 50}};
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(t->course() == Angle10(450));
  }
  SECTION("Manual: the set gyro") {
    e.combat.tdc.set_mode(Tdc::Mode::kManual);
    e.combat.tdc.set_gyro(Tdc::kBow, Angle10(150));
    TorpedoVehicle* t = e.fire(0);
    REQUIRE(t != nullptr);
    CHECK(e.combat.tdc.type_index() == 0);
    CHECK(t->course() == Angle10(150));
  }
}

TEST_CASE("The Mark 27's solution: the lead, the bearing, or no intercept", "[torpedo_types][tdc]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  Engagement e(tables, 14, 8, mark27_table());
  e.combat.tdc.set_type_index(2);
  SECTION("A full solution: the lead at 12 kn") {
    Ship& ship = e.add_ship("LFREIGHT", 0, 1500, Angle10(900), 6);
    e.lock(ship);
    CHECK(e.combat.tdc.bearing_only());
    CHECK_FALSE(e.combat.tdc.no_intercept());
    CHECK(signed_tenths(e.combat.tdc.gyro(Tdc::kBow)) == Catch::Approx(lead_tenths(6, 12)).margin(25));
  }
  SECTION("No intercept: a 30-kn destroyer crossing square: its bearing") {
    Ship& ship = e.add_ship("KAGERO", 0, 1500, Angle10(900), 30);
    e.lock(ship);
    CHECK(e.combat.tdc.no_intercept());
    CHECK(e.combat.tdc.gyro(Tdc::kBow) == e.combat.tdc.bearing());
    CHECK(signed_tenths(e.combat.tdc.gyro(Tdc::kBow)) == Catch::Approx(0).margin(20));
  }
  SECTION("A bearing only: the sonar source's, cycled by the TRACK button") {
    Ship& near = e.add_ship("LFREIGHT", 600, 600, Angle10(900), 10);
    Ship& far = e.add_ship("LFREIGHT", -2000, 2000, Angle10(900), 10);
    const auto hear = [&] {
      for (Ship* s : {&near, &far}) {
        s->ai().heard.heard = true;
        s->ai().heard.screw = ScrewSound::kHighSpeed;
      }
      near.ai().heard.bearing = Angle10(450);
      far.ai().heard.bearing = Angle10(3150);
    };
    hear();
    SimContext ctx = e.sim.context();
    e.combat.update_frame(ctx, *e.player);
    REQUIRE(e.combat.heard.size() == 2);
    CHECK(e.combat.heard[0].vehicle == &near);
    e.combat.tdc.cycle_sonar_source(e.combat.heard);
    CHECK(e.combat.tdc.sonar_source() == &near);
    e.combat.tdc.cycle_sonar_source(e.combat.heard);
    CHECK(e.combat.tdc.sonar_source() == &far);
    for (int i = 0; i < 2; ++i) {
      e.sim.step(10);
      hear();
      SimContext later = e.sim.context();
      e.combat.update_frame(later, *e.player);
    }
    CHECK(e.combat.tdc.bearing() == Angle10(3150));
    CHECK(e.combat.tdc.gyro(Tdc::kBow) == Angle10(3150));
    e.combat.tdc.cycle_sonar_source(e.combat.heard);
    CHECK(e.combat.tdc.sonar_source() == nullptr);
  }
}

TEST_CASE("The original's table has no bearing-only solution", "[torpedo_types][tdc]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  Engagement e(tables, 14, 8, original_table());
  e.combat.tdc.set_type_index(2);
  e.combat.tubes.set_tube(0, 4);
  e.frames(2);
  CHECK_FALSE(e.combat.tdc.bearing_only());
  CHECK(e.combat.heard.empty());
  // Auto mode without a target fires straight ahead, as the original.
  TorpedoVehicle* t = e.fire(0);
  REQUIRE(t != nullptr);
  CHECK(t->course() == Angle10(0));
  CHECK(t->homing() == nullptr);
}

// ------------------------------------------------------------ the homing

TEST_CASE("The Mark 27 homes on the loudest screws in its cone after 400 yd", "[torpedo_types][homing]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  const Torpedo& mk27 = *tables.find_torpedo("MARK27");
  Engagement e(tables, 14, 8, mark27_table());
  // A ship 300 yd west of the torpedo's track, 900 yd up it, crossing it
  // east at 10 kn: some 40 yd east of it, 450 yd ahead at the enable point.
  Ship& escort = e.add_ship("LFREIGHT", -300, 900, Angle10(900), 10);
  TorpedoVehicle& t = e.sim.add(std::make_unique<TorpedoVehicle>(
      mk27, e.player, fx::Vec3{kOrigin, -10 * kUnitsPerFoot, kOrigin + 40 * kYard}, Angle10(0), Angle10(0),
      Fixed16::from_int(12), 0, Angle10(0), 60));
  REQUIRE(t.homing() != nullptr);
  const auto strength = escort.setup().hit_points;
  bool steered_early = false;
  bool homed = false;
  // (The simulation frees the torpedo once it is spent.)
  const auto running = [&e, &t] {
    return std::ranges::any_of(e.sim.vehicles(), [&t](const auto& v) { return v.get() == &t && !v->removed(); });
  };
  for (std::int32_t s = 0; s < 400 && running(); ++s) {
    if (t.run_yards() < TorpedoHoming::kEnableYards - 20 && t.course() != Angle10(0)) {
      steered_early = true;
    }
    homed = homed || t.homing()->target == &escort;
    e.run(1);
  }
  CHECK(homed);
  CHECK_FALSE(steered_early);
  CHECK(escort.setup().hit_points < strength);
}

TEST_CASE("The Mark 27 ignores stopped screws, and its own boat for two minutes", "[torpedo_types][homing]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  const Torpedo& mk27 = *tables.find_torpedo("MARK27");
  Engagement e(tables, 14, 8, mark27_table());
  SECTION("A stopped ship") {
    e.add_ship("LFREIGHT", 300, 900, Angle10(900), 0);
    TorpedoVehicle& t = e.sim.add(std::make_unique<TorpedoVehicle>(
        mk27, e.player, fx::Vec3{kOrigin, -10 * kUnitsPerFoot, kOrigin + 40 * kYard}, Angle10(0), Angle10(0),
        Fixed16::from_int(12), 0, Angle10(0), 60));
    e.run(150);
    REQUIRE(t.homing() != nullptr);
    CHECK(t.homing()->enabled);
    CHECK(t.homing()->target == nullptr);
    CHECK(t.course() == Angle10(0));
  }
  SECTION("Its own boat, running ahead of it") {
    // The boat makes way north at 6..12 kn; the torpedo follows from 600
    // yd astern, closing.
    e.player->set_telegraph(2);
    e.run(240);
    REQUIRE(e.player->speed().to_double() > TorpedoHoming::kMinKnots);
    REQUIRE(e.player->speed().to_double() < 12);
    const fx::Vec3 p = e.player->position();
    TorpedoVehicle& t = e.sim.add(std::make_unique<TorpedoVehicle>(
        mk27, e.player, fx::Vec3{p.x, -10 * kUnitsPerFoot, p.z - 600 * kYard}, Angle10(0), Angle10(0),
        Fixed16::from_int(12), 0, Angle10(0), 60));
    const TorpedoHoming& h = *t.homing();
    while (h.run_time < (TorpedoHoming::kLauncherSafe - 5) << 16) {
      e.sim.step(10);
    }
    CHECK(h.enabled);
    CHECK(h.target == nullptr);
    while (h.run_time < (TorpedoHoming::kLauncherSafe + 5) << 16) {
      e.sim.step(10);
    }
    CHECK(h.target == e.player);
  }
}

// ------------------------------------------------------------ saves

TEST_CASE("The torpedo state is saved under the torpedo rules only, and comes back", "[torpedo_types][save]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = extended_tables(install);
  const auto make = [&](const MissionRules& rules) {
    Scenario sc = test::mixed_scenario(install, true);
    sc.header.tube_torpedoes[9] = static_cast<std::int32_t>(kMark27Id);
    MissionSetup setup;
    setup.options = RealismFile::kDefaultBits;
    setup.random = WatcomRandom(7);
    setup.rules = rules;
    return Mission::create(install, tables, std::move(sc), std::move(setup));
  };
  SECTION("The original's rules: the original's table and save") {
    const std::unique_ptr<Mission> m = make({});
    CHECK(m->player_combat().torpedo_types[2].name == "MARK18-1");
    const MissionSaveFile save = capture_mission_save(install, *m, 0, "Torpedoes", {});
    CHECK(save.state.rules_extension.empty());
    // Restored, the start types are those carried.
    const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(save.serialize());
    REQUIRE(parsed);
    MissionSetup setup;
    setup.random = WatcomRandom(1);
    const std::unique_ptr<Mission> back = restore_mission(install, tables, *parsed, setup);
    CHECK(back->start_torpedo_types() == m->start_torpedo_types());
  }
  SECTION("The torpedo rules: the table, the start types and a running Mark 27") {
    const std::unique_ptr<Mission> m = make(all_rules());
    PlayerCombat& pc = m->player_combat();
    // Feb 1944: Mark 14, 18-1, 18-2 and 27 aboard: the Mark 14 became an
    // 18-2.
    CHECK(m->start_torpedo_types() == std::vector<std::uint32_t>{3, 4, kMark27Id});
    REQUIRE(pc.torpedo_types[2].name == "MARK27");
    pc.tdc.set_mode(Tdc::Mode::kManual);
    pc.tdc.set_type_index(2);
    SimContext ctx = m->sim().context();
    TorpedoVehicle* t = pc.fire_tube(ctx, m->player(), 9);
    REQUIRE(t != nullptr);
    for (int i = 0; i < 700; ++i) {
      m->simulate_frame(10, false);
    }
    const TorpedoVehicle* running = nullptr;
    for (const auto& v : m->sim().vehicles()) {
      if (const auto* tv = dynamic_cast<const TorpedoVehicle*>(v.get()); tv != nullptr && tv->homing() != nullptr) {
        running = tv;
      }
    }
    REQUIRE(running != nullptr);
    REQUIRE(running->homing()->enabled);
    pc.tdc.cycle_sonar_source(std::vector<HeardScrews>{{m->ships().front(), Angle10(100), 1}});
    const MissionSaveFile save = capture_mission_save(install, *m, 0, "Torpedoes", {});
    const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(save.serialize());
    REQUIRE(parsed);
    MissionSetup setup;
    setup.random = WatcomRandom(1);
    const std::unique_ptr<Mission> back = restore_mission(install, tables, *parsed, setup);
    CHECK(back->player_combat().torpedo_types[2].name == "MARK27");
    CHECK(back->player_combat().tdc.sonar_source() == back->ships().front());
    CHECK(back->start_torpedo_types() == m->start_torpedo_types());
    const TorpedoVehicle* restored = nullptr;
    for (const auto& v : back->sim().vehicles()) {
      if (const auto* tv = dynamic_cast<const TorpedoVehicle*>(v.get()); tv != nullptr && tv->homing() != nullptr) {
        restored = tv;
      }
    }
    REQUIRE(restored != nullptr);
    CHECK(restored->homing()->enabled);
    CHECK(restored->homing()->run_time == running->homing()->run_time);
    CHECK(restored->homing()->listen_timer == running->homing()->listen_timer);
  }
}

// ------------------------------------------------------------ messages

TEST_CASE("The extended crew message comes after the original's table", "[torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const CrewMessages messages(ExeImage::load(install));
  CHECK(messages.all().size() == 77);
  CHECK(std::ranges::none_of(messages.all(), [](const CrewMessage& m) { return m.id == 0x80; }));
  const CrewMessage* m = messages.find(PlayerCombat::kMsgNoSolution);
  REQUIRE(m != nullptr);
  CHECK(m->text == "No firing solution, sir!");
  CHECK(m->speaker == CrewSpeaker::kExecutiveOfficer);
  CHECK(m->sound.empty());
}

}  // namespace sh
