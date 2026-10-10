#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "assets/dat_tables.h"
#include "game/class_params.h"
#include "game_data.h"
#include "sim/depth_charge.h"
#include "sim/player_combat.h"
#include "sim/player_damage.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/torpedo.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kOrigin = 2000000;

// The player's Gato at `depth_ft` with its combat state.
struct Boat {
  Boat(const GameTables& t, std::int32_t depth_ft, std::uint32_t seed = 1, std::uint32_t options = 0)
      : gato(*t.find_sub("GATO")), sim(conditions(options), kDeepSea, seed), combat(t, gato) {
    sim.clock().init(13, 6, 1, 12, 0);
    sim.set_player_combat(&combat);
    player = &sim.add(std::make_unique<PlayerSub>(
        sub_class_params(gato), fx::Vec3{kOrigin, -depth_ft * kUnitsPerFoot, kOrigin}, Angle10(0), 0));
  }

  static MissionConditions conditions(std::uint32_t options) {
    MissionConditions env;
    env.options = options;
    return env;
  }

  void run(std::int32_t seconds) {
    for (std::int32_t i = 0; i < seconds; ++i) {
      sim.simulate(Fixed16::from_int(1));
    }
  }

  const SubClass& gato;
  Simulation sim;
  PlayerCombat combat;
  PlayerSub* player = nullptr;
};

// Drops a charge `dx` ft east of the boat set to explode at its depth;
// returns the events after it went off.
MissionEvents depth_charge_at(Boat& b, std::int32_t dx_ft) {
  SimContext ctx = b.sim.context();
  spawn_depth_charge(ctx, {kOrigin + dx_ft * kUnitsPerFoot, 0, kOrigin}, Angle10(0), Angle10(-100),
                     b.player->depth_ft());
  b.run(30);
  return b.sim.take_events();
}

