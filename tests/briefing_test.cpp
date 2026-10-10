// The MISSION BRIEFING / PATROL BRIEFING screen: its text for a historical
// scenario, a generated encounter and a career patrol, and the dialog
// shown between the menus and the mission.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/exe_image.h"
#include "assets/patrol_zones.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_briefing.h"
#include "game/mission.h"
#include "game/options_realism.h"
#include "game/patrol.h"
#include "game/saves_options.h"
#include "game/scengen.h"
#include "game/shpath.h"
#include "game_data.h"
#include "ui/menu_skin.h"

namespace sh {
namespace {

// Returns the lines of a briefing text, each run of the filler glyphs 1
// and 2 of `menu_pad_text_to_width` written as '|'.
std::vector<std::string> briefing_lines(const std::string& text) {
  std::vector<std::string> lines(1);
  for (const char c : text) {
    if (c == '\n') {
      lines.emplace_back();
    } else if (c == 1 || c == 2) {
      if (!lines.back().ends_with('|')) {
        lines.back() += '|';
      }
    } else {
      lines.back() += c;
    }
  }
  return lines;
}

TEST_CASE("HIST01's MISSION BRIEFING lists the scenario's start and the boat's load", "[briefing]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const ExeImage exe = ExeImage::load(install);
  const ui::MenuResources menu = ui::MenuResources::load(install);
  const std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  const MissionBriefing facts = mission_briefing_facts(*mission);
  CHECK(mission_briefing_title(facts, exe) == "MISSION BRIEFING");
  const std::string text = mission_briefing_text(facts, exe, menu.small);
  // HIST01.SCN: WORLD-8.WLD 1944-02-16T16:00 weather=0 sea=0, a
  // Balao (SS-305) Skate, veteran crew, 15 ft, heading 217, ahead full.
  const std::vector<std::string> expected = {
      "Difficulty: |" + std::to_string(mission->difficulty_rating() / 10) + "%",
      "",
      "Date: |February 16, 1944",
      "Time: |16:00",
      "Sub: |(SS-305) Skate",
      "Class: |BALAO",
      "Crew: |Veteran",
      "Patrol Area:|Caroline Islands",
      "Position: |8-43N, 146-41E",
      "Heading: |217",
      "Speed: |18 knots",
      "Depth: |15 feet",
      "",
      "Weather: |Clear",
      "Sea:|Calm",
      "Sunrise:|06:02",
      "Sunset:|17:55",
      "Moon:|Quarter",
      "Moonrise:|23:59",
      "Moonset:|11:16",
      "",
      "Equipment:",
      "",
      // Ten tubes and 14 reloads.
      "|MARK 14 Torpedo (24)",
      "|SD Radar",
      "|Improved SJ Radar (SJ-1)",
      "|Plan Position Indicator (PPI)",
      "|Bathythermograph",
      "",
      "***END OF BRIEFING***",
  };
  CHECK(briefing_lines(text) == expected);
}

TEST_CASE("A generated convoy mission's briefing shows the chosen boat and date", "[briefing]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const ExeImage exe = ExeImage::load(install);
  const ui::MenuResources menu = ui::MenuResources::load(install);
  GeneratorParams params;
  params.day = 15;
  params.month = 5;  // June 1943
  params.year = 13;
  params.time = 1;  // day
  params.sub_class = "GATO";
  WatcomRandom random(1234);
  const MissionStartDraws draws = MissionStartDraws::draw(random);
  GeneratedMission generated = generate_single_mission(install, tables, MissionType::kConvoy, params, 0, random);
  MissionSetup setup;
  setup.random = random;
  const std::unique_ptr<Mission> mission = Mission::create(install, tables, std::move(generated), draws, setup);
  const MissionBriefing facts = mission_briefing_facts(*mission);
  CHECK(mission_briefing_title(facts, exe) == "MISSION BRIEFING");
  const std::vector<std::string> lines = briefing_lines(mission_briefing_text(facts, exe, menu.small));
  REQUIRE(lines.size() > 12);
  CHECK(lines[2] == "Date: |June 15, 1943");
  CHECK(lines[5] == "Class: |GATO");
  CHECK(lines[6] == "Crew: |Green");
  CHECK(lines[8].starts_with("Position: |"));
  CHECK(lines[13].starts_with("Weather: |"));
  CHECK(lines.back() == "***END OF BRIEFING***");
}

// A career in port in June 1943 with a Balao (as career_patrol_test).
Career test_career() {
  Career c;
  CareerRecord& r = c.record;
  r.name = "Tester";
  r.month = 5;
  r.day = 1;
  r.year = 13;
  r.status = CareerStatus::kInPort;
  r.boat = "(SS-337) Carbonero";
  r.class_name = "BALAO";
  r.rank = 1;
  // SD, SJa, SJ-1, PPI, bathythermograph.
  r.equipment = 0x6f;
  r.home_port = "Darwin";
  return c;
}

TEST_CASE("A career patrol's PATROL BRIEFING gives the fuel, not the difficulty or the sky", "[briefing]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const ExeImage exe = ExeImage::load(install);
  const ui::MenuResources menu = ui::MenuResources::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  Career career = test_career();
  WatcomRandom random(99);
  PatrolPlan plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
  Patrol patrol(install, tables, career, std::move(plan));
  MissionSetup setup;
  setup.options = option::kModeCareer | RealismFile::kDefaultBits;
  setup.random = random;
  const std::unique_ptr<Mission> mission = patrol.create_mission(std::move(setup));
  const MissionBriefing facts = mission_briefing_facts(*mission);
  CHECK(mission_briefing_title(facts, exe) == "PATROL BRIEFING");
  const std::vector<std::string> lines = briefing_lines(mission_briefing_text(facts, exe, menu.small));
  REQUIRE(lines.size() > 12);
  CHECK(lines[0] == "Date: |June 1, 1943");
  CHECK(lines[2] == "Sub: |(SS-337) Carbonero");
  CHECK(lines[5] == "Patrol Area:|Malaysia");
  const std::int32_t fuel = mission->player().fuel();
  const std::int32_t capacity = static_cast<std::int32_t>(mission->player_class().fuel_capacity);
  CHECK(lines[7] == "Fuel: |" + std::to_string(fuel) + " gallons  (" + std::to_string(fuel * 100 / capacity) + "%)");
  CHECK(lines[8].starts_with("Heading: |"));
  CHECK(lines[11].empty());
  CHECK(lines[12] == "Equipment:");
  for (const std::string& l : lines) {
    CHECK_FALSE(l.starts_with("Difficulty"));
    CHECK_FALSE(l.starts_with("Weather"));
    CHECK_FALSE(l.starts_with("Sunrise"));
  }
  CHECK(lines.back() == "***END OF BRIEFING***");
}

TEST_CASE("The patrol text drops the single mission's lines; equipment follows the option bits", "[briefing]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const ui::MenuResources menu = ui::MenuResources::load(install);
  MissionBriefing b;
  b.options = option::kModeCareer | (1u << 16);  // SJa only
  b.day = 3;
  b.month = 11;
  b.year = 11;
  b.hour = 7;
  b.minute = 5;
  b.sub = "(SS-212) Gato";
  b.class_name = "GATO";
  b.crew = 2;
  b.patrol_area = "Japan";
  b.latitude = {.degrees = Fixed16(), .whole_degrees = 33, .minutes = 4, .hemisphere = 'N'};
  b.longitude = {.degrees = Fixed16(), .whole_degrees = 135, .minutes = 30, .hemisphere = 'E'};
  b.fuel = 75;
  b.fuel_capacity = 100;
  b.heading = Angle10(45);  // 4.5 degrees
  b.speed = Fixed16::from_raw(-0x28000);  // -2.5 knots: truncated towards zero
  b.depth_ft = 60;
  b.torpedoes = {{"MARK 10", 3}, {"MARK 14", 1}};
  CHECK(briefing_lines(mission_briefing_text(b, exe, menu.small)) ==
        std::vector<std::string>{"Date: |December 3, 1941", "Time: |07:05", "Sub: |(SS-212) Gato", "Class: |GATO",
                                 "Crew: |Elite", "Patrol Area:|Japan", "Position: |33-04N, 135-30E",
                                 "Fuel: |75 gallons  (75%)", "Heading: |004", "Speed: |-2 knots", "Depth: |60 feet", "",
                                 "Equipment:", "", "|MARK 10 Torpedo (3)", "|MARK 14 Torpedo (1)",
                                 // The absent ones pad the line of the next one.
                                 "|SJ Radar (SJa)", "|", "***END OF BRIEFING***"});
}

// --- The screen ----------------------------------------------------------------

// A scratch directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_briefing_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A host replaying clicks and keys, one every 40 frames, whose missions
// are built and briefed like the application's (the briefing, then the
// mission started).
class BriefingHost final : public FrontendHost, public MissionRunner {
 public:
  struct Step {
    std::uint16_t key = 0;  // a key, or a click at (x, y)
    int x = 0;
    int y = 0;
    bool back = false;  // Android's Back instead
  };

