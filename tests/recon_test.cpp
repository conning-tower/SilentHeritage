// Tests of the photo reconnaissance: tracking the recon target in view
// (`player_track_recon_target_in_view`), the photo on the Tab key
// (`player_take_recon_photo`) with its crew message and patrol log entry,
// and the mission's end a minute after the last objective went
// (`player_update`'s lost-contact end).
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "game/class_params.h"
#include "game/frontend_results.h"
#include "game/mission.h"
#include "game/stations/cabin.h"
#include "game_data.h"
#include "sim/aircraft_strike.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "stations_control_fixture.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;
constexpr std::int32_t kMsgMissionCompleted = 69;
constexpr std::int32_t kMinute = 60 << 16;

// A surfaced GATO at the origin heading north in a single mission that
// ends on lost contact, and a photo-recon target `yards` away at
// `bearing` (the base's harbor: no landmarks).
struct ReconRig {
  ReconRig(const GameTables& t, std::int32_t yards, Angle10 bearing, std::int32_t delay_min = 0)
      : gato(*t.find_sub("GATO")), sim(conditions(), kDeepSea, 3), combat(t, gato) {
    sim.clock().init(12, 5, 1, 12, 0);
    sim.set_player_combat(&combat);
    player = &sim.add(std::make_unique<PlayerSub>(sub_class_params(gato), fx::Vec3{kOrigin, 0, kOrigin}, Angle10(0), 0));
    const fx::SinCos sc = fx::angle_sincos(bearing);
    const std::int32_t d = yards * kYard;
    bases[0].station = {kOrigin, 0, kOrigin};
    bases[0].harbor = {kOrigin + fx::mul(d, sc.sin), 0, kOrigin + fx::mul(d, sc.cos)};
    theatre.bases = bases;
    report = &sim.add(std::make_unique<IntelReport>(theatre, IntelReport::Kind::kPhotoRecon, 0, delay_min, 600));
  }

  static MissionConditions conditions() {
    MissionConditions c;
    c.end_on_lost_contact = true;
    return c;
  }
  ContactReports& reports() { return sim.sensors().reports(); }
  // Points the periscope `off` tenths of a degree right of the target.
  void aim(std::int32_t off) {
    const fx::Vec3 own = player->position();
    const fx::Vec3 pos = report->position();
    const Angle10 b = fx::heading_from_delta(pos.x - own.x, pos.z - own.z);
    combat.view_bearing = fx::angle_sub(Angle10(b.tenths() + off), player->motion().heading);
  }
  // Runs one 0.1 s step and returns its events.
  MissionEvents step() {
    sim.step(10);
    return sim.take_events();
  }

  const SubClass& gato;
  Simulation sim;
  PlayerCombat combat;
  std::vector<BaseRecord> bases = std::vector<BaseRecord>(1);
  AirTheatre theatre;
  PlayerSub* player = nullptr;
  IntelReport* report = nullptr;
};

