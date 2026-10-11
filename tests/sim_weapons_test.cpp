#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>

#include "assets/dat_tables.h"
#include "game/class_params.h"
#include "game_data.h"
#include "sim/collision.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/ship_weapons.h"
#include "sim/depth_charge.h"
#include "sim/simulation.h"
#include "sim/tdc.h"
#include "sim/torpedo.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kOrigin = 2000000;
constexpr std::int32_t kYard = kUnitsPerYard;

// A mission with the player's Gato surfaced and stopped at the origin
// heading north, its combat state, at `year` (since 1930) and `month`.
struct Engagement {
  Engagement(const GameTables& t, std::uint32_t options, std::int32_t year, std::int32_t month,
             std::uint32_t seed = 1)
      : tables(t),
        gato(*t.find_sub("GATO")),
        sim(conditions(options), kDeepSea, seed),
        combat(t, gato) {
    sim.clock().init(year, month, 1, 12, 0);
    combat.torpedo_types = {{{"MARK10", true, 1}, {"MARK14", true, 2}, {"MARK18-1", false, 3}}};
    combat.tdc.reset();
    combat.tdc.set_type_index(1);
    sim.set_player_combat(&combat);
    player = &sim.add(std::make_unique<PlayerSub>(sub_class_params(gato), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 0));
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

  // Runs whole seconds of game time.
  void run(std::int32_t seconds) {
    for (std::int32_t i = 0; i < seconds; ++i) {
      sim.simulate(Fixed16::from_int(1));
    }
  }

  const GameTables& tables;
  const SubClass& gato;
  Simulation sim;
  PlayerCombat combat;
  PlayerSub* player = nullptr;
};

std::int32_t count_messages(const MissionEvents& e, std::int32_t id) {
  std::int32_t n = 0;
  for (const CrewReport& r : e.crew_messages) {
    n += r.id == id ? 1 : 0;
  }
  return n;
}

}  // namespace

TEST_CASE("The weapons' angle helpers match the original tables", "[weapons]") {
  CHECK(fx::fixed_asin(0x10000) == 900);
  CHECK(fx::fixed_asin(0x8000) == 300);
  CHECK(fx::fixed_asin(-0x8000) == -300);
  CHECK(fx::fixed_asin(0x10001) == 0);
  CHECK(fx::angle_add(Angle10(3500), Angle10(200)) == Angle10(100));
  CHECK(fx::angle_add(Angle10(100), Angle10(-200)) == Angle10(-100));  // Not folded, as the original.
  CHECK(kFeetPerSecondPerKnot == 0x1b024);
  CHECK(kUnitsPerSecondPerKnot == 0x1b0246);
}

TEST_CASE("Collision boxes overlap only where the hulls touch", "[weapons]") {
  const fx::Mat3 id;
  const CollisionBox ship = make_hull_box({0, 0, 0}, id, id, 16 * 30, 16 * 200, -16 * 20, 16 * 50);
  const CollisionBox near = make_hull_box({16 * 25, -160, 16 * 150}, id, id, 16, 96, -176, -144, 96);
  const CollisionBox abeam = make_hull_box({16 * 40, -160, 0}, id, id, 16, 96, -176, -144, 96);
  const CollisionBox deep = make_hull_box({0, -16 * 40, 0}, id, id, 16, 96, -16 * 41, -16 * 39, 96);
  const CollisionBox unarmed = make_hull_box({0, -160, 0}, id, id, 16, 96, -176, -144, 0);
  CHECK(boxes_intersect(ship, near));
  CHECK_FALSE(boxes_intersect(ship, abeam));
  CHECK_FALSE(boxes_intersect(ship, deep));
  CHECK_FALSE(boxes_intersect(ship, unarmed));
}

