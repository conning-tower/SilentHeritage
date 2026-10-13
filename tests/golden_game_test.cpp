// Golden traces of the whole classic program: the front end's menus, a
// single mission launched from them, a career and the scenario editor,
// played by scripts (golden_game_rig.h) on the original's data or the
// synthetic install (`golden_data`) and compared with tests/golden/
// (golden_trace.h).

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "core/features.h"
#include "game_data.h"
#include "golden_flags.h"
#include "golden_game_rig.h"
#include "golden_trace.h"
#include "test_cases.h"

namespace sh::test {
namespace {

struct GameScript {
  std::string name;
  std::string steps;
};

// Steps of the scripts: the buttons of the menus (640x480 points) and
// recurring sequences.
constexpr std::string_view kSingleMission = "click:145:190,-40";
constexpr std::string_view kCareerMenu = "click:145:230,-80";
constexpr std::string_view kInterviews = "click:145:270,-80";
constexpr std::string_view kSubTour = "click:145:310,-80";
constexpr std::string_view kHistoric = "click:197:201,-60";
constexpr std::string_view kConvoy = "click:420:201,-60";
constexpr std::string_view kBuildCustom = "click:197:321";
constexpr std::string_view kSingleToMain = "click:197:375,-40";
constexpr std::string_view kListCancel = "click:163:430,-40";
constexpr std::string_view kListBegin = "click:464:431,-80";
constexpr std::string_view kBriefingOk = "click:157:412";
// The mission's END MISSION box (Esc): ABORT then YES, or RETURN TO BASE.
constexpr std::string_view kAbort = "esc,-20,click:320:265,-30,click:375:294";
constexpr std::string_view kReturnToBase = "esc,-20,click:320:289,-30";
// Time compression requested to 32, then to 256 (the jump of an
// unrestricted cap).
constexpr std::string_view kCompression32 = "kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1";
constexpr std::string_view kCompression256 = "kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1,kpplus,-1";
// The scenario editor's menu bar (right click) and its Files items.
constexpr std::string_view kEditorFiles = "rclick:320:200,-10,click:70:13,-10";

std::string join(std::initializer_list<std::string_view> parts) {
  std::string out;
  for (const std::string_view p : parts) {
    out += out.empty() ? "" : ",";
    out += p;
  }
  return out;
}

const std::vector<GameScript>& game_scripts() {
  static const std::vector<GameScript> list = {
      // The menus: the scenario lists and settings, the extras, the career
      // menu.
      {"frontend_menus",
       join({"-40,@main", kSingleMission, "@single", kHistoric, "@historic", "click:300:193,-20,@historic_item",
             kListCancel, kConvoy, "@convoy", "click:206:278,-20,@convoy_crew", kListBegin, "@subclass",
             "click:469:286,-20,@subclass_balao", "click:163:403,-40", kListCancel, "@single_back", kSingleToMain,
             kInterviews, "@interviews", "click:80:386,-40,@interviews_next", "click:320:446,-40", kSubTour,
             "@subtour", "click:145:230,-40,@subtour_room", "click:134:401,-40,@subtour_text", "click:134:442,-40",
             kCareerMenu, "@career", "click:480:342,-40,@main_back"})},
      // HIST01 launched from its list: every station's first frames, a
      // run under time compression, then ABORT.
      {"frontend_hist01",
       join({"-40", kSingleMission, kHistoric, kListBegin, "@briefing", kBriefingOk, "until:mission,-60,@conn",
             "f2,-40,@scope", "f4,-40,@bridge", "f5,-40,@chart", "f8,-40,@status", "f1,-40,@conn2", "4,-1",
             kCompression32, "-300,@run1", "-300,@run2", kAbort, "until:menu,-40,@single"})},
      // A generated convoy (BALAO) played at the chart, then RETURN TO
      // BASE: the port, the results and the single-mission menu.
      {"frontend_convoy",
       join({"-40", kSingleMission, kConvoy, kListBegin, "click:469:286,-20,click:472:403,-80,@briefing",
             kBriefingOk, "until:mission,-60,@conn", "f5,-40,4,-1", kCompression32, "-200,@run1", "-200,@run2",
             "-200,@run3", kReturnToBase, "@port", "-100,enter,-100,@results", "enter,-100,@single"})},
      // A new career to its first patrol: the orders, the boat, the base;
      // the patrol under time compression, SAVE CAREER from the game
      // options, RETURN TO BASE, the results, the letter, new equipment.
      {"career",
       join({"-40", kCareerMenu, "click:480:222,-80,@start", "click:460:289,-10,g,o,l,d,enter,-20,@home_base",
             "click:545:432,-100,@new_submarine", "enter,-100,@base", "click:134:206,-100,@patrol_area",
             "click:531:376,-400,@departure", "enter,-100,@briefing", "click:165:417,until:mission,-60,@conn",
             "f5,-40,4,-1", kCompression256, "-500,@run1", "-500,@run2", "-500,@run3",
             "alt+o,-30,@options", "click:318:410,-30,@save_name", "s,a,v,e,d,enter,-60,@saved", kReturnToBase,
             "@port", "-100,enter,-100,@results", "enter,-100,@letter", "enter,-100,@equipment",
             "enter,-100,@base_back"})},
      // BUILD CUSTOM MISSION: the editor's new scenario, a historical one
      // loaded and saved, then File / Run: the mission played and aborted.
      {"editor",
       join({"-40", kSingleMission, kBuildCustom, "until:editor,-20,@new", kEditorFiles, "@files",
             "click:155:94,-30,@load_historical", "click:250:209,-10,click:417:386,-60,@loaded", kEditorFiles,
             "click:155:142,-30,@save_scenario", "g,o,l,d,enter,-30,@saved", kEditorFiles,
             "click:155:46,-30,@realism", "click:477:442,-60,@briefing", kBriefingOk, "until:mission,-60,@conn",
             "-200,@run", kAbort, "until:menu,-60,@single"})},
  };
  return list;
}

TEST_CASE("The classic program follows its golden traces", "[golden]") {
  const GoldenData data = golden_data();
  for (const GameScript& script : game_scripts()) {
    DYNAMIC_SECTION(data.trace_name(script.name)) {
      GoldenGameRig rig(data.install);
      rig.run(script.steps);
      check_golden(data.trace_name(script.name), rig.trace(), {}, data.header);
    }
  }
}

// One test case per improvement, run in parallel by CTest.
[[maybe_unused]] const bool kFlagCases = register_test_cases(
    simulation_keeping_flags(),
    [](const FlagExpectation& e) {
      return "Each improvement alone keeps the simulation of the program's golden traces: " + std::string(e.name);
    },
    "[golden][features]",
    [](const FlagExpectation& e) {
      const GoldenData data = golden_data();
      for (const GameScript& script : game_scripts()) {
        DYNAMIC_SECTION(data.trace_name(script.name)) {
          Features features;
          features.*e.flag = true;
          GoldenGameRig rig(data.install, features);
          rig.run(script.steps);
          check_golden(data.trace_name(script.name), rig.trace(), kProgramSimKinds, data.header);
        }
      }
    });

// Runs the script of SH_GOLDEN_SCRIPT and prints its trace, for writing
// new scripts (with SH_SCREENS for the pictures of the checkpoints).
TEST_CASE("A golden script given in the environment runs", "[.golden_script]") {
  const char* steps = std::getenv("SH_GOLDEN_SCRIPT");
  REQUIRE(steps != nullptr);
  GoldenGameRig rig(golden_data().install);
  rig.run(steps);
  for (const GoldenLine& l : rig.trace().lines()) {
    std::printf("%s %s %s\n", l.checkpoint.c_str(), l.kind.c_str(), l.value.c_str());
  }
}

}  // namespace
}  // namespace sh::test