bool has_message(const MissionEvents& e, std::int32_t id) {
  for (const CrewReport& r : e.crew_messages) {
    if (r.id == id) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("A recon target in daylight within 2 degrees of the view can be photographed", "[recon]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  ReconRig rig(tables, 3000, Angle10(300));
  rig.aim(0);
  rig.step();  // Radios the target.
  CHECK(rig.report->recon_objective() == 1);
  rig.step();
  CHECK(rig.reports().recon_target() == rig.report);
  CHECK(rig.reports().recon_in_view() == rig.report);

  // 2.0 degrees off is still in the picture, 2.1 is not.
  rig.aim(20);
  rig.step();
  CHECK(rig.reports().recon_in_view() == rig.report);
  rig.aim(-21);
  rig.step();
  CHECK(rig.reports().recon_target() == rig.report);
  CHECK(rig.reports().recon_in_view() == nullptr);
  SimContext missed = rig.sim.context();
  CHECK_FALSE(rig.reports().take_recon_photo(missed));
  CHECK(rig.sim.take_events().crew_messages.empty());

  rig.aim(0);
  rig.step();
  SimContext ctx = rig.sim.context();
  CHECK(rig.reports().take_recon_photo(ctx));
  const MissionEvents e = rig.sim.take_events();
  REQUIRE(e.crew_messages.size() == 1);
  CHECK(e.crew_messages[0].id == kMsgMissionCompleted);
  CHECK(e.photo_recons == std::vector<std::int32_t>{-1});
  CHECK(rig.report->removed());
  CHECK(rig.reports().recon_in_view() == nullptr);
  // Only one photo of it.
  CHECK_FALSE(rig.reports().take_recon_photo(ctx));
}

TEST_CASE("No recon photo at night, beyond the lookouts' range or before the radio report", "[recon]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);

  SECTION("Light under 30 %") {
    // Close: the dusk light cuts the lookouts' range by 56 %.
    ReconRig rig(tables, 1500, Angle10(0));
    rig.aim(0);
    rig.sim.sensors().environment().light_pct = 29;
    rig.step();
    rig.step();
    CHECK(rig.reports().recon_target() == rig.report);
    CHECK(rig.reports().recon_in_view() == nullptr);
    rig.sim.sensors().environment().light_pct = 30;
    rig.step();
    CHECK(rig.reports().recon_in_view() == rig.report);
  }
  SECTION("Out of sighting range (4 ft eye, 5 ft target), still the nearest target") {
    ReconRig rig(tables, 6000, Angle10(0));
    rig.aim(0);
    rig.step();
    rig.step();
    CHECK(rig.reports().recon_target() == rig.report);
    CHECK(rig.reports().recon_in_view() == nullptr);
    SimContext ctx = rig.sim.context();
    CHECK_FALSE(rig.reports().take_recon_photo(ctx));
    CHECK_FALSE(has_message(rig.sim.take_events(), kMsgMissionCompleted));
  }
  SECTION("Beyond 25000 yd: no target") {
    ReconRig rig(tables, 25100, Angle10(0));
    rig.step();
    rig.step();
    CHECK(rig.reports().recon_target() == nullptr);
  }
  SECTION("Not radioed yet: an objective, but not a target") {
    ReconRig rig(tables, 3000, Angle10(0), 2);
    rig.aim(0);
    for (int i = 0; i < 20; ++i) {
      rig.step();
    }
    CHECK(rig.report->recon_objective() == -1);
    CHECK(rig.reports().recon_target() == nullptr);
    // It keeps the contact time from running.
    CHECK(rig.sim.sensors().quiet_time() == 0);
  }
}

TEST_CASE("A minute after the last objective a single mission ends", "[recon]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const auto run_to_end = [](ReconRig& rig, std::int32_t max_steps) {
    std::optional<std::int32_t> over;
    std::int32_t steps = 0;
    while (!over && steps < max_steps) {
      over = rig.step().game_over;
      ++steps;
    }
    return std::pair{over, steps};
  };
  for (const bool special : {true, false}) {
    ReconRig rig(tables, 3000, Angle10(0));
    rig.reports().set_group_activity(special);
    rig.aim(0);
    // The target keeps the mission going.
    CHECK_FALSE(run_to_end(rig, 700).first);
    SimContext ctx = rig.sim.context();
    REQUIRE(rig.reports().take_recon_photo(ctx));
    // Then 60 s without contact: "Mission over." on a special mission
    // (a success, end code 19), "Contact with enemy broken." otherwise.
    const auto [over, steps] = run_to_end(rig, 700);
    REQUIRE(over);
    CHECK(*over == (special ? 9 : 7));
    CHECK(rig.sim.sensors().quiet_time() > kMinute);
    CHECK(rig.sim.sensors().quiet_time() <= kMinute + (1 << 16) / 10 + 1);
    CHECK(steps >= 600);
  }
  CHECK(returns_to_port(10 + 9));

  SECTION("Not in a career or without the scenario's flag") {
    for (const bool career : {true, false}) {
      ReconRig rig(tables, 3000, Angle10(0));
      if (career) {
        rig.sim.conditions().options |= sensor_option::kContactGroups;
      } else {
        rig.sim.conditions().end_on_lost_contact = false;
      }
      rig.report->remove();
      CHECK_FALSE(run_to_end(rig, 700).first);
    }
  }
}

TEST_CASE("The Tab key photographs the target: crew message, patrol log, then the mission ends", "[recon]") {
  SH_REQUIRE_GAME_DATA(install);
  test::ControlRig rig(install);
  auto cabin_station = std::make_unique<CabinStation>(rig.screen->env());
  CabinStation& cabin = *cabin_station;
  rig.screen->install(StationId::kCabin, std::move(cabin_station));
  Mission& mission = rig.mission();
  Simulation& sim = mission.sim();
  for (const auto& v : sim.vehicles()) {
    if (v.get() != &mission.player()) {
      v->remove();
    }
  }
  sim.conditions().end_on_lost_contact = true;
  sim.sensors().reports().set_group_activity(true);
  const PlayerSub& boat = mission.player();
  const fx::Vec3 own = boat.position();
  std::vector<BaseRecord> bases(1);
  bases[0].station = {own.x, 0, own.z};
  bases[0].harbor = {own.x, 0, own.z + 2000 * kYard};
  AirTheatre theatre;
  theatre.bases = bases;
  IntelReport& report = sim.add(std::make_unique<IntelReport>(theatre, IntelReport::Kind::kPhotoRecon, 0, 0, 600));
  mission.player_combat().view_bearing = fx::angle_sub(Angle10(0), boat.motion().heading);
  rig.frames(3);
  sim.sensors().environment().light_pct = 100;
  rig.frames(2);
  REQUIRE(sim.sensors().reports().recon_in_view() == &report);

  rig.key(0x0f09, 3);
  CHECK(sim.sensors().reports().recon_in_view() == nullptr);
  bool said = false;
  const CrewMessageLog& log = rig.screen->bar().log();
  for (std::size_t i = 0; i < CrewMessageLog::kSlots; ++i) {
    said = said || log.entry(i).id == kMsgMissionCompleted;
  }
  CHECK(said);
  REQUIRE_FALSE(cabin.logs().patrol.empty());
  const PatrolLogEntry& entry = cabin.logs().patrol.back();
  CHECK(entry.type == PatrolLogEntry::kPhotoRecon);
  CHECK(entry.name == "Harbor");
  CHECK(entry.tonnage == 0);

  int frames = 0;
  while (rig.frames(1) && frames < 5000) {
    ++frames;
  }
  REQUIRE(mission.game_over());
  CHECK(*mission.game_over() == GameOverReason::kMissionOver);
  CHECK(mission.end_code() == 10 + 9);
}

TEST_CASE("A photographed landmark is logged by its location type", "[recon]") {
  SH_REQUIRE_GAME_DATA(install);
  test::ControlRig rig(install);
  CabinStation cabin(rig.screen->env());
  Mission& mission = rig.mission();
  const fx::Vec3 own = mission.player().position();
  const PatrolLogEntry e =
      photo_recon_log_entry(mission.sim().clock(), mission.world(), own.x, own.z, 2, cabin.location_types());
  CHECK(e.type == PatrolLogEntry::kPhotoRecon);
  CHECK(e.name == cabin.location_types()[2]);
  CHECK(e.day == mission.sim().clock().day());
}

}  // namespace sh