TEST_CASE("A Mark 14 runs its WEAPONS.DAT range at its set depth", "[weapons][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const Torpedo& mk14 = *tables.find_torpedo("MARK14");
  REQUIRE(mk14.run_time_s[0] == 530);
  MissionConditions env;
  Simulation sim(env, kDeepSea);
  TorpedoVehicle& t = sim.add(std::make_unique<TorpedoVehicle>(mk14, nullptr, fx::Vec3{0, -48, 0}, Angle10(0),
                                                                Angle10(0), Fixed16::from_int(31), 0, Angle10(0), 10));
  std::int32_t seconds = 0;
  std::int32_t yards = 0;
  while (!t.removed() && seconds < 700) {
    yards = t.run_yards();
    sim.simulate(Fixed16::from_int(1));
    ++seconds;
    if (seconds == 10) {
      CHECK(t.depth_ft() == Catch::Approx(10).margin(1));  // 2 ft per 0.2 s step toward it.
    }
  }
  // The run countdown burns 1 s per s at full power: 530 s at 31 kn.
  CHECK(seconds == Catch::Approx(530).margin(1));
  CHECK(yards == Catch::Approx(31 * 1.6878 * 530 / 3).epsilon(0.02));
  CHECK(yards == Catch::Approx(static_cast<double>(mk14.range_long_yd)).epsilon(0.05));
  CHECK(t.has_wake());
  CHECK_FALSE(TorpedoVehicle(*tables.find_torpedo("MARK18-1"), nullptr, {}, {}, {}, {}, 0, {}, 10).has_wake());
}

TEST_CASE("Mark 14s run deep and fire early until their fix dates", "[weapons][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const Torpedo& mk14 = *tables.find_torpedo("MARK14");
  const auto fire = [&](Engagement& e) -> TorpedoVehicle& {
    return e.sim.add(std::make_unique<TorpedoVehicle>(mk14, e.player, fx::Vec3{0, -48, 0}, Angle10(0), Angle10(0),
                                                      Fixed16::from_int(46), 1, Angle10(0), 10));
  };
  for (std::uint32_t seed = 1; seed < 20; ++seed) {
    // June 1942: deep running (rand(10) ft more), and 20 % prematures
    // after rand(60) s (ComSubsAsiatic: until 1944).
    Engagement early(tables, combat_option::kDudTorpedoes, 12, 5, seed);
    WatcomRandom r = early.sim.random();
    const TorpedoVehicle& t = fire(early);
    CHECK(t.set_depth_ft() == 10 + r.random_int(10));
    CHECK(t.premature_timer() == (r.random_int(100) < 20 ? r.random_int(60) << 16 : -1));

    // August 1942: the depth is fixed.
    Engagement later(tables, combat_option::kDudTorpedoes, 12, 7, seed);
    CHECK(fire(later).set_depth_ft() == 10);
    // Without the Dud Torpedoes option nothing is drawn.
    Engagement fixed(tables, 0, 12, 5, seed);
    const WatcomRandom before = fixed.sim.random();
    CHECK(fire(fixed).set_depth_ft() == 10);
    WatcomRandom b = before;
    WatcomRandom a = fixed.sim.random();
    CHECK(a.rand() == b.rand());
  }
  // The fix-date test compares with the month before.
  GameClock clock;
  clock.init(13, 7, 1, 0, 0);
  CHECK(before_fix(clock, torpedo_fix_dates().contact_exploder) == false);
  clock.init(13, 6, 31, 0, 0);
  CHECK(before_fix(clock, torpedo_fix_dates().contact_exploder));
}

TEST_CASE("With the rules' corrections the Mark 14 faults last until their fix days", "[weapons][rules]") {
  const MissionRules fixed = MissionRules::extended();
  const TorpedoFixDates& dates = torpedo_fix_dates();
  for (const FaultDate& d : {dates.deep_running, dates.contact_exploder, dates.magnetic_exploder_off[1]}) {
    GameClock clock;
    // The month before the fix: over for the original, not with the
    // corrections.
    REQUIRE(d.month > 1);
    clock.init(d.year, d.month - 1, 15, 0, 0);
    CHECK_FALSE(before_fix(clock, d));
    CHECK_FALSE(before_fix(clock, d, MissionRules{}));
    CHECK(before_fix(clock, d, fixed));
    // The fix's day: over for both.
    clock.init(d.year, d.month, d.day, 0, 0);
    CHECK_FALSE(before_fix(clock, d));
    CHECK_FALSE(before_fix(clock, d, fixed));
    // Two months before: faulty for both.
    clock.init(d.year, d.month - 2, 1, 0, 0);
    CHECK(before_fix(clock, d));
    CHECK(before_fix(clock, d, fixed));
  }
}

