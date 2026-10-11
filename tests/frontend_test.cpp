// The front end: the option files, the realism rating, the scenario lists,
// the menus driven by synthetic input up to the mission request, and the
// popup menus.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/exe_image.h"
#include "core/error.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/frontend_best.h"
#include "game/options_realism.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game_data.h"
#include "ui/controls.h"
#include "ui/popup_menu.h"

namespace sh {
namespace {

// A scratch directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_frontend_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& p) { return read_file(p); }

// --- Option files -----------------------------------------------------------

TEST_CASE("Option files round-trip through their binary formats", "[frontend]") {
  GameOptions g;
  g.auto_sd_radar = kOptionOff;
  g.effects_volume = 12;
  g.chart_follow_sub = kOptionOn;
  const std::vector<std::uint8_t> gb = g.to_bytes();
  CHECK(gb.size() == 45);
  CHECK(gb[0] == 3);
  GameOptions g2;
  REQUIRE(g2.read_from(gb));
  CHECK(g2 == g);

  DetailOptions d;
  d.detail_bits = 0x53;
  d.ship_wakes = kOptionOff;
  DetailOptions d2;
  REQUIRE(d2.read_from(d.to_bytes()));
  CHECK(d2 == d);
  CHECK(d.to_bytes().size() == 13);

  EncounterSettings e;
  e.year = 13;
  e.month = 4;
  e.special_mission = 2;
  EncounterSettings e2;
  REQUIRE(e2.read_from(e.to_bytes()));
  CHECK(e2 == e);
  CHECK(e.to_bytes().size() == 57);

  // REALISM.SAV keeps only the realism bits; the mode and upper bits stay.
  const std::uint32_t options = 0x30000u | 0x1u | 0x4u | 0x10u | 0x80u | 0x200u | 0x2000u | 0x8000u;
  std::uint32_t read_back = 0x40002;
  REQUIRE(RealismFile::read_into(RealismFile::to_bytes(options), read_back));
  CHECK(read_back == (0x40002u | (options & option::kRealismMask)));
}

TEST_CASE("Option files keep the fields read before a short read and refuse other versions", "[frontend]") {
  GameOptions g;
  // Version 3 with only auto torp load and auto SJ radar present.
  const std::vector<std::uint8_t> short_file{3, 0, 0, 0, 0, 0, 0, 0, 0, 7};
  REQUIRE(g.read_from(short_file));
  CHECK(g.auto_torp_load == 0);
  CHECK(g.auto_sj_radar == 0);
  CHECK(g.auto_sd_radar == kOptionOn);
  CHECK(g.effects_volume == 100);

  // GOPTION.SAV must be version 3: an older file is ignored whole.
  GameOptions old;
  CHECK_FALSE(old.read_from(std::vector<std::uint8_t>{2, 0, 0, 0, 0}));
  CHECK(old == GameOptions{});
  CHECK_FALSE(old.read_from(std::vector<std::uint8_t>{}));
}

TEST_CASE("A missing option file is written with the defaults", "[frontend]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  const EncounterSettings e = EncounterSettings::load(saves);
  CHECK(e == EncounterSettings{});
  CHECK(e.year == 11);
  CHECK(e.month == 11);
  CHECK(read_bytes(dir.path() / "SMISSION.SAV") == EncounterSettings{}.to_bytes());

  std::uint32_t options = 0x10001;
  RealismFile::load(saves, options);
  CHECK(options == (0x10001u | RealismFile::kDefaultBits));
  CHECK(std::filesystem::exists(dir.path() / "REALISM.SAV"));

  const DetailOptions d = DetailOptions::load(saves);
  CHECK(d.detail_bits == 0x7f);
  CHECK(read_bytes(dir.path() / "GDETAIL.SAV") == DetailOptions{}.to_bytes());
}

TEST_CASE("The shipped option files read back byte for byte", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));

  const GameOptions g = GameOptions::load(saves);
  CHECK(g.auto_torp_load == kOptionOn);
  CHECK(g.collision_alarm == kOptionOn);
  CHECK(g.effects_volume == 88);
  CHECK(g.speech_volume == 101);
  CHECK(g.auto_red_lights == kOptionOn);
  CHECK(g.chart_follow_sub == 0);
  CHECK(g.to_bytes() == install.read("SAVE/GOPTION.SAV"));