bool damaged(const MissionEvents& e) {
  for (const CrewReport& r : e.crew_messages) {
    if (r.id == 0x1f) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("Repair times follow severity and crew", "[damage]") {
  MissionConditions env;
  Simulation sim(env, kDeepSea, 7);
  SimContext ctx = sim.context();
  WatcomRandom r = sim.random();
  CHECK(PlayerDamage::repair_time(ctx, 50, 1) == std::max(50 * 2 * 108 + 1800 - r.random_int(3600), 60));
  CHECK(PlayerDamage::repair_time(ctx, 0, 2) == std::max(1800 - r.random_int(3600), 60));
}

TEST_CASE("Hits burn a ship at half strength and sink it at a twentieth", "[damage]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  Boat b(tables, 0);
  const ShipClass& cls = *tables.find_ship("LFREIGHT");
  auto made = std::make_unique<Ship>(ship_motion_params(cls), fx::Vec3{kOrigin, 0, kOrigin + 48 * 3000},
                                     Angle10(900), Fixed16::from_int(8), ShipOrder::kFollowPath);
  made->setup().ship_class = &cls;
  const auto strength = static_cast<std::int32_t>(cls.hull_strength);
  made->setup().hit_points = strength;
  Ship& ship = b.sim.add(std::move(made));
  const std::int32_t full_power = ship.motion().ordered_power;
  SimContext ctx = b.sim.context();

  // The player's hits count three times at the lowest combat level.
  const std::int32_t half = (strength / 2 + 2) / 3;
  ship.take_damage(ctx, b.player, DamageKind::kTorpedo, 0, half);
  CHECK(ship.setup().hit_points == strength - half * 3);
  b.run(1);
  CHECK(ship.combat().effects.burning);
  CHECK_FALSE(ship.sinking());
  // The engines are cut in proportion.
  CHECK(ship.motion().ordered_power <= full_power);
  CHECK(ship.motion().ordered_power ==
        std::min(full_power, ship.setup().hit_points * 100 / strength * ship.params().max_power / 100));

  ship.take_damage(ctx, b.player, DamageKind::kTorpedo, 0, strength);
  CHECK(ship.setup().hit_points == 0);
  b.run(1);
  const MissionEvents events = b.sim.take_events();
  CHECK(ship.sinking());
  CHECK(ship.orders() == ShipOrder::kNone);
  CHECK(ship.motion().ordered_power == 0);
  REQUIRE(events.ships_sunk.size() == 1);
  CHECK(events.ships_sunk[0] == &cls);
  CHECK(events.crew_messages.back().id == kMsgShipSinking);

  // Down at 4..9 units a second until SHIPS.DAT +188 feet deep.
  const std::int32_t rate = ship.combat().effects.sink_rate;
  CHECK(rate >= 4);
  CHECK(rate <= 9);
  std::int32_t seconds = 0;
  while (!ship.removed() && seconds < 5000) {
    b.run(1);
    ++seconds;
  }
  CHECK(seconds == Catch::Approx(static_cast<double>(cls.visual_size) * kUnitsPerFoot / rate).margin(3));
}

TEST_CASE("Other attackers' hits count once and add to the damage", "[damage]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  Boat b(tables, 0);
  const ShipClass& cls = *tables.find_ship("FUSO");
  auto made = std::make_unique<Ship>(ship_motion_params(cls), fx::Vec3{kOrigin, 0, kOrigin + 48 * 3000},
                                     Angle10(900), Fixed16::from_int(8), ShipOrder::kConvoyMember);
  made->setup().ship_class = &cls;
  made->setup().hit_points = static_cast<std::int32_t>(cls.hull_strength);
  Ship& ship = b.sim.add(std::move(made));
  SimContext ctx = b.sim.context();
  ship.take_damage(ctx, nullptr, DamageKind::kShellfire, 0, static_cast<std::int32_t>(cls.hull_strength) / 2);
  CHECK(ship.setup().damage == static_cast<std::int32_t>(cls.hull_strength) / 2);
  // Below half strength a convoy member leaves its convoy.
  CHECK(ship.orders() == ShipOrder::kFollowPath);
}

TEST_CASE("Depth charge damage falls off with distance", "[damage]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  {
    Boat b(tables, 200);
    const MissionEvents far = depth_charge_at(b, 2500);
    CHECK_FALSE(far.screen_shake);
    CHECK_FALSE(damaged(far));
    CHECK_FALSE(far.game_over.has_value());
  }
  {
    // Within 3 ft: lost for sure (end code 1, depth charged).
    Boat b(tables, 200);
    const MissionEvents close = depth_charge_at(b, 2);
    CHECK(close.screen_shake);
    REQUIRE(close.game_over.has_value());
    CHECK(*close.game_over == end_code::kDepthCharged);
  }
  // Damage when rand(distance) <= 120: always at 100 ft, about 121/250 at
  // 250 ft, never beyond 300 ft.
  const auto rate = [&](std::int32_t feet) {
    int hits = 0;
    constexpr int kTrials = 200;
    for (int i = 0; i < kTrials; ++i) {
      Boat b(tables, 200, static_cast<std::uint32_t>(i * 104729 + 3));
      const MissionEvents e = depth_charge_at(b, feet);
      CHECK_FALSE(e.game_over.has_value());
      hits += damaged(e) ? 1 : 0;
    }
    return static_cast<double>(hits) / kTrials;
  };
  CHECK(rate(100) == 1.0);
  CHECK(rate(250) == Catch::Approx(121.0 / 250).margin(0.12));
  CHECK(rate(310) == 0.0);
}

TEST_CASE("Severity grows as the margin shrinks", "[damage]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  // Expert combat level (bits 2-3 = 2): the severity is not reduced.
  // Margin 30: severity 100, four hits on systems.
  Boat b(tables, 200, 5, 0x8);
  SimContext ctx = b.sim.context();
  b.combat.damage.apply(ctx, *b.player, b.combat.tubes, DamageKind::kDepthCharge, 30, 0);
  const MissionEvents e = b.sim.take_events();
  int reports = 0;
  for (const CrewReport& r : e.crew_messages) {
    reports += r.id == 0x1f ? 1 : 0;
  }
  CHECK(reports >= 3);  // Four system hits (a tube hit may spare every tube), two breaches.
  CHECK(b.combat.damage.hull_damage() > 0);
  CHECK(b.combat.damage.damage_pct() > 0);
}

TEST_CASE("Below the crush depth the boat implodes", "[damage]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  Boat b(tables, 520);
  b.run(2);
  // Test depth 300: crush at 480 + 10 - rand(40).
  CHECK(b.combat.damage.crush_depth() >= 450);
  CHECK(b.combat.damage.crush_depth() <= 490);
  const MissionEvents e = b.sim.take_events();
  REQUIRE(e.game_over.has_value());
  CHECK(*e.game_over == end_code::kCrushed);

  Boat safe(tables, 200, 1, combat_option::kInvulnerable);
  safe.run(2);
  CHECK_FALSE(safe.sim.take_events().game_over.has_value());
}

TEST_CASE("Gunfire and strafing miss a submerged boat", "[damage]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  Boat b(tables, 100);
  SimContext ctx = b.sim.context();
  CHECK_FALSE(b.player->take_damage(ctx, nullptr, DamageKind::kGunfire, 0, 5));
  // Shellfire reaches it only 1 time in 25 (here it hits the periscope).
  int taken = 0;
  for (int i = 0; i < 100; ++i) {
    taken += b.player->take_damage(ctx, nullptr, DamageKind::kShellfire, 0, 5) ? 1 : 0;
  }
  CHECK(taken < 15);
  CHECK(b.player->systems()[4].flags == (taken > 0 ? 2 : 0));
}

}  // namespace sh
