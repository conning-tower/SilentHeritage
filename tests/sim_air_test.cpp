#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "core/random.h"
#include "game/class_params.h"
#include "game/mission.h"
#include "game_data.h"
#include "sim/air_sorties.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/enemy_sub.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/shell.h"
#include "sim/simulation.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;

// A surfaced GATO at the origin heading north, in daylight, with its combat
// state, and the aircraft or submarines added to it.
struct Theatre {
  explicit Theatre(const GameTables& t, std::uint32_t seed = 1)
      : tables(t), gato(*t.find_sub("GATO")), sim(MissionConditions{}, kDeepSea, seed), combat(t, gato) {
    sim.clock().init(12, 5, 1, 12, 0);
    sim.set_player_combat(&combat);
    player = &sim.add(std::make_unique<PlayerSub>(sub_class_params(gato), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 0));
  }

  Aircraft& add_aircraft(const char* type, std::int32_t bombs, bool strafes, std::int32_t crew = 1) {
    auto a = std::make_unique<Aircraft>();
    AircraftSetup& s = a->setup();
    s.type = tables.find_aircraft(type);
    s.crew = crew;
    s.bomb_load = bombs;
    s.strafes = strafes;
    s.mission = AircraftMissionCode::kPatrol;
    s.orbit_x = kOrigin;
    s.orbit_z = kOrigin;
    s.orbit_radius = 300000;
    s.sorties = 10000;
    return sim.add(std::move(a));
  }

  EnemySub& add_sub(std::int32_t crew, ShipOrder orders) {
    const ShipClass& cls = *tables.find_ship("KAIDAI TYPE");
    auto sub = std::make_unique<EnemySub>(fx::Vec3{kOrigin + 20000 * kYard, 0, kOrigin}, Angle10(0),
                                          Fixed16::from_int(10), orders, 1);
    sub->setup().ship_class = &cls;
    sub->setup().torpedo = tables.find_torpedo("TYPE95-1");
    sub->setup().crew = crew;
    sub->setup().hit_points = static_cast<std::int32_t>(cls.hull_strength);
    return sim.add(std::move(sub));
  }

  std::int32_t range_yd(const Vehicle& v) const {
    const fx::Vec3 a = player->position();
    const fx::Vec3 b = v.position();
    return static_cast<std::int32_t>(fx::approx_distance(a.x - b.x, a.z - b.z) / kYard);
  }

  const GameTables& tables;
  const SubClass& gato;
  Simulation sim;
  PlayerCombat combat;
  PlayerSub* player = nullptr;
};

template <typename T>
const T* find_vehicle(const Simulation& sim) {
  for (const auto& v : sim.vehicles()) {
    if (const T* t = dynamic_cast<const T*>(v.get())) {
      return t;
    }
  }
  return nullptr;
}

}  // namespace

TEST_CASE("Enemy submarine and aircraft inits draw in the original order", "[air][enemysub]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Theatre t(tables, 4242);
  WatcomRandom replay = t.sim.random();
  EnemySub& sub = t.add_sub(1, ShipOrder::kFollowPath);
  Aircraft& plane = t.add_aircraft("VAL", 1, true);
  // enemy_sub_init: the effects block (sink rate, blast sprite); aircraft_init: rand(3).
  CHECK(sub.combat().effects.sink_rate == replay.random_int(6) + 4);
  CHECK(sub.combat().effects.explosion_sprite == replay.random_int(2));
  CHECK(plane.ai().shadow_time == (replay.random_int(3) + 3) * 0x3c0000);
  CHECK(t.sim.random().rand() == replay.rand());
  // Landed and gone until a sortie launches it; hit points by realism.
  CHECK_FALSE(plane.airborne());
  CHECK(plane.sinking());
  CHECK(plane.ai().hit_points == 3);
  CHECK(plane.flight().heading == Angle10(1800));
}