  const DetailOptions d = DetailOptions::load(saves);
  CHECK(d.detail_bits == 127);
  CHECK(d.torpedo_wakes == kOptionOn);
  CHECK(d.to_bytes() == install.read("SAVE/GDETAIL.SAV"));

  const EncounterSettings e = EncounterSettings::load(saves);
  CHECK(e.year == 14);
  CHECK(e.month == 7);
  CHECK(e.time == 3);
  CHECK(e.sea == 1);
  CHECK(e.escort == 2);
  CHECK(e.crew == 2);
  CHECK(e.warship_type == 1);
  CHECK(e.special_mission == 1);
  CHECK(e.to_bytes() == install.read("SAVE/SMISSION.SAV"));

  std::uint32_t options = 0;
  RealismFile::load(saves, options);
  CHECK(options == 0x61a8);  // the EXPERT preset
  CHECK(RealismFile::to_bytes(options) == install.read("SAVE/REALISM.SAV"));

  // Nothing was written: every file came from the install.
  CHECK(std::filesystem::is_empty(dir.path()));
}

// --- Realism -----------------------------------------------------------------

TEST_CASE("The realism rating follows the presets and toggles", "[frontend]") {
  // Values of the original's formula (realism_rating, 0xa2d57).
  const std::uint32_t novice = apply_realism_preset(0x10003, RealismPreset::kNovice);
  CHECK(novice == 0x11e43);
  CHECK(realism_rating(novice & ~3u) == 0x13a4);
  CHECK(realism_rating_percent(realism_rating(0x1e40)) == 8);
  const std::uint32_t intermediate = apply_realism_preset(0xffff, RealismPreset::kIntermediate);
  CHECK(intermediate == (0x11d4 | 3));
  CHECK(realism_rating_percent(realism_rating(0x11d4)) == 50);
  CHECK(realism_rating(0x61a8) == 0x10000);
  CHECK(realism_rating_percent(realism_rating(0x61ac)) == 115);

  // Combat wraps after expert, sailing after realistic.
  CHECK(combat_level(cycle_combat_level(0xc)) == 0);
  CHECK(combat_level(cycle_combat_level(0x4)) == 2);
  CHECK(sailing_model(cycle_sailing_model(0x20)) == 0);
  CHECK(sailing_model(cycle_sailing_model(0x10)) == 2);

  // The check boxes show "limited" for the unlimited bits clear.
  CHECK(kRealismToggles[0].checked(0));                         // LIMITED FUEL
  CHECK_FALSE(kRealismToggles[0].checked(option::kUnlimitedFuel));
  CHECK(kRealismToggles[3].checked(option::kLimitedVisibility));  // LIMITED VISIBILITY
  CHECK(kRealismToggles[7].checked(0));                         // VULNERABLE BOAT
}

TEST_CASE("Encounter dates stay within the war", "[frontend]") {
  EncounterSettings e;
  e.year = 11;
  e.month = 11;
  CHECK(e.date_in_war());
  e.month = 10;
  CHECK_FALSE(e.date_in_war());
  e.year = 15;
  e.month = 7;
  CHECK(e.date_in_war());
  e.month = 8;
  CHECK_FALSE(e.date_in_war());
}

// --- Scenario lists -----------------------------------------------------------

TEST_CASE("The historical and custom scenario lists", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  const std::vector<ScenarioListEntry> hist = scan_scenarios(install, paths, GameDir::kScen, "HIST???.SCN", false);
  REQUIRE(hist.size() == 60);
  CHECK(hist.front().file == "HIST01.SCN");
  CHECK(hist.back().file == "HIST60.SCN");
  CHECK(hist.front().title == "Bud Gruner and the Agano\n");
  CHECK(hist.front().description.starts_with("\nFebruary 16th, 1944\n\nU.S.S. Skate"));
  for (const ScenarioListEntry& e : hist) {
    CHECK(e.title.size() <= 49);
  }

  const std::vector<ScenarioListEntry> custom = scan_scenarios(install, paths, GameDir::kCustom, "*.SCN", true);
  REQUIRE(custom.size() == 1);
  CHECK(custom.front().title == "SITDUCK");
  CHECK(custom.front().description.starts_with("A Sitting Duck\n\nWe have"));

  // The wildcard's '?' matches one character or nothing at the end.
  CHECK(scan_scenarios(install, paths, GameDir::kScen, "HIST?.SCN", false).empty());
  CHECK(scan_scenarios(install, paths, GameDir::kScen, "HIST2?.SCN", false).size() == 10);
}

