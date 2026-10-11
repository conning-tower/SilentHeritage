#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "game_data.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"
#include "sim_classes.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr fx::Vec3 kSurfaced{2000000, 0, 2000000};
constexpr fx::Vec3 kAt100ft{2000000, -100 * kUnitsPerFoot, 2000000};

// A mission with a Gato at `at`.
struct Mission {
  explicit Mission(fx::Vec3 at, std::int32_t telegraph, MissionConditions env = {})
      : sim(env, kDeepSea), boat(sim.add(std::make_unique<PlayerSub>(test::gato(), at, Angle10(0), telegraph))) {}

  // Runs `seconds` of game time in 0.2 s player sub-steps.
  void run(double seconds) {
    for (int i = 0; i < static_cast<int>(seconds * 5); ++i) {
      sim.simulate(Fixed16::from_raw(0x3334));
    }
  }

  Simulation sim;
  PlayerSub& boat;
};

}  // namespace

TEST_CASE("The telegraph spreads its power over the engines", "[sim][player]") {
  Mission m(kSurfaced, 5);
  CHECK(m.boat.diesel_mode());
  for (const EngineRoom& room : m.boat.engines()) {
    CHECK(room[1].power_pct == 100);
    CHECK(room[1].flags == EnginePart::kRunning);
  }
  CHECK(m.boat.shaft_power() == 5400);
  // The speed the class's diesels sustain: SUBS.DAT lists 20.75 kn (the
  // 0.1 kn search of motion_speed_for_power stops just above).
  CHECK(m.boat.motion().speed.to_double() == Catch::Approx(20.75).margin(0.2));

  m.boat.set_telegraph(1);  // Ahead 1/3: 40 % in all, on the first diesel.
  m.run(0.2);
  CHECK(m.boat.engines()[0][1].power_pct == 40);
  CHECK(m.boat.engines()[1][1].flags == 0);
  CHECK(m.boat.shaft_power() == 540);

  m.boat.set_telegraph(6);  // Back emergency: -372 %, 95 % per unit first.
  m.run(0.2);
  CHECK(m.boat.engines()[0][1].power_pct == -95);
  CHECK(m.boat.engines()[3][1].power_pct == -87);
  CHECK(m.boat.shaft_power() == -(3 * 1282 + 1174));
}

TEST_CASE("Submerged the motors drive the boat", "[sim][player]") {
  Mission m(kAt100ft, 3);
  CHECK_FALSE(m.boat.diesel_mode());
  CHECK(m.boat.depth_ft() == 100);
  CHECK(m.boat.engines()[0][0].power_pct == 95);
  CHECK(m.boat.engines()[1][0].power_pct == 95);
  CHECK(m.boat.engines()[2][0].flags == 0);
  CHECK(m.boat.shaft_power() == 2 * (95 * 685 / 100));

  Mission flank(kAt100ft, 5);
  // SUBS.DAT lists 8.75 kn submerged.
  CHECK(flank.boat.motion().speed.to_double() == Catch::Approx(8.75).margin(0.15));
}

TEST_CASE("The boat dives to the ordered depth and holds it", "[sim][player]") {
  Mission m(kSurfaced, 3);
  m.run(60);
  SimContext ctx = m.sim.context();
  m.boat.order_dive(ctx);
  CHECK(m.boat.telegraph() == 5);
  CHECK(m.boat.dive().ordered_depth_ft == 200);
  double reached_65 = -1;
  for (int s = 0; s < 1200 && reached_65 < 0; ++s) {
    m.run(1);
    if (m.boat.depth_ft() >= 65) {
      reached_65 = s + 1;
    }
  }
  CHECK_FALSE(m.boat.diesel_mode());
  // A crash dive at flank reaches periscope depth in about half a minute.
  CHECK(reached_65 > 15);
  CHECK(reached_65 < 60);
  m.run(1200);
  CHECK(m.boat.depth_ft() == Catch::Approx(200).margin(3));
  CHECK(m.boat.motion().pitch.tenths() == 0);

  m.boat.order_surface(ctx);
  m.run(1800);
  CHECK(m.boat.depth_ft() == 15);
  CHECK(m.boat.diesel_mode());
  CHECK(m.boat.dive().ballast == 1825 * 100);
}

