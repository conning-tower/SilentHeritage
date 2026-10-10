#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/line_of_sight.h"
#include "sim/player_sub.h"
#include "sim/radar.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/sighting.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim_classes.h"
#include "sim_test_grid.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;

// A submerged target of fixed noise for the escort sonar.
class QuietSub : public Vehicle {
 public:
  QuietSub(fx::Vec3 position, std::int32_t noise) : position_(position), noise_(noise) {}
  VehicleClass class_id() const override { return VehicleClass::kEnemySub; }
  void init(SimContext&) override {}
  std::int32_t update_pass() const override { return 1; }
  void update(SimContext&, Fixed16) override {}
  fx::Vec3 position() const override { return position_; }
  bool is_submarine() const override { return true; }
  std::int32_t noise_level() const override { return noise_; }

 private:
  fx::Vec3 position_;
  std::int32_t noise_;
};

// A ship class for the AI and sensors: a Kagero destroyer (SHIPS.DAT).
ShipAiClass kagero() {
  ShipAiClass c;
  c.type = ShipType::kDestroyer;
  c.role = ShipRole::kEscort;
  c.screw = ScrewSound::kHighSpeed;
  c.has_sonar = true;
  c.depth_charges = 36;
  c.rack_charges = 2;
  c.thrower_charges = 2;
  c.gun_count = 4;
  c.length_ft = 388;
  c.draft_ft = 12;
  c.visual_size = 100;
  c.lookout_ft = 25;
  return c;
}

ShipAiClass sampan() {
  ShipAiClass c;
  c.type = ShipType::kSampan;
  c.role = ShipRole::kSmallCraft;
  c.screw = ScrewSound::kNone;
  c.length_ft = 80;
  c.draft_ft = 5;
  c.visual_size = 120;
  c.lookout_ft = 6;
  return c;
}

Ship& add_ship(Simulation& sim, const ShipAiClass& cls, fx::Vec3 at, Angle10 heading, Fixed16 speed,
               ShipOrder orders, std::int32_t crew = 1) {
  auto ship = std::make_unique<Ship>(test::fubuki(), at, heading, speed, orders);
  ship->ai().cls = cls;
  ship->setup().crew = crew;
  return sim.add(std::move(ship));
}

std::vector<CrewReport> reports(Simulation& sim) { return sim.take_events().crew_messages; }

bool has_report(const std::vector<CrewReport>& r, std::int32_t id, std::int32_t arg) {
  return std::any_of(r.begin(), r.end(), [&](const CrewReport& c) { return c.id == id && c.arg == arg; });
}

}  // namespace

TEST_CASE("The sighting range is the mean horizon, cut by light, weather and fog", "[sensors]") {
  // 1.145 x 2027 x sqrt(h) yd: 68 ft gives 19138 yd, 100 ft 23208.
  SightingRange clear = sighting_compute_range(68, 100, {});
  CHECK(clear.horizon_yd == 19138);
  CHECK(clear.range_yd == 21173);
  // -(100 - 50) x 0.8 %, -2 x 15 %, -30 %, applied in turn.
  CHECK(sighting_compute_range(68, 100, {2, 30, 50}).range_yd == 6226);
  CHECK(sighting_compute_range(0, 100, {}).range_yd == 0);
  // The aircraft sortie distance: bridge to a 5000 ft target, +5 %.
  CHECK(sighting_reference_range({kOrigin, 0, kOrigin}, 40) == 25053);
}

TEST_CASE("Grid lines step the minor axis by a half-started DDA", "[sensors]") {
  std::vector<std::pair<int, int>> pixels;
  walk_line(0, 0, 5, 2, 9, 9, [&](int x, int y) { pixels.emplace_back(x, y); });
  CHECK(pixels == std::vector<std::pair<int, int>>{{0, 0}, {1, 0}, {2, 1}, {3, 1}, {4, 2}, {5, 2}});
  pixels.clear();
  // Pixels outside the grid are skipped.
  walk_line(-2, 0, 1, 0, 9, 9, [&](int x, int y) { pixels.emplace_back(x, y); });
  CHECK(pixels == std::vector<std::pair<int, int>>{{0, 0}, {1, 0}});
}

TEST_CASE("Land on the line hides targets lower than it", "[sensors]") {
  test::TestGrid grid(12, 12);
  grid.set(5, 5, 0x22);  // Two levels: 700 ft.
  const geom::WorldXZ from{grid.x_of(1), grid.z_of(5)};
  const geom::WorldXZ to{grid.x_of(8), grid.z_of(5)};
  const LandOnLine line = land_on_line(grid, from, to);
  CHECK(line.land);
  CHECK(line.max_level == 2);
  CHECK(los_blocked_by_land(grid, from, to, 100 * kUnitsPerFoot));
  CHECK_FALSE(los_blocked_by_land(grid, from, to, 701 * kUnitsPerFoot));
  // A line passing north of it is clear; four coast cells count as land.
  CHECK_FALSE(land_on_line(grid, {grid.x_of(1), grid.z_of(2)}, {grid.x_of(8), grid.z_of(2)}).land);
  for (int c = 0; c < 12; ++c) {
    grid.set(c, 8, kCoastCell);
    grid.set(c, 9, kCoastCell);
  }
  CHECK(land_on_line(grid, {grid.x_of(1), grid.z_of(8)}, {grid.x_of(8), grid.z_of(8)}).land);

  const SightingCheck check =
      sighting_check({}, &grid, {from.x, 68 * kUnitsPerFoot, from.z}, {to.x, 100 * kUnitsPerFoot, to.z});
  CHECK(check.distance_yd == SightingCheck::kBlockedByLand);
}