// --- Menus --------------------------------------------------------------------

// A host replaying clicks and keys, one every 40 frames (the screens' fades
// and waits take fewer), and recording the missions it is asked to run.
class ScriptedHost final : public FrontendHost, public MissionRunner {
 public:
  struct Step {
    std::uint16_t key = 0;  // a key, or a click at (x, y)
    int x = 0;
    int y = 0;
    bool back = false;  // Android's Back instead
  };
  static Step click(int x, int y) { return {0, x, y}; }
  static Step key(std::uint16_t k) { return {k, 0, 0}; }
  static Step back() { return {0, 0, 0, true}; }

  std::deque<Step> script;
  // The hotspots when each step was replayed: which screen was shown.
  using Screen = std::vector<std::array<int, 4>>;
  std::vector<Screen> screens;
  std::vector<MissionRequest> requests;
  std::vector<std::string> reports;
  int frames = 0;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8&, const VgaPalette&) override {
    ++frames;
    tick_ += 2;
    if (frames > 50'000) {
      throw std::runtime_error("the script did not end the front end");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 40 || script.empty()) {
      return;
    }
    idle_ = 0;
    const Step s = script.front();
    script.pop_front();
    Screen shown;
    for (std::size_t i = 0; i < gui.hotspots().size(); ++i) {
      const ui::Hotspot& h = gui.hotspots()[static_cast<int>(i)];
      shown.push_back({h.x0, h.y0, h.x1, h.y1});
    }
    screens.push_back(std::move(shown));
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
  void apply_sound_options(const GameOptions& options) override { sound_options = options; }
  void report(std::string_view message) override { reports.emplace_back(message); }
  std::int32_t run(const MissionRequest& request) override {
    requests.push_back(request);
    if (on_run) {
      on_run(request);
    }
    return mission.end_code;
  }
  // Called when a mission starts.
  std::function<void(const MissionRequest&)> on_run;
  MissionOutcome outcome() const override { return mission; }
  // How the missions end: aborted by default (no results).
  MissionOutcome mission = [] {
    MissionOutcome aborted;
    aborted.end_code = 1;
    return aborted;
  }();

  GameOptions sound_options;

 private:
  std::uint32_t tick_ = 0;
  int idle_ = 0;
};

// Screen positions of the buttons used below (dialog position + control).
constexpr ScriptedHost::Step kSingleMission = ScriptedHost::Step{0, 20 + 35 + 90, 43 + 130 + 17};
constexpr ScriptedHost::Step kExitToDos = ScriptedHost::Step{0, 20 + 35 + 90, 43 + 330 + 17};
constexpr ScriptedHost::Step kHistoric = ScriptedHost::Step{0, 69 + 38 + 90, 59 + 125 + 17};
constexpr ScriptedHost::Step kConvoy = ScriptedHost::Step{0, 69 + 261 + 90, 59 + 125 + 17};
constexpr ScriptedHost::Step kMainMenu = ScriptedHost::Step{0, 69 + 38 + 90, 59 + 299 + 17};
constexpr ScriptedHost::Step kBegin = ScriptedHost::Step{0, 69 + 335 + 60, 19 + 395 + 17};
constexpr ScriptedHost::Step kSubSelect = ScriptedHost::Step{0, 69 + 0x158 + 60, 39 + 0x15c + 12};
constexpr ScriptedHost::Step kCrewCycle = ScriptedHost::Step{0, 69 + 37 + 100, 19 + 243 + 16};

