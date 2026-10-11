// The sound man's screws reports for a ship that fades out and back in
// (`ship_hydrophone_check`), in the original and with
// `MissionRules::quiet_sonar_reacquire`.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "game/class_params.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/mission_rules.h"
#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/enemy_sub.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim_classes.h"
#include "sim_test_grid.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;
constexpr std::int32_t kHighSpeedScrews = 41;
constexpr std::int32_t kLowSpeedScrews = 42;

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

bool is_screws(const CrewReport& r) { return r.id == kHighSpeedScrews || r.id == kLowSpeedScrews; }

// A stopped boat at 100 ft listening to a destroyer 1000 yd south whose
// screws turn (heard) or stop (not heard). Only the hydrophone check runs,
// in frames of 0.15 s, with the compression asked at 8x.
class Listening {
 public:
  explicit Listening(bool quiet) : sim_(conditions(quiet), kDeepSea) {
    sim_.add(
        std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin}, Angle10(0), 0));
    ship_ = &sim_.add(std::make_unique<Ship>(test::fubuki(), fx::Vec3{kOrigin, 0, kOrigin - 1000 * kYard},
                                             Angle10(900), Fixed16::from_int(20), ShipOrder::kNone));
    ship_->ai().cls = destroyer();
    turning_power_ = ship_->motion().power;
    sim_.clock().set_auto_adjust(false);
    raise();
  }

  // Runs `seconds` with the screws turning or stopped; returns the number
  // of screws reports (the compression resets are counted by `resets`).
  int listen(std::int32_t seconds, bool turning) {
    ship_->motion().power = turning ? turning_power_ : 0;
    int reports = 0;
    for (std::int64_t t = 0; t < static_cast<std::int64_t>(seconds) * 100; t += sim_.clock().frame_ticks()) {
      sim_.clock().advance(15, false);
      SimContext ctx = sim_.context();
      ship_hydrophone_check(*ship_, ctx);
      const std::vector<CrewReport> messages = sim_.take_events().crew_messages;
      reports += static_cast<int>(std::count_if(messages.begin(), messages.end(), is_screws));
      if (sim_.clock().requested_compression() == 1) {
        ++resets_;
        raise();
      }
    }
    return reports;
  }

  // Runs one frame in which the ship pinged, its screws stopped; returns
  // whether it was heard.
  bool ping() {
    ship_->motion().power = 0;
    ship_->ai().heard.pinging = true;
    sim_.clock().advance(15, false);
    SimContext ctx = sim_.context();
    ship_hydrophone_check(*ship_, ctx);
    return ship_->ai().heard.heard;
  }

  int resets() const { return resets_; }

 private:
  static MissionConditions conditions(bool quiet) {
    MissionConditions env;
    env.rules.quiet_sonar_reacquire = quiet;
    return env;
  }
  void raise() {
    while (sim_.clock().requested_compression() < 8) {
      sim_.clock().double_compression();
    }
  }

  Simulation sim_;
  Ship* ship_ = nullptr;
  std::int32_t turning_power_ = 0;
  int resets_ = 0;
};

}  // namespace

TEST_CASE("Original: screws fading back in are called again once 3 minutes passed", "[sensors][quiet_sonar]") {
  SH_REQUIRE_GAME_EXE();
  Listening l(false);
  CHECK(l.listen(1, true) == 1);
  CHECK(l.resets() == 1);
  // Lost for 10 s within the 3 minutes: not called.
  CHECK(l.listen(60, true) == 0);
  CHECK(l.listen(10, false) == 0);
  CHECK(l.listen(60, true) == 0);
  // Heard all along past the 3 minutes: not called while heard...
  CHECK(l.listen(60, true) == 0);
  CHECK(l.resets() == 1);
  // ...but called again, compression reset, after a 1 s fade.
  CHECK(l.listen(1, false) == 0);
  CHECK(l.listen(1, true) == 1);
  CHECK(l.resets() == 2);
}

TEST_CASE("Quiet reacquire: a ship heard in the last 3 minutes is not called again", "[sensors][quiet_sonar]") {
  SH_REQUIRE_GAME_EXE();
  Listening l(true);
  // A new ship is called and resets the compression as in the original.
  CHECK(l.listen(1, true) == 1);
  CHECK(l.resets() == 1);
  CHECK(l.listen(60, true) == 0);
  CHECK(l.listen(10, false) == 0);
  CHECK(l.listen(60, true) == 0);
  CHECK(l.listen(60, true) == 0);
  // Fading out and back in after more than 3 minutes heard: silent.
  CHECK(l.listen(1, false) == 0);
  CHECK(l.listen(1, true) == 0);
  CHECK(l.listen(179, false) == 0);
  CHECK(l.listen(1, true) == 0);
  CHECK(l.resets() == 1);
  // Unheard for a full 3 minutes: called again, compression reset.
  CHECK(l.listen(181, false) == 0);
  CHECK(l.listen(1, true) == 1);
  CHECK(l.resets() == 2);
}

TEST_CASE("Quiet reacquire: a ship heard only by its pings is still called", "[sensors][quiet_sonar]") {
  SH_REQUIRE_GAME_EXE();
  for (const bool quiet : {false, true}) {
    INFO(quiet);
    Listening l(quiet);
    // A ping frame is heard but never called.
    CHECK(l.ping());
    CHECK(l.listen(1, false) == 0);
    CHECK(l.ping());
    CHECK(l.listen(1, false) == 0);
    // The screws then come into range: the first call.
    CHECK(l.listen(1, true) == 1);
    CHECK(l.resets() == 1);
  }
}