TEST_CASE("AI lookouts lose range to crew, aspect and a submerged boat", "[sensors]") {
  // Observer eye 25 ft, target top 8 ft: 9084 yd, then -20 % and -20 % for
  // an average crew: 5815 yd.
  const fx::Vec3 eye{0, 25 * kUnitsPerFoot, 0};
  AiSightingTarget beam{{0, 8 * kUnitsPerFoot, 5000 * kYard}, Angle10(900)};
  CHECK(ai_detect_target({}, 0, nullptr, eye, beam, 1, false).distance_yd == 5000);
  beam.position.z = 5800 * kYard;  // Abeam the range is 5796 yd.
  CHECK_FALSE(ai_detect_target({}, 0, nullptr, eye, beam, 1, false).seen());
  // Bow-on: -30 % x 91 / 90, 4052 yd.
  AiSightingTarget bow{{0, 8 * kUnitsPerFoot, 4000 * kYard}, Angle10(1800)};
  CHECK(ai_detect_target({}, 0, nullptr, eye, bow, 1, false).seen());
  bow.position.z = 4100 * kYard;
  CHECK_FALSE(ai_detect_target({}, 0, nullptr, eye, bow, 1, false).seen());

  // The player boat below radar depth: a tenth (581 yd), no aspect term.
  PlayerLookoutState boat;
  boat.depth_ft = 60;
  boat.radar_depth_ft = 48;
  boat.bridge_eye_ft = 40;
  boat.awash_depth_ft = 30;
  AiSightingTarget scope{{0, 8 * kUnitsPerFoot, 550 * kYard}, Angle10(1800), &boat};
  const AiSighting seen = ai_detect_target({}, 0, nullptr, eye, scope, 1, false);
  CHECK(seen.distance_yd == 550);
  CHECK_FALSE(seen.aspect_applied);
  scope.position.z = 600 * kYard;
  CHECK_FALSE(ai_detect_target({}, 0, nullptr, eye, scope, 1, false).seen());
}

TEST_CASE("The SJ sees a destroyer at 10000 yd but not a sampan", "[sensors]") {
  SH_REQUIRE_GAME_EXE();
  MissionConditions env;
  env.options = 0x10080;  // SJ, 15000 yd; Limited Visibility.
  Simulation sim(env, kDeepSea);
  sim.sensors().environment().fog_pct = 100;  // The lookouts see nothing.
  PlayerSub& boat = sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 0));
  add_ship(sim, kagero(), {kOrigin, 0, kOrigin + 10000 * kYard}, Angle10(900), Fixed16(), ShipOrder::kNone);
  add_ship(sim, sampan(), {kOrigin + 10000 * kYard, 0, kOrigin}, Angle10(0), Fixed16(), ShipOrder::kNone);
  SimContext ctx = sim.context();
  RadarSets& radar = sim.sensors().radar();
  radar.init(ctx, boat, sim.sensors().environment());
  CHECK(radar.sj().mode == SjMode::kAScope);
  CHECK(reports(sim).empty());  // Switched on without a report.
  // The beam turns 40 deg/s: one sweep in 9 s.
  for (int i = 0; i < 50; ++i) {
    sim.simulate(Fixed16::from_raw(0x3334));
  }
  const auto& blips = radar.sj().blips;
  REQUIRE(blips.size() == 1);
  CHECK(blips[0].source == 1);
  CHECK(blips[0].range_yd == 10000);
  // 38800 / (4 x 10000 / 15000).
  CHECK(blips[0].strength == 14550);
  // The operator calls the bearing (relative, degrees).
  CHECK(has_report(reports(sim), 50, 0));
  CHECK(sj_fitted_range(0x20000) == 35000);
  CHECK(sd_fitted_range(0x40000) == 17600);
}