TEST_CASE("The menus lead to a historical mission request", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  host.script = {kSingleMission, kHistoric, kBegin, kMainMenu, kExitToDos};
  front_end.run();

  CHECK(host.script.empty());
  CHECK(host.sound_options.effects_volume == 88);
  REQUIRE(host.requests.size() == 1);
  const MissionRequest& r = host.requests.front();
  CHECK(r.type == MissionType::kScenario);
  CHECK(r.scenario_path == "SCEN\\HIST01.SCN");
  CHECK(r.options == 0x61a8);
  CHECK(r.game_options.speech_volume == 101);
}

TEST_CASE("The menus lead to a convoy encounter request", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  host.script = {kSingleMission, kConvoy, kCrewCycle, kBegin, kSubSelect, kMainMenu, kExitToDos};
  front_end.run();

  REQUIRE(host.requests.size() == 1);
  const MissionRequest& r = host.requests.front();
  CHECK(r.type == MissionType::kConvoy);
  const GeneratorParams& g = r.generator;
  // SMISSION.SAV, with the crew cycled from elite back to green.
  CHECK(g.year == 14);
  CHECK(g.month == 7);
  CHECK(g.time == 3);
  CHECK(g.sea == 1);
  CHECK(g.convoy_size == 1);
  CHECK(g.escort == 2);
  CHECK(g.enemy == 1);
  CHECK(g.crew == 0);
  CHECK(g.warship_type == 1);
  CHECK(g.special_mission == 1);
  // The day of August 1944 from the month table read by [month][year]:
  // entry 7 * 12 + 14 (31 days).
  CHECK(g.day >= 1);
  CHECK(g.day <= 31);
  // BEGIN saved the settings.
  EncounterSettings saved;
  REQUIRE(saved.read_from(read_bytes(dir.path() / "SMISSION.SAV")));
  CHECK(saved.crew == 0);
  CHECK(saved.escort == 2);
  // SELECT took the first class in service in August 1944.
  CHECK(g.sub_class == "S CLASS");
}

TEST_CASE("With last_boat, the SUB CLASS screen selects the class last chosen", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  // The seventh class in service in August 1944 (GATO), in the list at
  // (0x174, 0x7f) of the dialog at (69, 39); the row height of the MENU
  // skin's metrics (0x10c37e).
  const int row_height = exe.u16(0x10c37e);
  const ScriptedHost::Step gato_row =
      ScriptedHost::click(69 + 0x174 + 20, 39 + 0x7f + 1 + 6 * row_height + row_height / 2);
  // Runs two convoy encounters, GATO chosen for the first and SELECT
  // pressed at once for the second, the date moved to January 1942 (before
  // the Gato) in between when `before_gato`.
  const auto run = [&](bool last_boat, bool before_gato) {
    const TempDir dir;
    const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
    ScriptedHost host;
    Features features;
    features.last_boat = last_boat;
    FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves, .features = features}, host,
                       host, 1234);
    front_end.set_play_intro(false);
    if (before_gato) {
      host.on_run = [&saves](const MissionRequest&) {
        EncounterSettings e = EncounterSettings::load(saves);
        e.year = 12;
        e.month = 0;
        REQUIRE(e.save(saves));
      };
    }
    host.script = {kSingleMission, kConvoy, kBegin, gato_row, kSubSelect,
                   kConvoy,        kBegin,  kSubSelect, kMainMenu, kExitToDos};
    front_end.run();
    CHECK(host.script.empty());
    std::vector<std::string> classes;
    for (const MissionRequest& r : host.requests) {
      classes.push_back(r.generator.sub_class);
    }
    return classes;
  };
  CHECK(run(false, false) == std::vector<std::string>{"GATO", "S CLASS"});
  CHECK(run(true, false) == std::vector<std::string>{"GATO", "GATO"});
  CHECK(run(true, true) == std::vector<std::string>{"GATO", "S CLASS"});
}

TEST_CASE("Alt-Q asks EXIT TO DOS? and YES ends the game", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1);
  front_end.set_play_intro(false);
  // Alt-Q, then N (stay), Alt-Q again, then Y.
  host.script = {ScriptedHost::key(0x1000), ScriptedHost::key(0x316e), ScriptedHost::key(0x1000),
                 ScriptedHost::key(0x1579)};
  CHECK_THROWS_AS(front_end.run(), QuitRequested);
  CHECK(host.script.empty());
}

