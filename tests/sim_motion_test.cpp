#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

#include "sim/geometry.h"
#include "sim/motion.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim_classes.h"

namespace sh {

using namespace geom;

namespace {

const FlatSeaFloor kDeepSea(-2000 * kUnitsPerFoot);
const Fixed16 kStep = Fixed16::from_raw(0x3334);  // The ships' 0.2 s sub-step.

// A ship block heading north at `speed` knots with `power` (16.16 "hp").
MotionState ship_block(double speed_kts, std::int32_t power) {
  MotionState m;
  m.position = {1000000, 0, 1000000};
  m.speed = Fixed16::from_double(speed_kts);
  m.power = power;
  m.ordered_power = power;
  motion_init(m, kDeepSea);
  return m;
}

}  // namespace

TEST_CASE("Ship classes top out at their listed speed", "[sim][motion]") {
  // docs/structures.md: solving the drag model for max_power gives the listed top
  // speed within 0.63 kn on the surface classes (FUSO 21.9 for "22.5").
  struct Case {
    ShipMotionParams cls;
    double listed;
  };
  for (const Case& c : {Case{test::fuso(), 22.5}, Case{test::yamato(), 27.0}, Case{test::fubuki(), 38.0},
                        Case{test::lfreight(), 10.0}}) {
    const Fixed16 top = motion_speed_for_power(c.cls.max_power, c.cls.hull, 0, 100);
    CHECK(top.to_double() == Catch::Approx(c.listed).margin(0.63));
    CHECK(motion_power_for_speed(top, c.cls.max_power, c.cls.hull, 0, 100) == c.cls.max_power);
  }
  CHECK(motion_speed_for_power(test::fuso().max_power, test::fuso().hull, 0, 100).to_double() ==
        Catch::Approx(21.9).margin(0.06));
}

TEST_CASE("A ship accelerates from rest to the speed its power sustains", "[sim][motion]") {
  const ShipMotionParams cls = test::fubuki();
  MotionState m = ship_block(0.0, 0);
  m.ordered_power = cls.max_power;
  const double top = motion_speed_for_power(cls.max_power, cls.hull, 0, 100).to_double();
  double speed_at_1min = 0;
  for (int i = 0; i < 5 * 60 * 30; ++i) {  // 30 minutes.
    motion_step(m, cls.hull, 0, kStep, 100, kDeepSea);
    if (i == 5 * 60) {
      speed_at_1min = m.speed.to_double();
    }
  }
  CHECK(speed_at_1min > 5.0);
  CHECK(speed_at_1min < top);
  CHECK(m.speed.to_double() == Catch::Approx(top).margin(0.2));
  CHECK(m.power == fx::mul(cls.max_power, 550 << 16));
  CHECK(m.heading.tenths() == 0);
  // Heading north: only z moved, and the log counted the miles run.
  CHECK(m.position.x == 1000000);
  CHECK(m.log_miles > 10);
  CHECK(m.speed_tenths == static_cast<std::int32_t>(m.speed.to_double() * 10));
}

TEST_CASE("Rough seas slow a ship down", "[sim][motion]") {
  const ShipMotionParams cls = test::fuso();
  const double calm = motion_speed_for_power(cls.max_power, cls.hull, 0, 100).to_double();
  const double sea1 = motion_speed_for_power(cls.max_power, cls.hull, 1, 100).to_double();
  const double sea2 = motion_speed_for_power(cls.max_power, cls.hull, 2, 100).to_double();
  CHECK(sea1 < calm);
  CHECK(sea2 < sea1);
  // Sea state 2 counts as 3: drag x (1 + 3).
  const Fixed16 v = Fixed16::from_int(10);
  CHECK(motion_power_for_speed(v, 100000, cls.hull, 2, 100) ==
        Catch::Approx(4 * motion_power_for_speed(v, 100000, cls.hull, 0, 100)).margin(4));
  CHECK(motion_power_for_speed(v, 100000, cls.hull, 1, 50) ==
        Catch::Approx(1.5 * motion_power_for_speed(v, 100000, cls.hull, 0, 100)).margin(4));
  // Astern the drag is multiplied by astern_drag_factor (25.5 for FUSO).
  const std::int32_t ahead = motion_power_for_speed(Fixed16::from_int(5), 100000, cls.hull, 0, 100);
  const std::int32_t astern = motion_power_for_speed(Fixed16::from_int(-5), 100000, cls.hull, 0, 100);
  CHECK(static_cast<double>(astern) / -ahead == Catch::Approx(25.5).epsilon(0.02));
}

TEST_CASE("The rudder yaws the hull at speed x sin(rudder) over half length x turn factor", "[sim][motion]") {
  const ShipMotionParams cls = test::fubuki();
  const double speed = motion_speed_for_power(cls.max_power, cls.hull, 0, 100).to_double();
  MotionState m = ship_block(speed, cls.max_power);
  m.rudder = 250;
  const std::int32_t before = m.heading_rad;
  motion_step(m, cls.hull, 0, kStep, 100, kDeepSea);
  const double v = speed * 27.008;  // Units per second.
  const double expected =
      v * std::sin(25.0 * std::numbers::pi / 180) * 0.2 / (cls.hull.length_ft / 2.0 * cls.hull.turn_factor.to_double());
  CHECK((m.heading_rad - before) / 65536.0 == Catch::Approx(expected).epsilon(0.01));

  // Held hard over, the ship comes round a full circle and the bigger,
  // slower-turning class needs much more room.
  const auto circle_diameter_yd = [](const ShipMotionParams& c) {
    const double s = motion_speed_for_power(c.max_power, c.hull, 0, 100).to_double();
    MotionState b = ship_block(s, c.max_power);
    b.rudder = 250;
    std::int32_t max_x = b.position.x;
    std::int32_t turned = 0;
    Angle10 last = b.heading;
    while (turned < 3600) {
      motion_step(b, c.hull, 0, kStep, 100, kDeepSea);
      turned += fx::angle_sub(b.heading, last).tenths();
      last = b.heading;
      max_x = std::max(max_x, b.position.x);
    }
    return (max_x - 1000000) / 48.0;
  };
  const double dd = circle_diameter_yd(test::fubuki());
  const double bb = circle_diameter_yd(test::yamato());
  CHECK(dd > 300);
  CHECK(bb > 2 * dd);
}

TEST_CASE("Steering turns to the ordered heading the short way and settles", "[sim][motion]") {
  const ShipMotionParams cls = test::fubuki();
  MotionState m = ship_block(20.0, motion_power_for_speed(Fixed16::from_int(20), cls.max_power, cls.hull, 0, 100));
  m.steering = true;
  m.ordered_heading = Angle10(3000);  // 60 deg to port: full rudder left.
  motion_step(m, cls.hull, 0, kStep, 100, kDeepSea);
  CHECK(m.rudder == -250);
  m.ordered_heading = Angle10(150);  // 15 deg to starboard: 250 x 15/60.
  motion_step(m, cls.hull, 0, kStep, 100, kDeepSea);
  CHECK(m.rudder == 250 * fx::angle_diff(Angle10(150), m.heading) / 600);
  for (int i = 0; i < 5 * 120; ++i) {
    motion_step(m, cls.hull, 0, kStep, 100, kDeepSea);
  }
  CHECK(std::abs(fx::angle_diff(m.heading, Angle10(150))) <= 5);
}

TEST_CASE("Route are reached within turn_factor x 34 x angle off / 180 deg + 100 yd", "[sim][motion]") {
  // FUBUKI turn factor 29.2 -> 29: radius 986 x off / 1800 + 100 yd.
  const Fixed16 tf = test::fubuki().hull.turn_factor;
  MotionState m;
  m.position = {480000, 0, 480000};
  Route route;
  route.count = 2;
  route.x[0] = 480000;
  route.z[0] = 480000 + 99 * 48;
  route.x[1] = 480000 + 593 * 48 - 1;
  route.z[1] = 480000;
  route.x[2] = -1;  // Loop back to the first point.
  std::int32_t index = 0;
  motion_steer_to_waypoint(m, tf, route, index);
  CHECK(m.steering);
  CHECK(m.ordered_heading.tenths() == 0);
  CHECK(index == 1);  // Dead ahead: within 100 yd.
  motion_steer_to_waypoint(m, tf, route, index);
  CHECK(m.ordered_heading.tenths() == 900);
  CHECK(index == 0);  // 90 deg off: within 593 yd; the -1 sentinel loops.

  route.x[1] = 480000 + 594 * 48;
  index = 1;
  motion_steer_to_waypoint(m, tf, route, index);
  CHECK(index == 1);
  route.x[2] = 0;
  route.x[1] = 480000 + 100;
  motion_steer_to_waypoint(m, tf, route, index);
  CHECK(index == 2);  // The end of a route that does not loop.
  motion_steer_to_waypoint(m, tf, route, index);
  CHECK(index == 2);
}

TEST_CASE("A hull touching the bottom stops", "[sim][motion]") {
  const ShipMotionParams cls = test::fubuki();
  const FlatSeaFloor shoal(-5 * kUnitsPerFoot);  // 5 ft of water for a 10 ft draft.
  MotionState m = ship_block(20.0, cls.max_power);
  motion_step(m, cls.hull, 0, kStep, 100, shoal);
  CHECK(m.keel_clearance == 5 * kUnitsPerFoot);
  // The next step stops the hull (the speed reads the velocity before it).
  motion_step(m, cls.hull, 0, kStep, 100, shoal);
  const fx::Vec3 at = m.position;
  motion_step(m, cls.hull, 0, kStep, 100, shoal);
  CHECK(m.speed.raw() == 0);
  for (int i = 0; i < 50; ++i) {
    motion_step(m, cls.hull, 0, kStep, 100, shoal);
  }
  CHECK(m.position == at);
}

TEST_CASE("Submarine wave effect fades with depth", "[sim][motion]") {
  CHECK(sub_wave_effect_pct(0, 10, 40) == 0);
  CHECK(sub_wave_effect_pct(1, 30, 40) == 100);
  CHECK(sub_wave_effect_pct(1, 60, 40) == 50);
  CHECK(sub_wave_effect_pct(2, 80, 40) == 0);
}

TEST_CASE("Ships init at their ordered speed and allow 64x until sinking", "[sim][motion]") {
  MissionConditions env;
  Simulation sim(env, kDeepSea);
  Ship& ship = sim.add(std::make_unique<Ship>(test::fuso(), fx::Vec3{1000000, 0, 1000000}, Angle10(900),
                                              Fixed16::from_int(30), ShipOrder::kPatrol));
  // 30 kn is beyond FUSO: the power is capped and the speed is its top.
  CHECK(ship.motion().ordered_power == 9000);
  CHECK(ship.motion().speed.to_double() == Catch::Approx(21.9).margin(0.06));
  CHECK(ship.max_time_compression() == 64);
  sim.simulate(Fixed16::from_int(60));
  CHECK(ship.position().x == Catch::Approx(1000000 + ship.motion().speed.to_double() * 27.008 * 60).epsilon(0.01));
  ship.set_sinking();
  CHECK(ship.max_time_compression() == kUnrestrictedCompression);
}

}  // namespace sh