TEST_CASE("Quiet reacquire: an enemy submarine's screws follow the same rule", "[sensors][quiet_sonar]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  for (const bool quiet : {false, true}) {
    INFO(quiet);
    MissionConditions env;
    env.rules.quiet_sonar_reacquire = quiet;
    Simulation sim(env, kDeepSea);
    sim.add(std::make_unique<PlayerSub>(sub_class_params(*tables.find_sub("GATO")),
                                        fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin}, Angle10(0), 0));
    // A surfaced Kaidai 1000 yd south at 10 kn: heard while its screws turn.
    auto boat = std::make_unique<EnemySub>(fx::Vec3{kOrigin, 0, kOrigin - 1000 * kYard}, Angle10(900),
                                           Fixed16::from_int(10), ShipOrder::kNone, 1);
    boat->setup().ship_class = tables.find_ship("KAIDAI TYPE");
    EnemySub& sub = sim.add(std::move(boat));
    const std::int32_t turning = sub.motion().power;
    REQUIRE(turning != 0);
    sim.clock().set_auto_adjust(false);
    int resets = 0;
    const auto listen = [&](std::int32_t seconds, bool on) {
      sub.motion().power = on ? turning : 0;
      int reports = 0;
      for (std::int64_t t = 0; t < static_cast<std::int64_t>(seconds) * 100; t += sim.clock().frame_ticks()) {
        while (sim.clock().requested_compression() < 8) {
          sim.clock().double_compression();
        }
        sim.clock().advance(15, false);
        SimContext ctx = sim.context();
        sub.hydrophone_check(ctx);
        const std::vector<CrewReport> messages = sim.take_events().crew_messages;
        reports += static_cast<int>(std::count_if(messages.begin(), messages.end(), is_screws));
        if (sim.clock().requested_compression() == 1) {
          ++resets;
        }
      }
      return reports;
    };
    CHECK(listen(1, true) == 1);
    CHECK(listen(240, true) == 0);
    // A short fade after 3 minutes heard: called again only in the original.
    CHECK(listen(1, false) == 0);
    CHECK(listen(1, true) == (quiet ? 0 : 1));
    CHECK(resets == (quiet ? 1 : 2));
    // 3 minutes unheard: called again with the rule too.
    CHECK(listen(181, false) == 0);
    CHECK(listen(1, true) == 1);
    CHECK(resets == (quiet ? 2 : 3));
  }
}

namespace {

// One run of HIST49 (escorts near the boat), the boat sent to 150 ft at
// ahead 1/3, the compression raised to the cap again whenever it drops.
struct Run {
  std::vector<std::uint32_t> random_states;  // After each frame.
  std::vector<std::size_t> report_frames;    // Frames with a screws report.
  std::vector<std::size_t> reset_frames;     // Frames that reset the compression.
};

Run run_hist49(const Install& install, const GameTables& tables, bool quiet, std::int32_t minutes) {
  std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, "SCEN/HIST49.SCN");
  mission->sim().conditions().rules.quiet_sonar_reacquire = quiet;
  GameClock& clock = mission->sim().clock();
  const auto raise = [&] {
    while (clock.double_compression()) {
    }
  };
  mission->simulate_frame(5, true);
  {
    SimContext ctx = mission->sim().context();
    mission->player().order_dive(ctx);
  }
  mission->player().set_ordered_depth(150);
  mission->player().set_telegraph(1);
  raise();
  Run run;
  std::int64_t game = 0;
  while (game < static_cast<std::int64_t>(minutes) * 6000 && !mission->game_over()) {
    const MissionEvents events = mission->simulate_frame(5, false);
    game += clock.frame_ticks();
    const std::size_t frame = run.random_states.size();
    run.random_states.push_back(mission->sim().random().state());
    if (std::any_of(events.crew_messages.begin(), events.crew_messages.end(), is_screws)) {
      run.report_frames.push_back(frame);
    }
    if (clock.requested_compression() == 1) {
      run.reset_frames.push_back(frame);
      raise();
    }
  }
  return run;
}

}  // namespace

TEST_CASE("Quiet reacquire keeps the random draws and drops only repeated calls", "[sensors][quiet_sonar]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const Run original = run_hist49(install, tables, false, 15);
  const Run quiet = run_hist49(install, tables, true, 15);
  // The same frames and draws: detection is untouched.
  CHECK(quiet.random_states == original.random_states);
  // The original calls ships again as their screws fade in and out (19
  // calls, each resetting the compression); the 8 first calls remain.
  CHECK(original.report_frames.size() == 19);
  CHECK(original.reset_frames.size() == 19);
  CHECK(quiet.report_frames.size() == 8);
  CHECK(quiet.reset_frames.size() == 8);
  CHECK(std::includes(original.report_frames.begin(), original.report_frames.end(), quiet.report_frames.begin(),
                      quiet.report_frames.end()));
}

TEST_CASE("Quiet reacquire is the EXTENDED.CFG rule quiet_sonar_reacquire", "[quiet_sonar]") {
  CHECK_FALSE(MissionRules{}.quiet_sonar_reacquire);
  CHECK(parse_mission_rules_config("rule.quiet_sonar_reacquire = 1\n").quiet_sonar_reacquire);
  MissionRules rules;
  rules.quiet_sonar_reacquire = true;
  CHECK(decode_mission_rules(encode_mission_rules(rules)) == rules);
}

}  // namespace sh