// A scenario editor whose File / Run writes a copy of SITDUCK as
// CUSTOM\AEONSCEN.TMP.
class FakeEditor final : public EditorRunner {
 public:
  FakeEditor(const SaveDir& custom, std::vector<std::uint8_t> scenario, EditorExit exit)
      : custom_(custom), scenario_(std::move(scenario)), exit_(exit) {}
  EditorExit run_editor() override {
    ++runs;
    if (exit_ == EditorExit::kRun) {
      custom_.write("AEONSCEN.TMP", scenario_);
    }
    return exit_;
  }
  int runs = 0;

 private:
  const SaveDir& custom_;
  std::vector<std::uint8_t> scenario_;
  EditorExit exit_;
};

constexpr ScriptedHost::Step kBuildCustom = ScriptedHost::Step{0, 69 + 38 + 90, 59 + 245 + 17};
constexpr ScriptedHost::Step kRealismOk = ScriptedHost::Step{0, 52 + 367 + 60, 10 + 415 + 12};

TEST_CASE("BUILD CUSTOM MISSION plays the scenario the editor ran, then deletes it", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path() / "SAVE", &install, paths.dir(GameDir::kSave));
  const SaveDir custom(dir.path() / "CUSTOM", &install, paths.dir(GameDir::kCustom));
  const std::vector<std::uint8_t> sitduck = install.read("CUSTOM/SITDUCK.SCN");
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves, .custom = &custom}, host, host,
                     1234);
  front_end.set_play_intro(false);

  SECTION("File / Run: the realism menu, the mission of AEONSCEN.TMP, the file deleted") {
    FakeEditor editor(custom, sitduck, EditorExit::kRun);
    front_end.set_editor(&editor);
    bool file_read = false;
    host.on_run = [&](const MissionRequest& request) {
      CHECK(read_scenario_file(install, paths, &custom, request.scenario_path) == sitduck);
      file_read = true;
    };
    host.script = {kSingleMission, kBuildCustom, kRealismOk, kMainMenu, kExitToDos};
    front_end.run();
    CHECK(host.script.empty());
    CHECK(editor.runs == 1);
    REQUIRE(host.requests.size() == 1);
    CHECK(host.requests.front().type == MissionType::kScenario);
    CHECK(host.requests.front().scenario_path == "CUSTOM\\AEONSCEN.TMP");
    CHECK(file_read);
    CHECK_FALSE(std::filesystem::exists(dir.path() / "CUSTOM" / "AEONSCEN.TMP"));
  }
  SECTION("File / Exit: back to the menu, no mission") {
    FakeEditor editor(custom, sitduck, EditorExit::kExit);
    front_end.set_editor(&editor);
    host.script = {kSingleMission, kBuildCustom, kMainMenu, kExitToDos};
    front_end.run();
    CHECK(editor.runs == 1);
    CHECK(host.requests.empty());
  }
  SECTION("QUIT TO DOS? in the editor ends the game") {
    FakeEditor editor(custom, sitduck, EditorExit::kQuitToDos);
    front_end.set_editor(&editor);
    host.script = {kSingleMission, kBuildCustom};
    CHECK_THROWS_AS(front_end.run(), QuitRequested);
  }
}

TEST_CASE("The custom list shows the player's scenarios over the install's", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const SaveDir custom(dir.path(), &install, paths.dir(GameDir::kCustom));
  std::vector<std::uint8_t> mine = install.read("CUSTOM/SITDUCK.SCN");
  const std::string briefing = "Mine";
  std::ranges::copy(briefing, mine.begin() + 6);
  REQUIRE(custom.write("AAA.SCN", mine));
  std::vector<ScenarioListEntry> list = scan_scenarios(install, paths, GameDir::kCustom, "*.SCN", true, &custom);
  REQUIRE(list.size() == 2);
  CHECK(list[0].title == "AAA");
  CHECK(list[0].description.starts_with("Mine"));
  CHECK(list[1].title == "SITDUCK");
  CHECK(read_scenario_file(install, paths, &custom, "CUSTOM\\AAA.SCN") == mine);
  // A deleted install scenario is left out and cannot be read.
  custom.remove("SITDUCK.SCN");
  list = scan_scenarios(install, paths, GameDir::kCustom, "*.SCN", true, &custom);
  REQUIRE(list.size() == 1);
  CHECK(list[0].title == "AAA");
  CHECK_THROWS_AS(read_scenario_file(install, paths, &custom, "CUSTOM\\SITDUCK.SCN"), DataError);
  // Other directories are the install's.
  CHECK(read_scenario_file(install, paths, &custom, "SCEN\\HIST01.SCN") == install.read("SCEN/HIST01.SCN"));
}