TEST_CASE("HIST57 spawns its submarine and escort aircraft", "[air][enemysub][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> m = Mission::load(install, tables, "SCEN/HIST57.SCN", {});
  REQUIRE(m->enemy_subs().size() == 1);
  const EnemySub& sub = *m->enemy_subs()[0];
  CHECK(sub.position().x == 10152960);
  CHECK(sub.position().z == 14620160);
  CHECK(sub.dive().depth_ft == 0);
  CHECK(sub.dive_params().draft_ft == 0);
  CHECK(sub.dive_params().ballast_neutral == 2602);
  CHECK(sub.dive().ballast == 1810 * 100);
  CHECK(sub.side() == 1);
  CHECK(sub.setup().torpedo != nullptr);
  CHECK(sub.radar_cross_section({}, Angle10(0)) == 343 * 100);
  REQUIRE(m->aircraft().size() == 1);
  const Aircraft& val = *m->aircraft()[0];
  CHECK(val.setup().mission == AircraftMissionCode::kEscort);
  CHECK(val.escorted_group() == 1);
  CHECK(val.setup().bomb_load == 1);
  CHECK_FALSE(val.airborne());
  // The take-off range: the clear-day range to a 5000 ft target from the bridge, +5 %.
  CHECK(m->air_sorties().reference_range_yd() ==
        sighting_reference_range(m->player().position(), m->player().sub().bridge_height_ft));
  CHECK(m->air_sorties().reference_range_yd() > 20000);

  const std::unique_ptr<Mission> m60 = Mission::load(install, tables, "SCEN/HIST60.SCN", {});
  REQUIRE(m60->aircraft().size() == 2);
  CHECK(m60->aircraft()[1]->setup().type->index_name == "BETTY");
  CHECK(m60->aircraft()[1]->setup().bomb_load == 4);
}

TEST_CASE("Air sorties launch at the reference range when light and fog allow", "[air]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Theatre t(tables);
  Aircraft& plane = t.add_aircraft("VAL", 1, true);
  plane.setup().orbit_z = kOrigin + 40000 * kYard;
  AirSorties sorties;
  sorties.reset(20000);
  SensorEnvironment& env = t.sim.sensors().environment();
  env.light_pct = 100;
  env.fog_pct = 0;
  SimContext ctx = t.sim.context();
  // Four hundredths do not make a pass; the first pass sees the 200/200
  // snapshot and launches nothing.
  t.sim.clock().advance(4, false);
  sorties.tick(ctx);
  t.sim.clock().advance(5, false);
  sorties.tick(ctx);
  CHECK_FALSE(plane.airborne());
  // Dusk: nothing takes off.
  env.light_pct = 40;
  sorties.tick(ctx);
  CHECK_FALSE(plane.airborne());
  // Fog thickening since the last pass: still nothing.
  env.light_pct = 100;
  env.fog_pct = 10;
  sorties.tick(ctx);
  CHECK_FALSE(plane.airborne());
  // No worse than the last pass: it takes off.
  sorties.tick(ctx);
  REQUIRE(plane.airborne());
  CHECK_FALSE(plane.sinking());
  CHECK(plane.setup().sorties == 9999);
  CHECK(plane.ai().bombs == 1);
  CHECK(plane.position().y == 0x12c0);
  // At 20000 yd (halved to 15 bits then doubled) toward its station, north +- 15 deg.
  CHECK(t.range_yd(plane) >= 19970);
  CHECK(t.range_yd(plane) <= 20030);
  const Angle10 bearing = fx::heading_from_delta(plane.position().x - kOrigin, plane.position().z - kOrigin);
  CHECK((bearing.tenths() <= 150 || bearing.tenths() >= 3450));
}

TEST_CASE("An aircraft bombs a surfaced boat inside its release range, then strafes", "[air]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Theatre t(tables);
  SensorEnvironment& env = t.sim.sensors().environment();
  env.light_pct = 100;
  Aircraft& plane = t.add_aircraft("VAL", 1, true);
  SimContext ctx = t.sim.context();
  plane.take_off(ctx, {kOrigin, 0x12c0, kOrigin + 3000 * kYard}, 30000);

  std::int32_t bomb_range = -1;
  std::int32_t release = 0;
  std::int32_t strafe_range = -1;
  for (int frame = 0; frame < 20000 && strafe_range < 0; ++frame) {
    if (bomb_range < 0) {
      release = 300 + plane.ai().release_offset;
    }
    t.sim.step(5);
    for (const auto& v : t.sim.vehicles()) {
      if (bomb_range < 0 && v->class_id() == VehicleClass::kBomb) {
        bomb_range = plane.ai().last_range;
      }
      if (const auto* shell = dynamic_cast<const Shell*>(v.get());
          shell != nullptr && shell->owner() == &plane && strafe_range < 0) {
        strafe_range = t.range_yd(*shell);
      }
    }
  }
  REQUIRE(bomb_range >= 0);
  // Released inside the crew's range (300 yd for a veteran, plus the run's offset).
  CHECK(bomb_range < release);
  CHECK(plane.ai().bombs == 0);
  CHECK(plane.ai().drops == 1);
  REQUIRE(strafe_range >= 0);
  // The guns open inside 1200 yd, on a nose-on run at 80 ft.
  CHECK(strafe_range < 1200);
  CHECK(plane.ai().state == AircraftState::kStrafeRun);
  CHECK(plane.flight().ordered_altitude_ft == 80);
}

