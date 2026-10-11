// The simulation's corrections of the original's errors under the extended
// variant's mission rules (`MissionRules`, sim/context.h): each with the
// rules on, and with the original's rules for the unchanged behaviour.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "core/fixed_math.h"
#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/aircraft.h"
#include "sim/effects.h"
#include "sim/enemy_sub.h"
#include "sim/player_combat.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim_classes.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;

const MissionRules kOriginal{};
const MissionRules kFixed = MissionRules::extended();

MissionConditions conditions(const MissionRules& rules) {
  MissionConditions c;
  c.rules = rules;
  return c;
}

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

// A vehicle of fixed noise, a submarine or not.
class Noise : public Vehicle {
 public:
  Noise(fx::Vec3 position, std::int32_t noise, bool submarine)
      : position_(position), noise_(noise), submarine_(submarine) {}
  VehicleClass class_id() const override { return submarine_ ? VehicleClass::kEnemySub : VehicleClass::kShip; }
  void init(SimContext&) override {}
  std::int32_t update_pass() const override { return 1; }
  void update(SimContext&, Fixed16) override {}
  fx::Vec3 position() const override { return position_; }
  bool is_submarine() const override { return submarine_; }
  std::int32_t noise_level() const override { return noise_; }

 private:
  fx::Vec3 position_;
  std::int32_t noise_;
  bool submarine_;
};

// An alerted escort heading north with a submerged submarine 1000 yd
// ahead. The escort is 4000 yd south of (kOrigin, kOrigin), where the
// original's bearing slip (x - x, x - z) gives the true bearing of a point.
constexpr std::int32_t kEscortZ = kOrigin - 4000 * kYard;
struct Hunt {
  explicit Hunt(const MissionRules& rules) : sim(conditions(rules), kDeepSea) {
    auto ship = std::make_unique<Ship>(test::fubuki(), fx::Vec3{kOrigin, 0, kEscortZ}, Angle10(0), Fixed16(),
                                       ShipOrder::kPatrol);
    ship->ai().cls = destroyer();
    ship->setup().crew = 1;
    escort = &sim.add(std::move(ship));
    sub = &sim.add(
        std::make_unique<Noise>(fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kEscortZ + 1000 * kYard}, 50, true));
    escort->set_alert(1);
  }

  // Runs up to `steps` steps; returns the step the escort first held the
  // submarine as its contact, or -1.
  int contact_step(int steps) {
    for (int i = 0; i < steps; ++i) {
      sim.step(10);
      if (escort->ai().contact.vehicle == sub) {
        return i;
      }
    }
    return -1;
  }

  Simulation sim;
  Ship* escort = nullptr;
  Vehicle* sub = nullptr;
};

}  // namespace

TEST_CASE("Depth-charge blasts deafen the escorts only with the corrections", "[rules][ai]") {
  Hunt plain(kOriginal);
  const int found = plain.contact_step(3000);
  REQUIRE(found >= 0);
  for (const MissionRules& rules : {kOriginal, kFixed, MissionRules{.fix_obvious_errors = true, .keen_escort_sonar = true}}) {
    Hunt hunt(rules);
    // A blast 2000 yd astern: near enough to deafen the escort (2400 yd),
    // far from the contact and out of the sonar's beam.
    hunt.sim.add(
        std::make_unique<UnderwaterBlast>(fx::Vec3{kOrigin, -50 * kUnitsPerFoot, kEscortZ - 2000 * kYard}));
    const int step = hunt.contact_step(found + 1);
    if (rules.escorts_deafened()) {
      // Deaf while the blast is heard (180 s).
      CHECK(step == -1);
    } else {
      // The original, and KEEN ESCORT SONAR: the blast changes nothing.
      CHECK(step == found);
    }
  }
}

TEST_CASE("A noise cancels a sonar contact by the true geometry only with the corrections", "[rules][ai]") {
  // A ship at (kOrigin, kEscortZ) heading north, its beam ahead, its
  // estimate 1000 yd ahead.
  const fx::Vec3 ship{kOrigin, 0, kEscortZ};
  const fx::Vec3 estimate{kOrigin, -1600, kEscortZ + 1000 * kYard};
  const auto cancels = [&](const fx::Vec3& p, const MissionRules& rules) {
    return sonar_noise_cancels(ship, Angle10(0), 0, estimate, p, 100, rules);
  };
  // Within 800 yd of the estimate: both.
  const fx::Vec3 near{kOrigin + 500 * kYard, 0, kEscortZ + 1000 * kYard};
  CHECK(cancels(near, kOriginal));
  CHECK(cancels(near, kFixed));
  // In the beam 3000 yd beyond the estimate: the original compares with
  // the estimate's range from the map's origin, so it cancels.
  const fx::Vec3 beyond{kOrigin, 0, kEscortZ + 4000 * kYard};
  CHECK(cancels(beyond, kOriginal));
  CHECK_FALSE(cancels(beyond, kFixed));
  // Abeam, out of the beam with the true bearing.
  const fx::Vec3 abeam{kOrigin + 2000 * kYard, 0, kEscortZ};
  CHECK_FALSE(cancels(abeam, kFixed));
  // Behind the ship: not in the beam for either.
  const fx::Vec3 astern{kOrigin, 0, kEscortZ - 3000 * kYard};
  CHECK_FALSE(cancels(astern, kFixed));
  // A nearer noise in the beam cancels with the corrections too.
  const fx::Vec3 short_of{kOrigin, 0, kEscortZ + 100 * kYard};
  CHECK(cancels(short_of, kFixed));
}