// --- Popup menus --------------------------------------------------------------

// A GUI whose pump replays scripted input, for the popup menus.
struct PopupHarness {
  explicit PopupHarness(const Install& install)
      : resources(ui::GuiResources::load(install)), skin(resources), hooks(resources),
        gui(screen, [this] { return tick; }, skin) {
    gui.set_pump([this] {
      ++tick;
      if (script.empty()) {
        throw std::runtime_error("the popup is still open");
      }
      script.front()();
      script.pop_front();
    });
  }
  void key(std::uint16_t k) {
    script.emplace_back([this, k] { gui.mouse().post_key(k, 0); });
  }
  void release_at(int x, int y) {
    script.emplace_back([this, x, y] {
      gui.mouse().update(x, y, 0, 0);
    });
  }
  void press_at(int x, int y) {
    script.emplace_back([this, x, y] { gui.mouse().update(x, y, ui::kButtonLeft, 0); });
  }

  Image8 screen{kScreenWidth, kScreenHeight, 0x21};
  std::uint32_t tick = 0;
  ui::GuiResources resources;
  ui::DefaultSkin skin;
  ui::DefaultMenuHooks hooks;
  ui::Gui gui;
  std::deque<std::function<void()>> script;
};

TEST_CASE("Popup menus select items by keys and the mouse", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  PopupHarness h(install);
  std::vector<std::string> ran;
  bool toggled = false;
  const ui::PopupMenu sub{{ui::PopupItem::make_action("Inner", [&] { ran.push_back("inner"); })}};
  const ui::PopupMenu menu{{
      ui::PopupItem::make_action("Alpha", [&] { ran.push_back("alpha"); }),
      ui::PopupItem::make_action("Bravo", [&] { ran.push_back("bravo"); }, 0xff42, "B"),
      ui::PopupItem::make_submenu("More", sub),
      ui::PopupItem::make_toggle("Flag",
                                 [&](bool flip) {
                                   if (flip) {
                                     toggled = !toggled;
                                   }
                                   return toggled;
                                 }),
  }};
  const Image8 before = h.screen;
  constexpr int kX = 100;
  constexpr int kY = 80;

  SECTION("Down and Enter run the second item") {
    h.key(ui::kKeyDown);
    h.key(ui::kKeyEnter);
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, true) == ui::kPopupItemRun);
    CHECK(ran == std::vector<std::string>{"bravo"});
  }
  SECTION("Up wraps to the last item") {
    h.key(ui::kKeyUp);
    h.key(ui::kKeyEnter);
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, false) == ui::kPopupItemRun);
    CHECK(toggled);
    CHECK(ran.empty());
  }
  SECTION("Esc closes the menu with its key") {
    h.key(ui::kKeyEsc);
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, true) == ui::kKeyEsc);
    CHECK(ran.empty());
  }
  SECTION("An item's hotkey runs it") {
    h.key(0x3042);  // B
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, true) == ui::kPopupItemRun);
    CHECK(ran == std::vector<std::string>{"bravo"});
  }
  SECTION("A click released on an item runs it") {
    // Item 0 spans y + 9 .. y + 28.
    h.press_at(kX + 50, kY + 15);
    h.release_at(kX + 50, kY + 15);
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, true) == ui::kPopupItemRun);
    CHECK(ran == std::vector<std::string>{"alpha"});
  }
  SECTION("Right opens a submenu, whose item closes every menu") {
    h.key(ui::kKeyDown);
    h.key(ui::kKeyDown);
    h.key(ui::kKeyRight);
    h.key(ui::kKeyEnter);
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, true) == ui::kPopupItemRun);
    CHECK(ran == std::vector<std::string>{"inner"});
  }
  SECTION("Left closes only the submenu") {
    h.key(ui::kKeyDown);
    h.key(ui::kKeyDown);
    h.key(ui::kKeyRight);
    h.key(ui::kKeyLeft);
    h.key(ui::kKeyUp);
    h.key(ui::kKeyEnter);
    CHECK(ui::popup_menu_run(h.gui, h.hooks, menu, kX, kY, true) == ui::kPopupItemRun);
    CHECK(ran == std::vector<std::string>{"bravo"});
  }
  // The screen under the menu is restored and its hotspots removed.
  CHECK(h.screen.pixels().size() == before.pixels().size());
  CHECK(std::equal(h.screen.pixels().begin(), h.screen.pixels().end(), before.pixels().begin()));
  CHECK(h.gui.hotspots().size() == 0);
}