TEST_CASE("Escort sonar hears a submarine by its noise, speed and depth", "[sensors]") {
  MissionConditions env;
  env.thermal_layer_ft = 10000;
  Simulation sim(env, kDeepSea);
  Ship& escort = add_ship(sim, kagero(), {kOrigin, 0, kOrigin}, Angle10(0), Fixed16(), ShipOrder::kPatrol);
  // Noise 20 %: passive (5000 + 1000 x 2) x 20 % = 1400 yd.
  QuietSub& sub = sim.add(std::make_unique<QuietSub>(fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin + 1000 * kYard}, 20));
  SimContext ctx = sim.context();
  ai_ship_sonar_search(escort, ctx, 0x10000);
  CHECK(escort.ai().contact.vehicle == &sub);
  CHECK(escort.ai().contact.age == 0);
  // Strength (7000 - 1000) x 100 / 7000 = 85: a 60 yd error.
  CHECK(escort.ai().contact.position.x == kOrigin + 60 * kUnitsPerFoot);
  CHECK(escort.ai().contact.position.y == -100 * kUnitsPerFoot + 30);

  const auto heard_at = [&](std::int32_t yards, std::int32_t layer_ft, std::int32_t speed_tenths) {
    MissionConditions e;
    e.thermal_layer_ft = layer_ft;
    Simulation s(e, kDeepSea);
    Ship& dd = add_ship(s, kagero(), {kOrigin, 0, kOrigin}, Angle10(0), Fixed16(), ShipOrder::kPatrol);
    s.add(std::make_unique<QuietSub>(fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin + yards * kYard}, 20));
    dd.motion().speed_tenths = speed_tenths;
    SimContext c = s.context();
    ai_ship_sonar_search(dd, c, 0x10000);
    return dd.ai().contact.age == 0;
  };
  CHECK(heard_at(1350, 10000, 0));
  CHECK_FALSE(heard_at(1450, 10000, 0));
  // Under the thermal layer it is not heard.
  CHECK_FALSE(heard_at(1000, 80, 0));
  // At 10 kn the passive range halves (700 yd).
  CHECK(heard_at(650, 10000, 100));
  CHECK_FALSE(heard_at(750, 10000, 100));
  // Never right astern (170-190 deg).
  CHECK_FALSE(heard_at(-1000, 10000, 0));
}

TEST_CASE("The lookouts list a ship and the OOD calls it", "[sensors]") {
  SH_REQUIRE_GAME_EXE();
  Simulation sim(MissionConditions{}, kDeepSea);
  PlayerSub& boat = sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 0));
  Ship& ship = add_ship(sim, kagero(), {kOrigin + 5000 * kYard, 0, kOrigin}, Angle10(0), Fixed16(), ShipOrder::kNone);
  sim.simulate(Fixed16::from_raw(0x3334));
  const auto& contacts = sim.sensors().reports().contacts();
  REQUIRE(contacts.size() == 1);
  CHECK(contacts[0].vehicle == &ship);
  CHECK(contacts[0].kind == ContactKind::kShip);
  CHECK(contacts[0].range_yd == 5000);
  CHECK(contacts[0].relative_bearing == Angle10(900));
  CHECK(ship.sighted());
  CHECK(sim.sensors().reports().enemies_near());
  CHECK(has_report(reports(sim), 56, 90));
  // Not called again for 5 minutes.
  sim.simulate(Fixed16::from_raw(0x3334));
  CHECK(reports(sim).empty());
  CHECK(boat.side() != ship.side());
}

TEST_CASE("The hydrophones hear a ship's screws when submerged", "[sensors]") {
  SH_REQUIRE_GAME_EXE();
  Simulation sim(MissionConditions{}, kDeepSea);
  sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin}, Angle10(0), 0));
  Ship& ship = add_ship(sim, kagero(), {kOrigin, 0, kOrigin - 1000 * kYard}, Angle10(900), Fixed16::from_int(20),
                        ShipOrder::kPatrol);
  sim.step(10);
  CHECK(ship.ai().heard.heard);
  CHECK(ship.ai().heard.bearing == Angle10(1800));
  CHECK(has_report(reports(sim), 41, 180));
  sim.step(10);
  CHECK(reports(sim).empty());
}

TEST_CASE("Player noise follows the shaft power, quartered silent", "[sensors]") {
  Simulation sim(MissionConditions{}, kDeepSea);
  PlayerSub& boat =
      sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin}, Angle10(0), 0));
  CHECK(player_noise_level(boat) == 0);
  boat.motion().power = 2740 * 550 / 2;  // Half the motors' power as thrust.
  CHECK(player_noise_level(boat) == 50);
  boat.set_telegraph(PlayerSub::kTelegraphSilent);
  CHECK(player_noise_level(boat) == 12);
  WatcomRandom random(1);
  CHECK(init_thermal_layer(random, 0) == 10000);
  CHECK(init_thermal_layer(random, 2) / 100 == 2);
}

TEST_CASE("Ship sensor fields come from SHIPS.DAT", "[sensors]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const ShipClass* kagero_dat = tables.find_ship("KAGERO");
  REQUIRE(kagero_dat != nullptr);
  const ShipAiClass c = ShipAiClass::from(*kagero_dat);
  CHECK(c.has_sonar);
  CHECK(c.depth_charges == 36);
  CHECK(c.rack_charges == 2);
  CHECK(c.thrower_charges == 2);
  CHECK(c.lookout_ft == 25);
  CHECK(c.length_ft == 388);
  CHECK(c.visual_size == 100);
}

}  // namespace sh