  BriefingHost(const Install& install, const GameTables& tables) : install_(install), tables_(tables) {}

  FrontEnd* front_end = nullptr;
  std::deque<Step> script;
  // The steps left when each mission started (after its briefing).
  std::vector<std::size_t> started_with;
  // The frame shown with the briefing open (when the first step after the
  // menus was replayed).
  Image8 briefing_frame{kScreenWidth, kScreenHeight};
  std::size_t capture_at = 0;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8& screen, const VgaPalette&) override {
    tick_ += 2;
    last_ = &screen;
    if (++frames_ > 50'000) {
      throw std::runtime_error("the script did not end the front end");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 40 || script.empty()) {
      return;
    }
    idle_ = 0;
    if (script.size() == capture_at && last_ != nullptr) {
      briefing_frame = *last_;
    }
    const Step s = script.front();
    script.pop_front();
    if (s.back) {
      gui.mouse().post_back(0);
    } else if (s.key != 0) {
      gui.mouse().post_key(s.key, 0);
    } else {
      gui.mouse().update(s.x, s.y, ui::kButtonLeft, 0);
      gui.mouse().update(s.x, s.y, 0, 0);
    }
  }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view) override {}
  std::int32_t run(const MissionRequest& request) override {
    MissionSetup setup;
    setup.options = request.options;
    setup.random = request.random;
    std::string path = request.scenario_path;
    std::replace(path.begin(), path.end(), '\\', '/');
    const std::unique_ptr<Mission> mission = Mission::load(install_, tables_, path, std::move(setup));
    front_end->mission_briefing_screen(*mission);
    started_with.push_back(script.size());
    return 1;  // aborted: no results
  }
  MissionOutcome outcome() const override { return {}; }