// --- Android's Back ---------------------------------------------------------

// More buttons of the main menu and the single-mission menu.
constexpr ScriptedHost::Step kCareer = ScriptedHost::Step{0, 20 + 35 + 90, 43 + 170 + 17};
constexpr ScriptedHost::Step kInterviews = ScriptedHost::Step{0, 20 + 35 + 90, 43 + 210 + 17};
constexpr ScriptedHost::Step kSubTour = ScriptedHost::Step{0, 20 + 35 + 90, 43 + 250 + 17};
constexpr ScriptedHost::Step kCredits = ScriptedHost::Step{0, 20 + 35 + 90, 43 + 290 + 17};
constexpr ScriptedHost::Step kWarship = ScriptedHost::Step{0, 69 + 38 + 90, 59 + 165 + 17};
constexpr ScriptedHost::Step kContinue = ScriptedHost::Step{0, 69 + 261 + 90, 59 + 205 + 17};
constexpr ScriptedHost::Step kLoadCustom = ScriptedHost::Step{0, 69 + 261 + 90, 59 + 245 + 17};
constexpr ScriptedHost::Step kBestMissions = ScriptedHost::Step{0, 69 + 261 + 90, 59 + 299 + 17};
// The REALISM button of the scenario lists and NOVICE in the REALISM menu.
constexpr ScriptedHost::Step kListRealism = ScriptedHost::Step{0, 69 + 185 + 60, 19 + 395 + 17};
constexpr ScriptedHost::Step kNovice = ScriptedHost::Step{0, 52 + 27 + 60, 10 + 125 + 17};

// Each script below ends on the main menu's Back: `run` returns (EXIT TO
// DOS, without its box). The screen shown at each step (its hotspots)
// shows that each Back went back to the screen before.
TEST_CASE("Back walks the single-mission screens back one at a time and quits on the main menu", "[frontend][back]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  const ScriptedHost::Step back = ScriptedHost::back();
  // The historical list; the convoy encounter and its SUB CLASS screen;
  // a historical mission, after which the menu comes back.
  host.script = {kSingleMission, kHistoric, back, kConvoy, kBegin, back, back, kHistoric, kBegin, back, back};
  front_end.run();
  CHECK(host.script.empty());
  const std::vector<ScriptedHost::Screen>& on = host.screens;
  REQUIRE(on.size() == 11);
  const ScriptedHost::Screen& main_menu = on[0];
  const ScriptedHost::Screen& single = on[1];
  CHECK(main_menu != single);
  CHECK(on[2] != single);  // the historical list
  CHECK(on[3] == single);
  CHECK(on[5] != on[4]);  // SUB CLASS over the encounter
  CHECK(on[6] == on[4]);
  CHECK(on[7] == single);
  CHECK(on[8] == on[2]);
  CHECK(on[9] == single);
  CHECK(on[10] == main_menu);
  REQUIRE(host.requests.size() == 1);
  CHECK(host.requests[0].scenario_path == "SCEN\\HIST01.SCN");
}

