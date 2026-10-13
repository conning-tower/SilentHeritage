// Golden traces of the classic game: scripted missions with a fixed seed
// whose simulation, generator, crew messages, UI state, station pictures
// and save bytes are compared with the traces recorded from main
// (golden_trace.h): on the original's data (SH_DATA) those of
// tests/golden/real/, tied to the install they were recorded on (on other
// data the tests skip); without it, on the synthetic install, those of
// tests/golden/synthetic/ (`golden_data`). The traces hold hashes only.
//
// The extended build runs them with every improvement off and must match;
// with each improvement on, the simulation's part of the trace must match
// unless the improvement is declared to change it (`kFlagExpectations`).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "assets/scenario.h"
#include "core/features.h"
#include "core/fixed_math.h"
#include "game/mission.h"
#include "game/mission_save.h"
#include "game/scengen.h"
#include "game_data.h"
#include "golden_flags.h"
#include "golden_rig.h"
#include "golden_trace.h"
#include "test_cases.h"
#include "sim/geometry.h"
#include "sim/sensor_environment.h"

namespace sh::test {
namespace {

// A scripted run: builds its mission on `game` and plays it on `rig`.
struct GoldenScript {
  std::string name;
  std::function<std::unique_ptr<Mission>(const GoldenGame&)> mission;
  std::function<void(GoldenMissionRig&)> play;
};

// The station keys, in the order the tour visits them.
struct StationKey {
  std::string_view key;
  std::string_view name;
};
constexpr StationKey kTour[] = {{"f2", "scope"},  {"f3", "tbt"},    {"f4", "bridge"},  {"f5", "chart"},
                                {"f6", "tdc"},    {"f7", "gauges"}, {"f8", "status"},  {"f9", "radar"},
                                {"f10", "cabin"}, {"alt+f4", "deckgun"}, {"f1", "conn"}};

// Requests a time compression of 32 (the main loop takes a hotkey every
// other frame): with 5-tick frames the effective compression is 8, unless
// the game limits it.
constexpr std::string_view kCompression32 = "kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1";

// Runs `count` stretches of `frames` frames, each ending in a checkpoint
// `<prefix><i>`.
void stretches(GoldenMissionRig& rig, std::string_view prefix, int count, int frames) {
  for (int i = 1; i <= count && !rig.over(); ++i) {
    rig.frames(frames);
    rig.checkpoint(std::format("{}{}", prefix, i));
  }
}

MissionSetup seeded(std::uint32_t seed) {
  MissionSetup setup;
  setup.random = WatcomRandom(seed);
  return setup;
}

// HIST01 with the boat moved 1200 yd abeam of the first ship, facing it.
Scenario hist01_abeam(const Install& install) {
  Scenario scenario = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
  const ShipRecord& ship = scenario.ships.front();
  PlayerSubRecord& player = *scenario.player_sub;
  const double h = ship.heading * std::numbers::pi / 180;
  const double d = 1200.0 * kUnitsPerYard;
  player.x = ship.x + static_cast<std::int32_t>(d * std::cos(h));
  player.y = ship.y - static_cast<std::int32_t>(d * std::sin(h));
  player.heading = fx::heading_from_delta(ship.x - player.x, ship.y - player.y).tenths() / 10;
  return scenario;
}

// The generated convoy of June 1943 by day, generator seed 1234.
std::unique_ptr<Mission> generated_convoy(const GoldenGame& game) {
  GeneratorParams params;
  params.day = 15;
  params.month = 5;
  params.year = 13;
  params.time = 1;
  const std::vector<const SubClass*> subs = sub_classes_in_service(game.tables, params.month, params.year);
  REQUIRE_FALSE(subs.empty());
  params.sub_class = subs.back()->index_name;
  WatcomRandom random(1234);
  const MissionStartDraws draws = MissionStartDraws::draw(random);
  GeneratedMission generated =
      generate_single_mission(game.install, game.tables, MissionType::kConvoy, params, 0, random);
  MissionSetup setup;
  setup.random = random;
  return Mission::create(game.install, game.tables, std::move(generated), draws, setup);
}

const std::vector<GoldenScript>& scripts() {
  static const std::vector<GoldenScript> list = {
      // Every station in turn, then orders and time compression.
      {"hist01_tour",
       [](const GoldenGame& g) { return Mission::load(g.install, g.tables, "SCEN/HIST01.SCN", seeded(7)); },
       [](GoldenMissionRig& rig) {
         rig.play("-60");
         rig.checkpoint("start");
         for (const StationKey& s : kTour) {
           rig.play(std::format("{},-40", s.key));
           rig.checkpoint(s.name);
         }
         rig.play(std::format("4,-1,right,-1,right,-20,{}", kCompression32));
         stretches(rig, "run", 6, 200);
         rig.save_checkpoint("end");
       }},
      // A torpedo fired from the periscope at the first ship, followed on
      // the periscope, the bridge and the chart.
      {"hist01_attack",
       [](const GoldenGame& g) { return Mission::create(g.install, g.tables, hist01_abeam(g.install), seeded(11)); },
       [](GoldenMissionRig& rig) {
         rig.play("-60,f2,-2");
         Mission& m = rig.mission();
         const Ship& ship = *m.ships().front();
         const fx::Vec3 p = m.player().position();
         const fx::Vec3 s = ship.motion().position;
         m.player_combat().view_bearing =
             fx::angle_sub(fx::heading_from_delta(s.x - p.x, s.z - p.z), m.player().heading());
         rig.play("space,-300");
         rig.checkpoint("marked");
         rig.play("alt+1,-1");
         rig.checkpoint("fired");
         stretches(rig, "scope", 10, 100);
         rig.play("f4,-100");
         rig.checkpoint("bridge");
         rig.play("f5,-100");
         rig.checkpoint("chart");
         rig.save_checkpoint("end");
       }},
      // A generated convoy at the chart, at full speed under time
      // compression, then a crash dive.
      {"convoy_generated", generated_convoy,
       [](GoldenMissionRig& rig) {
         rig.play(std::format("-60,f5,-20,4,-1,{}", kCompression32));
         stretches(rig, "run", 8, 250);
         rig.play("C,-1");
         stretches(rig, "dive", 4, 200);
         rig.save_checkpoint("end");
       }},
      // A mission saved, then restored as CONTINUE MISSION restores it
      // and played on under time compression.
      {"hist05_restore",
       [](const GoldenGame& g) { return Mission::load(g.install, g.tables, "SCEN/HIST05.SCN", seeded(5)); },
       [](GoldenMissionRig& rig) {
         rig.play("-200");
         rig.checkpoint("saved");
         const MissionSaveFile save = rig.save_checkpoint("saved");
         GoldenMissionRig restored(rig.game(),
                                   restore_mission(rig.game().install, rig.game().tables, save, seeded(5)),
                                   rig.features());
         restored.trace() = std::move(rig.trace());
         restored.play("-2");
         restored.checkpoint("restored");
         restored.play(std::format("3,-1,{}", kCompression32));
         stretches(restored, "run", 4, 250);
         restored.save_checkpoint("end");
         rig.trace() = std::move(restored.trace());
       }},
  };
  return list;
}

void run_script(const GoldenGame& game, const GoldenScript& script, Features features, GoldenTrace& out) {
  GoldenMissionRig rig(game, script.mission(game), features);
  script.play(rig);
  out = rig.trace();
}

// Checks that every script played with `features` keeps the simulation of
// its golden trace.
void check_simulation_kept(const Features& features) {
  const GoldenData data = golden_data();
  const GoldenGame game(data.install);
  for (const GoldenScript& script : scripts()) {
    DYNAMIC_SECTION(data.trace_name(script.name)) {
      GoldenTrace trace;
      run_script(game, script, features, trace);
      check_golden(data.trace_name(script.name), trace, kSimKinds, data.header);
    }
  }
}

TEST_CASE("The classic game follows its golden traces", "[golden]") {
  const GoldenData data = golden_data();
  const GoldenGame game(data.install);
  for (const GoldenScript& script : scripts()) {
    DYNAMIC_SECTION(data.trace_name(script.name)) {
      GoldenTrace trace;
      run_script(game, script, Features::classic(), trace);
      check_golden(data.trace_name(script.name), trace, {}, data.header);
    }
  }
}

// One test case per improvement and per command bar layout, run in
// parallel by CTest.
[[maybe_unused]] const bool kFlagCases = register_test_cases(
    simulation_keeping_flags(),
    [](const FlagExpectation& e) {
      return "Each improvement alone keeps the simulation of the golden traces: " + std::string(e.name);
    },
    "[golden][features]",
    [](const FlagExpectation& e) {
      Features features;
      features.*e.flag = true;
      check_simulation_kept(features);
    });

struct BarStyle {
  std::string_view name;
  CommandBarStyle style;
};
constexpr BarStyle kBarStyles[] = {{"original", CommandBarStyle::kOriginal},
                                   {"bar_orders", CommandBarStyle::kBarOrders},
                                   {"bar_orders_time", CommandBarStyle::kBarOrdersTime}};
[[maybe_unused]] const bool kBarCases = register_test_cases(
    kBarStyles,
    [](const BarStyle& s) {
      return "Each command bar layout keeps the simulation of the golden traces: " + std::string(s.name);
    },
    "[golden][features]",
    [](const BarStyle& s) {
      Features features;
      features.command_bar = s.style;
      check_simulation_kept(features);
    });

// The radar rules (sim/context.h) with nothing to act on: the ST's rule
// (the boats are not fitted with it) and the APR-1's (the APR-1 bit
// cleared) must keep the simulation of the golden traces.
constexpr std::pair<std::string_view, bool MissionRules::*> kDormantRules[] = {
    {"st_radar", &MissionRules::st_radar},
    {"apr1", &MissionRules::apr1},
};

TEST_CASE("The radar rules keep the simulation of the golden traces without their fits", "[golden][rules]") {
  const GoldenData data = golden_data();
  const GoldenGame game(data.install);
  for (const auto& [name, rule] : kDormantRules) {
    for (const GoldenScript& script : scripts()) {
      DYNAMIC_SECTION(name << " " << data.trace_name(script.name)) {
        std::unique_ptr<Mission> mission = script.mission(game);
        MissionConditions& c = mission->sim().conditions();
        c.options &= ~(sensor_option::kApr1Detector | sensor_option::kStPeriscopeRadar);
        c.rules.*rule = true;
        GoldenMissionRig rig(game, std::move(mission));
        script.play(rig);
        check_golden(data.trace_name(script.name), rig.trace(), kSimKinds, data.header);
      }
    }
  }
}

}  // namespace
}  // namespace sh::test