TEST_CASE("A square Mark 14 hit is a dud two times in three before Aug 1943", "[weapons][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const Torpedo& mk14 = *tables.find_torpedo("MARK14");
  // Fires at a ship lying across the track 600 yd ahead; returns 1 for a
  // dud, 0 for a hit.
  const auto shot = [&](std::int32_t month, std::uint32_t seed) {
    Engagement e(tables, combat_option::kDudTorpedoes, 13, month, seed);
    e.combat.command = 1;  // ComSubPac: no magnetic prematures after Jun 1943.
    Ship& ship = e.add_ship("LFREIGHT", 0, 600, Angle10(900), 0, ShipOrder::kNone);
    e.sim.add(std::make_unique<TorpedoVehicle>(mk14, e.player,
                                               fx::Vec3{kOrigin, -10 * kUnitsPerFoot, kOrigin + 100 * kYard},
                                               Angle10(0), Angle10(0), Fixed16::from_int(46), 1, Angle10(0), 10));
    e.run(60);
    const MissionEvents events = e.sim.take_events();
    const bool hit = ship.setup().hit_points < static_cast<std::int32_t>(ship.setup().ship_class->hull_strength);
    REQUIRE(hit != (count_messages(events, 0x13) == 1));
    return hit ? 0 : 1;
  };
  constexpr int kShots = 300;
  int duds = 0;
  for (int i = 0; i < kShots; ++i) {
    duds += shot(6, static_cast<std::uint32_t>(i * 7919 + 1));
  }
  // p = (90 deg / 10) / 135.
  CHECK(static_cast<double>(duds) / kShots == Catch::Approx(90.0 / 135).margin(0.08));
  int late_duds = 0;
  for (int i = 0; i < kShots; ++i) {
    late_duds += shot(8, static_cast<std::uint32_t>(i * 7919 + 1));
  }
  // Fixed: the 3 % of every torpedo.
  CHECK(static_cast<double>(late_duds) / kShots == Catch::Approx(0.03).margin(0.03));
}

TEST_CASE("The TDC solution puts a torpedo into a ship on a steady course", "[weapons][tdc][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  Engagement e(tables, 0, 13, 6);
  // A freighter 1500 yd north crossing east at 10 kn.
  Ship& ship = e.add_ship("LFREIGHT", 0, 1500, Angle10(900), 10, ShipOrder::kFollowPath);
  e.combat.tubes.set_tube(0, 2);
  e.combat.tdc.set_target(&ship);
  e.combat.tdc.set_sighting_range(20000);
  for (int i = 0; i < 10; ++i) {
    e.sim.step(10);
    SimContext ctx = e.sim.context();
    e.combat.update_frame(ctx, *e.player);
  }
  // Novice realism: the estimate is exact.
  CHECK(e.combat.tdc.quality() == 100);
  CHECK(e.combat.tdc.tracking());
  CHECK(e.combat.tdc.range_yd() == Catch::Approx(1500).margin(10));
  CHECK(e.combat.tdc.course().tenths() == Catch::Approx(900).margin(1));
  // Lead: asin(10 / 46) = 12.6 deg to the right.
  CHECK(e.combat.tdc.gyro(Tdc::kBow).tenths() == Catch::Approx(126).margin(15));
  SimContext ctx = e.sim.context();
  TorpedoVehicle* torpedo = e.combat.fire_tube(ctx, *e.player, 0);
  REQUIRE(torpedo != nullptr);
  CHECK(e.combat.tubes.content(0) == 0);
  // (The run timer meets the x coordinates, ill-conditioned on a track
  // crossing at right angles: not checked.)
  CHECK(e.combat.tdc.shortest_run_timer().has_value());
  const double run_time = 1500 * 3 / (46 * 1.6878);

  std::int32_t seconds = 0;
  const auto strength = static_cast<std::int32_t>(ship.setup().ship_class->hull_strength);
  while (ship.setup().hit_points == strength && seconds < 120) {
    for (int i = 0; i < 10; ++i) {
      e.sim.step(10);
    }
    ++seconds;
  }
  CHECK(ship.setup().hit_points < strength);
  CHECK(seconds == Catch::Approx(run_time).margin(4));
  // (wh - wh / 4 + rand(wh / 2)) / 10 = 50..83, x 3 at the lowest combat
  // level: enough to sink the freighter.
  CHECK(ship.setup().hit_points <= std::max(0, strength - 150));
  e.sim.step(10);
  CHECK(ship.sinking());
  CHECK(e.sim.take_events().ships_sunk.size() == 1);
}