TEST_CASE("Back cancels REALISM and closes CONTINUE MISSION, BEST MISSIONS and the encounters", "[frontend][back]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  const ScriptedHost::Step back = ScriptedHost::back();
  // REALISM from the list, NOVICE, Back (its CANCEL: nothing saved), Back
  // to the menu; CONTINUE MISSION's "no saved mission" box; BEST
  // MISSIONS; the custom list; the warship encounter.
  host.script = {kSingleMission, kHistoric, kListRealism, kNovice, back, back, kContinue, back,
                 kBestMissions,  back,      kLoadCustom,  back,    kWarship, back, back, back};
  front_end.run();
  CHECK(host.script.empty());
  const std::vector<ScriptedHost::Screen>& on = host.screens;
  REQUIRE(on.size() == 16);
  const ScriptedHost::Screen& single = on[1];
  CHECK(on[3] != on[2]);  // REALISM over the list
  CHECK(on[4] == on[3]);
  CHECK(on[5] == on[2]);
  for (const std::size_t i : {6, 8, 10, 12, 14}) {
    CHECK(on[i] == single);
  }
  for (const std::size_t i : {7, 9, 11, 13}) {
    CHECK(on[i] != single);
  }
  CHECK(on[15] == on[0]);
  CHECK(host.requests.empty());
  CHECK_FALSE(std::filesystem::exists(dir.path() / "REALISM.SAV"));
}

TEST_CASE("Back leaves INTERVIEWS, SUB TOUR, the credits and the career menu", "[frontend][back]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  const ScriptedHost::Step back = ScriptedHost::back();
  host.script = {kInterviews, back, kSubTour, back, kCredits, back, kCareer, back, back};
  front_end.run();
  CHECK(host.script.empty());
  const std::vector<ScriptedHost::Screen>& on = host.screens;
  REQUIRE(on.size() == 9);
  for (const std::size_t i : {2, 4, 6, 8}) {
    CHECK(on[i] == on[0]);
  }
  for (const std::size_t i : {1, 3, 5, 7}) {
    CHECK(on[i] != on[0]);
  }
}

TEST_CASE("Back closes the mission results, the commander's name entry and BEST MISSIONS", "[frontend][back]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  // No best missions yet: the mission ranks first.
  const SaveDir saves(dir.path());
  ScriptedHost host;
  host.mission.end_code = 0;
  host.mission.boat_name = "(SS-305) Skate";
  host.mission.class_name = "BALAO";
  host.mission.difficulty = 500;
  PatrolLogEntry sunk;
  sunk.type = 0;
  sunk.name = "SHOKAKU";
  sunk.tonnage = 29800;
  host.mission.patrol_log = {sunk};
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  const ScriptedHost::Step back = ScriptedHost::back();
  // The name entry edits at once: Back ends the edit and is its OK.
  host.script = {kSingleMission, kHistoric, kBegin, back, back, kBestMissions, back, back, back};
  front_end.run();
  CHECK(host.script.empty());
  const std::optional<BestMissionsTable> table = BestMissionsTable::load(saves);
  REQUIRE(table);
  REQUIRE(table->entries[0]);
  CHECK(table->entries[0]->name == "Anonymous");
  CHECK(table->entries[0]->results.sub == "(SS-305) Skate");
}

TEST_CASE("The default menu hooks place the popup items", "[frontend]") {
  SH_REQUIRE_GAME_DATA(install);
  ui::GuiResources resources = ui::GuiResources::load(install);
  ui::DefaultMenuHooks hooks(resources);
  CHECK(hooks.popup_height(4) == 4 * 24 + 14);
  CHECK(hooks.submenu_origin(2, 100, 50).x == 100 + 225);
  CHECK(hooks.submenu_origin(2, 100, 50).y == 50 + 9 + 48);
  Image8 buffer(240, 200);
  const ui::PopupItem item = ui::PopupItem::make_action("Alpha", [] {});
  const ui::Rect r = hooks.popup_item(Viewport::full(buffer), 100, 50, 1, item, true);
  CHECK(r == ui::Rect{105, 50 + 33, 105 + 221, 50 + 33 + 19});
  Image8 bar(640, 40);
  const ui::Rect title = hooks.menu_bar_item(Viewport::full(bar), 20, {.menu = nullptr, .label = "File"}, false);
  CHECK(title.x0 == 25);
  CHECK(title.y1 == 26);
  CHECK(title.x1 == 34 + resources.small.text_width("File"));
}

}  // namespace
}  // namespace sh