TEST_CASE("Route pruning drops the failing point only with the corrections", "[rules][ai]") {
  // Shallow water (10 ft) west of x = kOrigin, deep elsewhere.
  class Shelf : public SeaFloor {
   public:
    std::int32_t height_at(std::int32_t x, std::int32_t) const override {
      return x < kOrigin ? -10 * kUnitsPerFoot : -3000 * kUnitsPerFoot;
    }
  } shelf;
  const std::array<std::int32_t, 4> xs{kOrigin - 1000 * kYard, kOrigin + 1000 * kYard, kOrigin + 2000 * kYard,
                                       kOrigin + 3000 * kYard};
  for (const MissionRules& rules : {kOriginal, kFixed}) {
    Simulation sim(conditions(rules), shelf);
    auto ship = std::make_unique<Ship>(test::lfreight(), fx::Vec3{xs[1], 0, kOrigin}, Angle10(900),
                                       Fixed16::from_int(8), ShipOrder::kFollowPath);
    ship->ai().cls.draft_ft = 15;
    Route& route = ship->setup().route;
    route.count = 4;
    route.x = {xs[0], xs[1], xs[2], xs[3], -2};
    route.z = {kOrigin, kOrigin, kOrigin, kOrigin};
    const Ship& s = sim.add(std::move(ship));
    REQUIRE(s.setup().route.count == 3);
    if (rules.fix_obvious_errors) {
      // The shallow first point goes.
      CHECK(s.setup().route.x[0] == xs[1]);
      CHECK(s.setup().route.x[1] == xs[2]);
    } else {
      // The original drops the next point and keeps the shallow one.
      CHECK(s.setup().route.x[0] == xs[0]);
      CHECK(s.setup().route.x[1] == xs[2]);
    }
    CHECK(s.setup().route.x[2] == xs[3]);
  }
}

TEST_CASE("An enemy sub's depth stays above a shallow bottom only with the corrections", "[rules][sub]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const FlatSeaFloor shallow(-100 * kUnitsPerFoot);
  for (const MissionRules& rules : {kOriginal, kFixed}) {
    Simulation sim(conditions(rules), shallow);
    const ShipClass& cls = *tables.find_ship("KAIDAI TYPE");
    auto owned = std::make_unique<EnemySub>(fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), Fixed16::from_int(5),
                                            ShipOrder::kNone, 1);
    owned->setup().ship_class = &cls;
    owned->setup().hit_points = static_cast<std::int32_t>(cls.hull_strength);
    EnemySub& sub = sim.add(std::move(owned));
    sub.dive().ordered_depth_ft = 300;
    sim.step(10);
    // 20 ft above the bottom: 80 ft, or the floor's height in world units
    // for the original.
    const std::int32_t floor_y = -100 * kUnitsPerFoot - 0x140;
    CHECK(sub.dive().ordered_depth_ft == (rules.fix_obvious_errors ? -floor_y / kUnitsPerFoot : floor_y));
  }
}

TEST_CASE("The AA guns' elevation runs over the horizontal range only with the corrections", "[rules][player]") {
  // An aircraft 500 ft up, 1500 yd north and 10 ft east.
  const std::int32_t dx = 10 * kUnitsPerFoot;
  const std::int32_t dy = 500 * kUnitsPerFoot;
  const std::int32_t dz = 1500 * kYard;
  CHECK(aa_gun_elevation(dx, dy, dz, kOriginal) == fx::heading_from_delta(dy, dx));
  CHECK(aa_gun_elevation(dx, dy, dz, kFixed) ==
        fx::heading_from_delta(dy, static_cast<std::int32_t>(fx::approx_distance(dx, dz))));
  CHECK(aa_gun_elevation(dx, dy, dz, kOriginal).tenths() > 800);
  CHECK(aa_gun_elevation(dx, dy, dz, kFixed).tenths() < 100);
  // Due north the original uses |dz| too.
  CHECK(aa_gun_elevation(0, dy, dz, kOriginal) == aa_gun_elevation(0, dy, dz, kFixed));

  // Above 1.9 degrees; a target below wraps high for the original.
  CHECK(aa_guns_fire_at(Angle10(20), kOriginal));
  CHECK(aa_guns_fire_at(Angle10(20), kFixed));
  CHECK_FALSE(aa_guns_fire_at(Angle10(19), kFixed));
  CHECK(aa_guns_fire_at(Angle10(3500), kOriginal));
  CHECK_FALSE(aa_guns_fire_at(Angle10(3500), kFixed));
}

TEST_CASE("A strafing run folds the contact's bearing by 3600 only with the corrections", "[rules][air]") {
  CHECK(strafe_bearing(900, kOriginal) == 900);
  CHECK(strafe_bearing(900, kFixed) == 900);
  CHECK(strafe_bearing(3500, kOriginal) == 2600);
  CHECK(strafe_bearing(3500, kFixed) == -100);
}

}  // namespace sh