 private:
  const Install& install_;
  const GameTables& tables_;
  const Image8* last_ = nullptr;
  std::uint32_t tick_ = 0;
  int idle_ = 0;
  int frames_ = 0;
};

// Screen positions of the buttons (dialog position + control).
constexpr BriefingHost::Step kSingleMission{0, 20 + 35 + 90, 43 + 130 + 17};
constexpr BriefingHost::Step kExitToDos{0, 20 + 35 + 90, 43 + 330 + 17};
constexpr BriefingHost::Step kHistoric{0, 69 + 38 + 90, 59 + 125 + 17};
constexpr BriefingHost::Step kMainMenu{0, 69 + 38 + 90, 59 + 299 + 17};
constexpr BriefingHost::Step kBegin{0, 69 + 335 + 60, 19 + 395 + 17};
// The briefing dialog at (0x14, 0x1e): OK at (0x55, 0x172), the down
// arrow at (0xf3, 0x14a).
constexpr BriefingHost::Step kBriefingOk{0, 0x14 + 0x55 + 60, 0x1e + 0x172 + 12};
constexpr BriefingHost::Step kBriefingDown{0, 0x14 + 0xf3 + 16, 0x1e + 0x14a + 10};

TEST_CASE("BEGIN on a historical mission shows its briefing; OK starts the mission", "[briefing]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const GameTables tables = GameTables::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  BriefingHost host(install, tables);
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  host.front_end = &front_end;

  SECTION("OK") {
    // The arrows scroll without closing it; OK does, and the mission runs.
    host.script = {kSingleMission, kHistoric, kBegin, kBriefingDown, kBriefingDown, kBriefingOk, kMainMenu,
                   kExitToDos};
    host.capture_at = 5;
    front_end.run();
    CHECK(host.script.empty());
    REQUIRE(host.started_with.size() == 1);
    CHECK(host.started_with.front() == 2);
    // The dialog over the loading picture: its frame's top-left corner and
    // the text box's white background.
    const Image8& f = host.briefing_frame;
    CHECK(f.at(0x14 + 0x12 + 10, 0x1e + 0x84 + 3) != f.at(5, 5));
    CHECK(f.at(0x14 + 0x12 + 190, 0x1e + 0x84 + 0xd0) == f.at(0x14 + 0x12 + 10, 0x1e + 0x84 + 3));
  }

  SECTION("Back closes it like OK") {
    const BriefingHost::Step back{0, 0, 0, true};
    host.script = {kSingleMission, kHistoric, kBegin, back, back, back};
    front_end.run();
    CHECK(host.script.empty());
    REQUIRE(host.started_with.size() == 1);
    CHECK(host.started_with.front() == 2);
  }

  SECTION("Alt-Q asks EXIT TO DOS? over the briefing") {
    host.script = {kSingleMission, kHistoric, kBegin, BriefingHost::Step{0x1000, 0, 0},
                   BriefingHost::Step{0x1579, 0, 0}};
    CHECK_THROWS_AS(front_end.run(), QuitRequested);
    CHECK(host.started_with.empty());
  }
}

}  // namespace
}  // namespace sh
