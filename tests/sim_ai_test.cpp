#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim_classes.h"
#include "sim_test_grid.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;

ShipAiClass destroyer() {
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

ShipAiClass freighter() {
  ShipAiClass c;
  c.type = static_cast<ShipType>(14);
  c.role = ShipRole::kMerchant;
  c.length_ft = 375;
  c.draft_ft = 15;
  c.visual_size = 110;
  c.lookout_ft = 45;
  return c;
}

Ship& add_ship(Simulation& sim, const ShipAiClass& cls, fx::Vec3 at, Angle10 heading, Fixed16 speed,
               ShipOrder orders, std::int32_t crew = 1, std::int32_t group = 0) {
  auto ship = std::make_unique<Ship>(cls.role == ShipRole::kEscort ? test::fubuki() : test::lfreight(), at, heading,
                                     speed, orders);
  ship->ai().cls = cls;
  ship->setup().crew = crew;
  ship->setup().convoy_group = group;
  return sim.add(std::move(ship));
}

// A submerged target of fixed noise.
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

// Records the patterns the AI drops.
struct PatternLog : ShipWeaponsHook {
  void drop_depth_charges(Ship& ship, SimContext&, std::int32_t depth_ft) override {
    depths.push_back(depth_ft);
    ship.ai().depth_charges -= ship.ai().cls.rack_charges + ship.ai().cls.thrower_charges;
    ship.ai().charges_dropped += ship.ai().cls.rack_charges;
  }
  std::vector<std::int32_t> depths;
};

}  // namespace

TEST_CASE("Convoy members keep station on their leader", "[ai]") {
  Simulation sim(MissionConditions{}, kDeepSea);
  Ship& leader = add_ship(sim, freighter(), {kOrigin, 0, kOrigin}, Angle10(0), Fixed16::from_int(8),
                          ShipOrder::kConvoyLeader, 1, 5);
  Ship& member = add_ship(sim, freighter(), {kOrigin + 1000 * kYard, 0, kOrigin}, Angle10(0), Fixed16::from_int(8),
                          ShipOrder::kConvoyMember, 1, 5);
  SimContext ctx = sim.context();
  convoy_regroup_on_leader(ctx, 5);
  CHECK(member.ai().leader == &leader);
  CHECK(convoy_find_leader(sim.vehicles(), 5) == &leader);
  leader.motion().ordered_heading = Angle10(450);
  leader.motion().steering = true;
  for (int i = 0; i < 300; ++i) {
    sim.simulate(Fixed16::from_raw(0x3334));
  }
  // A minute at 8 kn and a turn to 045: the member moved rigidly with it.
  CHECK(leader.position().z - kOrigin > 250 * kYard);
  CHECK(member.position().x - leader.position().x == 1000 * kYard);
  CHECK(member.position().z - leader.position().z == 0);
  CHECK(member.motion().heading == leader.motion().heading);
  CHECK(member.motion().speed == leader.motion().speed);
}

TEST_CASE("An alerted escort hunts a sonar contact and drops a pattern", "[ai]") {
  Simulation sim(MissionConditions{}, kDeepSea);
  PatternLog log;
  sim.sensors().environment().weapons = &log;
  Ship& escort = add_ship(sim, destroyer(), {kOrigin, 0, kOrigin}, Angle10(0), Fixed16(), ShipOrder::kPatrol);
  QuietSub& sub =
      sim.add(std::make_unique<QuietSub>(fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin + 1000 * kYard}, 50));
  escort.set_alert(1);
  std::vector<ShipAiMode> modes;
  for (int i = 0; i < 3000 && log.depths.empty(); ++i) {
    sim.step(10);
    if (modes.empty() || modes.back() != escort.ai().mode) {
      modes.push_back(escort.ai().mode);
    }
  }
  CHECK(escort.ai().sonar.active);
  CHECK(escort.ai().contact.vehicle == &sub);
  // Sonar contact: to the datum at 17 kn, then the attack run.
  const std::vector<ShipAiMode> expected{ShipAiMode::kCruise, ShipAiMode::kInvestigate, ShipAiMode::kInvestigating,
                                         ShipAiMode::kAttackStart, ShipAiMode::kAttackRun, ShipAiMode::kOpening};
  CHECK(modes == expected);
  REQUIRE(log.depths.size() == 1);
  // Set to the estimated depth: 100 ft less half the position error.
  CHECK(log.depths[0] > 90);
  CHECK(log.depths[0] <= 100);
  CHECK(escort.ai().depth_charges == 32);
  CHECK(escort.dropping_charges());
}