TEST_CASE("An aircraft homes and lands out of range in bad light", "[air]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Theatre t(tables);
  SensorEnvironment& env = t.sim.sensors().environment();
  env.light_pct = 30;
  Aircraft& plane = t.add_aircraft("BETTY", 4, true, 2);
  SimContext ctx = t.sim.context();
  plane.take_off(ctx, {kOrigin, 0x12c0, kOrigin + 2000 * kYard}, 1000);
  for (int frame = 0; frame < 8; ++frame) {
    t.sim.step(5);
  }
  CHECK(plane.ai().state == AircraftState::kHome);
  CHECK(plane.flight().ordered_altitude_ft == 1000);
  for (int frame = 0; frame < 20000 && plane.airborne(); ++frame) {
    t.sim.step(5);
  }
  REQUIRE_FALSE(plane.airborne());
  CHECK(t.range_yd(plane) > 3000);
  // Relaunch in (10 + rand(10)) x (3 - crew) minutes.
  CHECK(plane.relaunch_timer() >= 10 * 0x3c0000);
  CHECK(plane.relaunch_timer() < 20 * 0x3c0000);
}

TEST_CASE("An alerted enemy submarine dives to its orders' depths", "[enemysub]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  SECTION("A veteran crew closes the contact at periscope depth") {
    Theatre t(tables);
    EnemySub& sub = t.add_sub(1, ShipOrder::kFollowPath);
    sub.set_alert(1);
    sub.ai().contact.position = {kOrigin + 40000 * kYard, 0, kOrigin};
    sub.ai().contact.age = 0x10000;
    for (int frame = 0; frame < 6000; ++frame) {
      t.sim.step(10);
    }
    CHECK(sub.dive().ordered_depth_ft == 64);
    CHECK(sub.dive().depth_ft > 54);
    CHECK(sub.dive().depth_ft <= 66);
    CHECK(sub.exposure() == 5);
    CHECK(sub.radar_cross_section({}, Angle10(0)) == 0);
    CHECK(sub.ai().mode == EnemySubMode::kCloseOnContact);
    // Steering for the contact to the east.
    const fx::Vec3 p = sub.position();
    CHECK(sub.motion().ordered_heading ==
          fx::heading_from_delta(sub.ai().contact.position.x - p.x, sub.ai().contact.position.z - p.z));
  }
  SECTION("A green crew searches deep") {
    Theatre t(tables);
    EnemySub& sub = t.add_sub(0, ShipOrder::kFollowPath);
    sub.set_alert(1);
    for (int frame = 0; frame < 9000; ++frame) {
      t.sim.step(10);
    }
    CHECK(sub.ai().mode == EnemySubMode::kSearchDeep);
    CHECK(sub.dive().ordered_depth_ft == 100);
    CHECK(sub.dive().depth_ft > 90);
  }
  SECTION("Unalerted, it stays surfaced and is heard only on the surface") {
    Theatre t(tables);
    EnemySub& sub = t.add_sub(1, ShipOrder::kFollowPath);
    for (int frame = 0; frame < 500; ++frame) {
      t.sim.step(10);
    }
    CHECK(sub.dive().depth_ft == 0);
    CHECK(sub.exposure() == 1);
    CHECK(sub.noise_level() > 20);
  }
}