TEST_CASE("Fuel burns with diesel power and the battery with motor power squared", "[sim][player]") {
  Mission diesel(kSurfaced, 5);
  diesel.run(1000);
  // Full power: SUBS.DAT +272 = 7209 / 65536 per second.
  CHECK(diesel.boat.fuel() == Catch::Approx(117000 - 1000 * 7209 / 65536.0).margin(1));

  Mission motors(kAt100ft, 5);
  motors.run(1000);
  // (power / motor hp)^2 x 0.1467 per second.
  CHECK(motors.boat.battery() == Catch::Approx(600 - 1000 * 0x258e / 65536.0).margin(1));
  CHECK(motors.boat.fuel() == 117000);

  MissionConditions unlimited;
  unlimited.options = option::kUnlimitedBattery | option::kUnlimitedFuel;
  Mission free(kAt100ft, 5, unlimited);
  free.run(1000);
  CHECK(free.boat.battery() == 600);
}

TEST_CASE("Idle diesels recharge the battery on the surface", "[sim][player]") {
  Mission m(kSurfaced, 0);
  m.boat.set_battery(300);
  m.run(1000);
  // A tenth of the deficit per 600 units per second: 600 - 300 e^(-t / 6000).
  CHECK(m.boat.battery() == Catch::Approx(600 - 300 * std::exp(-1000.0 / 6000)).margin(3));
  int charging = 0;
  for (const EngineRoom& room : m.boat.engines()) {
    charging += (room[1].flags & EnginePart::kCharging) != 0 ? 1 : 0;
  }
  CHECK(charging == 2);  // 0.04 x 65536 / 2405 per diesel.
}

TEST_CASE("Air lasts about a day submerged", "[sim][player]") {
  Mission m(kAt100ft, 1);
  const std::int32_t full = m.boat.air();
  CHECK(full >= 1000);
  CHECK(full < 1333);
  // 0x2f6 / 65536 per second (0x2f8 per s after the 0.2 s rounding); the
  // first step borrows a whole unit.
  m.run(0.2);
  CHECK(m.boat.air() == full - 1);
  m.run(859.8);
  CHECK(m.boat.air() == full - 10);
  m.run(10);
  CHECK(m.boat.air() == full - 11);
}

TEST_CASE("The helm sets the rudder, capped by steering damage", "[sim][player]") {
  Mission m(kSurfaced, 3);
  m.boat.set_helm(3);
  m.run(0.2);
  CHECK(m.boat.motion().rudder == 150);
  m.boat.set_helm(-7);
  m.run(0.2);
  CHECK(m.boat.motion().rudder == -350);
  CHECK(m.boat.helm() == -7);
  MissionEvents events = m.sim.take_events();
  CHECK(events.crew_messages == std::vector<CrewReport>{{8, 15}, {9, 35}});

  m.boat.systems()[static_cast<std::size_t>(PlayerSystem::kSteeringGear)] = {60, 1};
  m.run(0.2);
  CHECK(m.boat.motion().rudder == -80);
  CHECK(m.boat.helm() == -2);  // Shown from the rudder: -8 deg is the -10 step.
}

TEST_CASE("The boat follows its route and stops at the last waypoint", "[sim][player]") {
  Mission m(kSurfaced, 3);
  Route route;
  route.count = 2;
  route.x[0] = kSurfaced.x;
  route.z[0] = kSurfaced.z + 2000 * kUnitsPerYard;
  route.x[1] = kSurfaced.x + 2000 * kUnitsPerYard;
  route.z[1] = kSurfaced.z + 2000 * kUnitsPerYard;
  m.boat.follow_route(route);
  m.run(3600);
  CHECK_FALSE(m.boat.following_route());
  CHECK(m.boat.telegraph() == 0);
  CHECK(m.boat.route_index() == 2);
  const MissionEvents events = m.sim.take_events();
  CHECK(std::count(events.crew_messages.begin(), events.crew_messages.end(), CrewReport{55, -1}) == 1);
  CHECK(m.boat.position().x > kSurfaced.x + 1000 * kUnitsPerYard);
}

TEST_CASE("At the map edge the boat turns back", "[sim][player]") {
  MissionConditions env;
  env.map = MapExtent{0x200000, 0x200000};
  Mission m({0x200000 - 0x70000, 0, 0x100000}, 3, env);
  m.run(0.2);
  CHECK(m.boat.motion().steering);
  CHECK(m.boat.motion().ordered_heading.tenths() == 2700);
}

}  // namespace sh