TEST_CASE("Tubes refuse an empty tube and a gyro angle over 130 deg", "[weapons][tdc]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Engagement e(tables, 0, 13, 6);
  REQUIRE(e.gato.bow_tubes == 6);
  REQUIRE(e.gato.stern_tubes == 4);
  e.combat.tubes.set_tube(0, 2);
  e.combat.tubes.set_tube(6, 2);
  SimContext ctx = e.sim.context();
  CHECK(e.combat.fire_tube(ctx, *e.player, 1) == nullptr);
  CHECK(e.sim.take_events().crew_messages.at(0).id == PlayerCombat::kMsgNotLoaded);

  // A dialled target dead astern.
  e.combat.tdc.set_mode(Tdc::Mode::kManual);
  e.combat.tdc.set_range_yd(1000);
  e.combat.tdc.mark_target(e.combat.own_boat(*e.player, ctx), Angle10(1800), e.sim.vehicles(), {});
  e.combat.tdc.toggle_track();
  e.sim.step(10);
  SimContext ctx2 = e.sim.context();
  e.combat.update_frame(ctx2, *e.player);
  CHECK(e.combat.tdc.gyro(Tdc::kBow) == Angle10(1800));
  CHECK(e.combat.tdc.gyro(Tdc::kStern).tenths() % 3600 == 0);
  CHECK(e.combat.fire_tube(ctx2, *e.player, 0) == nullptr);
  CHECK(e.sim.take_events().crew_messages.at(0).id == PlayerCombat::kMsgGyroLimit);
  CHECK(e.combat.tubes.content(0) == 2);
  TorpedoVehicle* t = e.combat.fire_tube(ctx2, *e.player, 6);
  REQUIRE(t != nullptr);
  CHECK(t->heading() == Angle10(1800));
}

TEST_CASE("Reloads take the documented time, one at a time per room", "[weapons]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  for (const bool realistic : {false, true}) {
    Engagement e(tables, realistic ? combat_option::kRealisticReloads : 0, 13, 6);
    e.combat.crew = 1;
    TorpedoTubes& tubes = e.combat.tubes;
    tubes.bow_rack()[0] = 2;
    tubes.bow_rack()[1] = 2;
    SimContext ctx = e.sim.context();
    WatcomRandom r = e.sim.random();
    tubes.start_reload(ctx, 0, 1);
    tubes.start_reload(ctx, 1, 1);
    const std::int32_t base = realistic ? 420 : 60;
    const std::int32_t first = r.random_int(realistic ? 120 : 60) + base + base / 2;
    const std::int32_t second = r.random_int(realistic ? 120 : 60) + base + base / 2;
    CHECK(tubes.content(0) == -2);
    CHECK(tubes.timer(0) == first << 16);
    CHECK(tubes.timer(1) == (first + second) << 16);  // Queued after tube 1.
    CHECK(tubes.bow_rack()[0] == 0);
    for (std::int32_t s = 0; s < first + 1; ++s) {
      tubes.update(ctx, false, Fixed16::from_int(1), 1);
    }
    CHECK(tubes.content(0) == 2);
    CHECK(tubes.content(1) == -2);
    const MissionEvents events = e.sim.take_events();
    REQUIRE(count_messages(events, TorpedoTubes::kMsgLoaded) == 1);
    CHECK(events.crew_messages.back().arg == 1);
  }
}

TEST_CASE("An escort drops its racks across the stern, then throws", "[weapons]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Engagement e(tables, 0, 13, 6);
  Ship& dd = e.add_ship("FUBUKI", 0, 3000, Angle10(0), 0, ShipOrder::kNone);
  const std::int32_t racks = dd.ai().cls.rack_charges;
  const std::int32_t throwers = dd.ai().cls.thrower_charges;
  REQUIRE(racks + throwers > 0);
  dd.ai().depth_charges = racks + throwers + 5;
  ShipWeapons weapons(tables);
  SimContext ctx = e.sim.context();
  WatcomRandom r = e.sim.random();
  weapons.drop_depth_charges(dd, ctx, 100);
  CHECK(dd.ai().depth_charges == 5);
  CHECK(dd.ai().charges_dropped == racks);
  std::int32_t k = 0;
  for (const auto& v : e.sim.vehicles()) {
    if (const auto* dc = dynamic_cast<const DepthCharge*>(v.get())) {
      CHECK(dc->set_depth_ft() == (k < racks ? r.random_int(30) + 85 : r.random_int(50) + 75));
      ++k;
    }
  }
  CHECK(k == racks + throwers);
}

}  // namespace sh