TEST_CASE("A bomber group radios its strike, bombs its target and leaves downed pilots", "[air]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Theatre t(tables, 99);
  std::vector<BaseRecord> bases(1);
  bases[0].station = {kOrigin + 10000 * kYard, 0, kOrigin};
  bases[0].harbor = {kOrigin + 11000 * kYard, 0, kOrigin};
  std::vector<Location> locations(2);
  locations[0] = {LocationType::kCity, kOrigin + 60000 * kYard, 0, kOrigin, Angle10(0)};
  locations[1] = {LocationType::kPortFacilities, kOrigin + 9000 * kYard, 0, kOrigin, Angle10(0)};
  AirTheatre theatre;
  theatre.bases = bases;
  theatre.locations = locations;
  theatre.grid_width = 200;
  theatre.grid_height = 200;
  theatre.scale_shift = 0;
  theatre.air_edges = {0, 0, 1, 0};  // Only the west edge.

  WatcomRandom replay = t.sim.random();
  AirGroup& group = t.sim.add(std::make_unique<AirGroup>(theatre, 0, 12, 2, 0));
  // airgroup_init: rand(4) until the open edge, the row on it, the formation seed.
  std::int32_t edge = 0;
  do {
    edge = replay.random_int(4);
  } while (edge != 2);
  const std::int32_t row = replay.random_int(200);
  CHECK(group.start_x() == 0x8000);
  CHECK(group.start_z() == 0x10000 * 200 - (row * 0x10000 + 0x8000));
  CHECK(group.formation_seed() == static_cast<std::uint32_t>(replay.rand()));
  // Aimed at the landmark nearest its base.
  CHECK(group.target_x() == kOrigin + 9000 * kYard);
  CHECK(group.plane_count() == 12);

  // Two minutes of launch delay, then the strike report.
  std::vector<RadioReport> radio;
  bool bombed = false;
  for (int frame = 0; frame < 200000 && !(bombed && t.sim.vehicles().size() > 3 && !radio.empty() &&
                                           radio.back().kind == RadioReport::Kind::kDownedPilot);
       ++frame) {
    t.sim.step(10);
    MissionEvents e = t.sim.take_events();
    radio.insert(radio.end(), e.radio_reports.begin(), e.radio_reports.end());
    if (frame == 100) {
      CHECK(radio.empty());
      CHECK(group.launch_delay_min() > 0);
    }
    for (const auto& v : t.sim.vehicles()) {
      if (v->class_id() == VehicleClass::kBomb && !bombed) {
        bombed = true;
        // Over the target (within 1000 yd of 500 yd north-east of it).
        CHECK(std::abs(v->position().x - (group.target_x() + 24000)) <= 1000 * kYard);
        CHECK(group.over_target());
      }
    }
  }
  REQUIRE(radio.size() >= 2);
  CHECK(radio[0].kind == RadioReport::Kind::kAirStrike);
  CHECK(radio[0].args[4] == 0);
  CHECK(radio[0].x == kOrigin + 9000 * kYard);
  CHECK(bombed);
  CHECK(radio.back().kind == RadioReport::Kind::kDownedPilot);
  const DownedPilot* pilot = find_vehicle<DownedPilot>(t.sim);
  REQUIRE(pilot != nullptr);
  CHECK(pilot->survival_hours() > 27);
  CHECK(pilot->survival_hours() <= 36);
  CHECK(std::abs(pilot->reported_x() - pilot->position().x) <= 1600 * kYard);
}

TEST_CASE("A photo recon radios its target after its delay and expires", "[air]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Theatre t(tables, 5);
  std::vector<BaseRecord> bases(1);
  bases[0].station = {kOrigin, 0, kOrigin};
  bases[0].harbor = {kOrigin + 500 * kYard, 0, kOrigin};
  AirTheatre theatre;
  theatre.bases = bases;  // No port or industry nearby: the harbor.
  IntelReport& recon = t.sim.add(std::make_unique<IntelReport>(theatre, IntelReport::Kind::kPhotoRecon, 0, 2, 3));
  CHECK(recon.location_type() == -1);
  CHECK(recon.position().x == kOrigin + 500 * kYard);
  std::vector<RadioReport> radio;
  std::int32_t sent_at = -1;
  const std::int32_t start = t.sim.clock().minutes_elapsed();
  for (int frame = 0; frame < 20000 && find_vehicle<IntelReport>(t.sim) != nullptr; ++frame) {
    t.sim.step(100);
    for (const RadioReport& r : t.sim.take_events().radio_reports) {
      radio.push_back(r);
      sent_at = t.sim.clock().minutes_elapsed() - start;
    }
  }
  REQUIRE(radio.size() == 1);
  CHECK(radio[0].kind == RadioReport::Kind::kPhotoRecon);
  CHECK(radio[0].args[0] == -1);
  CHECK(radio[0].args[1] == 0);
  // After its 2 minutes of delay; removed once its 3 minutes (and one more) ran out.
  CHECK(sent_at == 2);
  CHECK(find_vehicle<IntelReport>(t.sim) == nullptr);
  CHECK(t.sim.clock().minutes_elapsed() - start - sent_at == 4);
}

}  // namespace sh