TEST_CASE("The convoy leader zigzags in timed legs", "[ai]") {
  SH_REQUIRE_GAME_EXE();
  Simulation sim(MissionConditions{}, kDeepSea);
  auto ship = std::make_unique<Ship>(test::lfreight(), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0),
                                     Fixed16::from_int(8), ShipOrder::kConvoyLeader);
  ship->ai().cls = freighter();
  ship->setup().crew = 1;
  ship->setup().convoy_group = 3;
  ship->setup().zigzag = 1;
  ship->setup().route.count = 2;
  ship->setup().route.x = {kOrigin, kOrigin};
  ship->setup().route.z = {kOrigin, kOrigin + 60000 * kYard};
  Ship& leader = sim.add(std::move(ship));
  SimContext ctx = sim.context();
  convoy_regroup_on_leader(ctx, 3);
  const Angle10 base = leader.ai().zigzag.base_course;

  // Runs until the zigzag leg changes; returns the seconds it took.
  const auto next_leg = [&] {
    const std::int32_t leg = leader.ai().zigzag.leg;
    int steps = 0;
    while (leader.ai().zigzag.leg == leg && steps < 100000) {
      sim.step(10);
      ++steps;
    }
    return steps / 10.0;
  };
  next_leg();
  REQUIRE(leader.ai().zigzag.leg == 1);
  const std::int32_t r = leader.ai().zigzag.random;
  CHECK(r < 20);
  // Leg 1 of the plan: 10 minutes (+ r / 5) at +20 deg (+ r) from the base.
  const std::int32_t minutes = r / 5 + 10;
  CHECK(leader.motion().ordered_heading == Angle10((base.tenths() + (20 + r) * 10) % 3600));
  const double took = next_leg();
  CHECK(took >= minutes * 60);
  CHECK(took <= minutes * 60 + 1.7);  // Thinking every 1.6 s at 1x.
  // Leg 2: 17 minutes at -35 deg (`angle_add` leaves a negative sum as is).
  std::int32_t leg2 = base.tenths() + (r - 35) * 10;
  while (leg2 >= 3600) {
    leg2 -= 3600;
  }
  CHECK(leader.motion().ordered_heading == Angle10(leg2));
  CHECK(leader.ai().zigzag.timer <= (r / 5 + 17) * 0x3c0000);
  CHECK(leader.ai().zigzag.timer > (r / 5 + 17) * 0x3c0000 - 0x20000);
}

TEST_CASE("Route points behind land are pruned at spawn", "[ai]") {
  test::TestGrid grid(12, 12);
  grid.set(5, 6, 0x22);
  Simulation sim(MissionConditions{}, kDeepSea);
  sim.sensors().environment().grid = &grid;
  auto ship = std::make_unique<Ship>(test::lfreight(), fx::Vec3{grid.x_of(1), 0, grid.z_of(6)}, Angle10(900),
                                     Fixed16::from_int(8), ShipOrder::kFollowPath);
  ship->ai().cls = freighter();
  Route& route = ship->setup().route;
  route.count = 4;
  route.x = {grid.x_of(1), grid.x_of(8), grid.x_of(8), grid.x_of(1), -2};
  route.z = {grid.z_of(6), grid.z_of(6), grid.z_of(2), grid.z_of(2)};
  const Ship& s = sim.add(std::move(ship));
  // The leg 0 -> 1 crosses land: point 1 goes, and the check moves on to
  // the old point 3's leg, which is clear.
  CHECK(s.setup().route.count == 3);
  CHECK(s.setup().route.x[1] == grid.x_of(8));
  CHECK(s.setup().route.z[1] == grid.z_of(2));
  CHECK(s.setup().route.x[2] == grid.x_of(1));
  CHECK(s.setup().route.x[3] == -2);
}

}  // namespace sh
