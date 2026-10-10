// Playing on a touch screen and being suspended: the system's text input
// (soft keyboard characters to BIOS words, the edit loops turning it on),
// and the resume snapshot RESUME.SAV written when the program is suspended
// and resumed at the next launch.

#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/patrol_zones.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "game/frontend.h"
#include "game/game_runner.h"
#include "game/mission.h"
#include "game/mission_session.h"
#include "game/patrol.h"
#include "game/resume.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game/sim_state.h"
#include "game_data.h"
#include "platform/bios_keys.h"
#include "ui/controls.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh {
namespace {

// ------------------------------------------------------------ text input

InputEvent key_down(std::uint32_t key, std::uint32_t scancode, char32_t character, std::uint16_t mods = 0,
                    char32_t altgr = 0) {
  InputEvent e;
  e.type = EventType::kKeyDown;
  e.key = key;
  e.scancode = scancode;
  e.character = character;
  e.altgr_character = altgr;
  e.mods = mods;
  return e;
}

TEST_CASE("Text input characters give the words of a US keyboard", "[keys][resume]") {
  // "é" (two UTF-8 bytes) and the newline are dropped.
  const std::vector<InputEvent> events = text_input_events("aZ 5+\xc3\xa9\n@");
  std::vector<std::uint16_t> words;
  for (const InputEvent& e : events) {
    CHECK(e.type == EventType::kText);
    const std::optional<std::uint16_t> word = bios_key_word(e);
    REQUIRE(word);
    words.push_back(*word);
  }
  CHECK(words == std::vector<std::uint16_t>{0x1e61, 0x2c5a, 0x3920, 0x0635, 0x0d2b, 0x0340});
  CHECK(text_input_events("\xe2\x82\xac").empty());
  CHECK_FALSE(bios_character_word(U'\t'));
}

TEST_CASE("Keys typing characters are left to the text input", "[keys][resume]") {
  // Typing keys: their characters come as text, so the key downs go.
  CHECK(key_types_text(key_down(SDLK_A, SDL_SCANCODE_A, U'a')));
  CHECK(key_types_text(key_down(SDLK_A, SDL_SCANCODE_A, U'A', SDL_KMOD_LSHIFT)));
  CHECK(key_types_text(key_down(SDLK_SPACE, SDL_SCANCODE_SPACE, U' ')));
  // An AZERTY key: '&' plain, '@' with AltGr.
  CHECK(key_types_text(key_down(SDLK_1, SDL_SCANCODE_1, U'&')));
  CHECK(key_types_text(key_down(SDLK_0, SDL_SCANCODE_0, U'à', SDL_KMOD_RALT, U'@')));
  // A non-ASCII letter: its text is dropped, and so is the key down.
  CHECK(key_types_text(key_down(0xe9, SDL_SCANCODE_2, U'é')));
  // The keypad: its operators, its digits under Num Lock.
  CHECK(key_types_text(key_down(SDLK_KP_PLUS, SDL_SCANCODE_KP_PLUS, SDLK_KP_PLUS)));
  CHECK(key_types_text(key_down(SDLK_KP_5, SDL_SCANCODE_KP_5, SDLK_KP_5, SDL_KMOD_NUM)));
  CHECK_FALSE(key_types_text(key_down(SDLK_KP_5, SDL_SCANCODE_KP_5, SDLK_KP_5)));
  // A script's key without scancode.
  CHECK(key_types_text(key_down(SDLK_B, SDL_SCANCODE_UNKNOWN, 0)));

  // The others stay keys: editing and leaving the field, hotkeys.
  CHECK_FALSE(key_types_text(key_down(SDLK_RETURN, SDL_SCANCODE_RETURN, U'\r')));
  CHECK_FALSE(key_types_text(key_down(SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE, U'\b')));
  CHECK_FALSE(key_types_text(key_down(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, U'\x1b')));
  CHECK_FALSE(key_types_text(key_down(SDLK_AC_BACK, SDL_SCANCODE_AC_BACK, SDLK_AC_BACK)));
  CHECK_FALSE(key_types_text(key_down(SDLK_TAB, SDL_SCANCODE_TAB, U'\t')));
  CHECK_FALSE(key_types_text(key_down(SDLK_DELETE, SDL_SCANCODE_DELETE, SDLK_DELETE)));
  CHECK_FALSE(key_types_text(key_down(SDLK_LEFT, SDL_SCANCODE_LEFT, SDLK_LEFT)));
  CHECK_FALSE(key_types_text(key_down(SDLK_A, SDL_SCANCODE_A, U'a', SDL_KMOD_LCTRL)));
  CHECK_FALSE(key_types_text(key_down(SDLK_A, SDL_SCANCODE_A, U'a', SDL_KMOD_LALT)));
  InputEvent up = key_down(SDLK_A, SDL_SCANCODE_A, U'a');
  up.type = EventType::kKeyUp;
  CHECK_FALSE(key_types_text(up));
}

// A GUI on a black screen with an injected clock and recording text
// input hooks.
struct TextInputHarness {
  Image8 screen{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::GuiResources resources{};
  ui::DefaultSkin skin{resources};
  ui::Gui gui{screen, [this] { return tick; }, skin};
  std::vector<std::string> calls;

  TextInputHarness() {
    gui.set_text_input({.begin =
                            [this](const ui::Rect& r, ui::TextFieldKind kind) {
                              calls.push_back(std::format("begin {} {} {} {} {}", r.x0, r.y0, r.x1, r.y1,
                                                          kind == ui::TextFieldKind::kDigits ? "digits" : "text"));
                            },
                        .end = [this] { calls.emplace_back("end"); }});
  }

  // Runs `dialog` with `keys` typed from the pump, recording whether the
  // text input was on when the keys were read.
  int run(ui::Dialog& dialog, std::deque<std::uint16_t> keys, std::vector<std::size_t>* calls_at_keys = nullptr) {
    int pumps = 0;
    gui.set_pump([&] {
      ++tick;
      REQUIRE(++pumps < 1000);
      if (pumps > 2 && !keys.empty()) {
        if (calls_at_keys != nullptr) {
          calls_at_keys->push_back(calls.size());
        }
        gui.mouse().post_key(keys.front(), 0);
        keys.pop_front();
      }
    });
    return dialog.run();
  }
};

TEST_CASE("The text fields' edit loops turn the text input on and off", "[ui][resume]") {
  SH_REQUIRE_GAME_EXE();
  SECTION("a text entry") {
    TextInputHarness h;
    ui::Dialog d(h.gui, 100, 50, 300, 100, ui::kDialogCancel);
    auto& entry = d.emplace<ui::TextEntry>(10, 20, 150, 8, "", [](std::string&, int) { return 5; });
    std::vector<std::size_t> at_keys;
    CHECK(h.run(d, {0x1e61, 0x3062, ui::kKeyEnter}, &at_keys) == 5);
    CHECK(entry.text == "ab");
    const int height = h.skin.metrics().entry_height;
    CHECK(h.calls == std::vector<std::string>{std::format("begin 110 70 259 {} text", 70 + height - 1), "end"});
    // The keys were typed with the text input on.
    for (const std::size_t n : at_keys) {
      CHECK(n == 1);
    }
  }
  SECTION("a numeric entry") {
    TextInputHarness h;
    ui::Dialog d(h.gui, 0, 0, 300, 100, ui::kDialogCancel);
    d.emplace<ui::SpinEntry>(10, 10, 60, 4, "1");
    h.run(d, {0x0332, ui::kKeyEsc});
    REQUIRE(h.calls.size() == 2);
    CHECK(h.calls[0].ends_with(" digits"));
    CHECK(h.calls[1] == "end");
  }
  SECTION("a text box, tapped again while editing") {
    TextInputHarness h;
    ui::Dialog d(h.gui, 0, 0, 300, 200, ui::kDialogCancel);
    auto& box = d.emplace<ui::TextBox>(10, 10, 120, 100, 10, 50, "");
    int pumps = 0;
    h.gui.set_pump([&] {
      ++h.tick;
      REQUIRE(++pumps < 1000);
      if (pumps == 3) {
        h.gui.mouse().post_key(0x1e61, 0);
      } else if (pumps == 5) {
        // A tap on the box shows the keyboard again.
        h.gui.mouse().update(40, 40, ui::kButtonLeft, 0);
        h.gui.mouse().update(40, 40, 0, 0);
      } else if (pumps == 8) {
        h.gui.mouse().post_key(ui::kKeyEsc, 0);
      }
    });
    d.run();
    CHECK(box.text == "a");
    CHECK(h.calls == std::vector<std::string>{"begin 10 10 129 109 text", "begin 10 10 129 109 text", "end"});
  }
  SECTION("nested fields") {
    TextInputHarness h;
    h.gui.begin_text_input({1, 2, 3, 4}, ui::TextFieldKind::kText);
    {
      const ui::TextInputScope inner(h.gui, {5, 6, 7, 8}, ui::TextFieldKind::kDigits);
    }
    h.gui.end_text_input();
    CHECK(h.calls == std::vector<std::string>{"begin 1 2 3 4 text", "begin 5 6 7 8 digits", "begin 1 2 3 4 text",
                                              "end"});
  }
}

// ------------------------------------------------------- resume snapshot

// Thrown by the host to stop a run at a chosen frame (the process killed).
struct Stop {};

// The program's host without SDL: a clock moving 5 ticks per frame shown,
// the input given by `on_poll`.
class ResumeHost final : public FrontendHost {
 public:
  std::function<void(ui::Gui&)> on_poll;
  std::vector<std::string> reports;
  std::uint32_t tick = 0;
  int presents = 0;

  std::uint32_t ticks() override { return tick; }
  void present(const Image8&, const VgaPalette&) override {
    tick += 5;
    if (++presents > 200'000) {
      throw std::runtime_error("the run did not stop");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (on_poll) {
      on_poll(gui);
    }
  }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view message) override { reports.emplace_back(message); }
};

// The game program over a save directory: the front end and the mission
// runner on a `ResumeHost`, as `run_game` puts them together.
struct GameRig {
  GameRig(const Install& game, const std::filesystem::path& dir)
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        saves(dir / "SAVE", &game, paths.dir(GameDir::kSave)),
        custom(dir / "CUSTOM", &game, paths.dir(GameDir::kCustom)),
        runner({.install = game,
                .paths = paths,
                .exe = exe,
                .saves = saves,
                .custom = custom,
                .wait_tick = [this] { host.tick += 5; }},
               host),
        front_end({.install = game, .paths = paths, .exe = exe, .saves = saves, .custom = &custom}, host, runner, 1) {
    runner.set_front_end(front_end);
    front_end.set_play_intro(false);
    front_end.set_resume(true);
  }

  // Clicks the OK button of the MISSION BRIEFING.
  void close_briefing(ui::Gui& gui) {
    if (++briefing_polls_ % 20 == 0) {
      gui.mouse().update(165, 412, ui::kButtonLeft, 0);
      gui.mouse().update(165, 412, 0, 0);
    }
  }
  // Returns the frames shown since the mission started (counting from its
  // first poll), -1 before.
  int mission_frame() {
    if (runner.session() == nullptr) {
      return -1;
    }
    if (mission_start_ < 0) {
      mission_start_ = host.presents;
    }
    return host.presents - mission_start_;
  }

  const Install& install;
  ShPaths paths;
  ExeImage exe;
  SaveDir saves;
  SaveDir custom;
  ResumeHost host;
  GameRunner runner;
  FrontEnd front_end;

 private:
  int briefing_polls_ = 0;
  int mission_start_ = -1;
};

std::vector<std::uint8_t> sim_bytes(Mission& m) { return capture_sim_state(m, {}, {}).serialize(); }

// Returns where two state lists differ first ("" when equal).
std::string state_difference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  ByteReader ra(a, "a");
  ByteReader rb(b, "b");
  const SimStateFile fa = SimStateFile::parse(ra, kSimStateVersion);
  const SimStateFile fb = SimStateFile::parse(rb, kSimStateVersion);
  const auto differs = [](const StateImage& x, const StateImage& y) {
    const auto dx = x.bytes(0, x.size());
    const auto dy = y.bytes(0, y.size());
    const auto [ix, iy] = std::ranges::mismatch(dx, dy);
    return ix != dx.end() || iy != dy.end() ? std::format(" +{}", ix - dx.begin()) : std::string();
  };
  if (const std::string d = differs(fa.player, fb.player); !d.empty()) {
    return "player" + d;
  }
  if (fa.vehicles.size() != fb.vehicles.size()) {
    return "vehicle count";
  }
  for (std::size_t i = 0; i < fa.vehicles.size(); ++i) {
    if (const std::string d = differs(fa.vehicles[i].image, fb.vehicles[i].image); !d.empty()) {
      return std::format("vehicle {}{}", i, d);
    }
  }
  if (fa.tdc && fb.tdc && !differs(*fa.tdc, *fb.tdc).empty()) {
    return "tdc" + differs(*fa.tdc, *fb.tdc);
  }
  if (fa.deck_gun && fb.deck_gun && !differs(*fa.deck_gun, *fb.deck_gun).empty()) {
    return "deck gun" + differs(*fa.deck_gun, *fb.deck_gun);
  }
  if (fa.sim_extension != fb.sim_extension) {
    const auto [ix, iy] = std::ranges::mismatch(fa.sim_extension, fb.sim_extension);
    return std::format("sim extension +{}", ix - fa.sim_extension.begin());
  }
  return a == b ? "" : "elsewhere";
}

bool has_snapshot(const SaveDir& saves) {
  const std::optional<std::vector<std::uint8_t>> data = saves.read(kResumeFileName);
  return data && !data->empty();
}

// The mission time: day, hour, minute, second, hundredths.
std::array<std::int32_t, 5> mission_time(const Mission& m) {
  const GameClock& c = m.sim().clock();
  return {c.day(), c.hour(), c.minute(), c.second(), c.hundredths()};
}

TEST_CASE("A suspended mission resumes at the next launch as if never left", "[resume]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_resume_mission_test";
  std::filesystem::remove_all(dir);
  constexpr int kSuspendAt = 300;
  constexpr int kMore = 200;

  // HIST01 played at the status station, suspended (the snapshot written
  // as on SDL_EVENT_WILL_ENTER_BACKGROUND), then played on.
  std::vector<std::uint8_t> expected;
  std::array<std::int32_t, 5> expected_time{};
  std::vector<std::uint8_t> at_suspend;
  {
    GameRig a(install, dir);
    a.host.on_poll = [&](ui::Gui& gui) {
      const int frame = a.mission_frame();
      if (frame < 0) {
        a.close_briefing(gui);
        return;
      }
      Mission& mission = a.runner.session()->screen().mission();
      if (frame == 0) {
        a.runner.session()->screen().stations().select(StationId::kStatus);
      } else if (frame == kSuspendAt && at_suspend.empty()) {
        REQUIRE(a.runner.suspend());
        at_suspend = sim_bytes(mission);
      } else if (frame == kSuspendAt + kMore) {
        expected = sim_bytes(mission);
        expected_time = mission_time(mission);
        throw Stop();
      }
    };
    MissionRequest request;
    request.type = MissionType::kScenario;
    request.scenario_path = "SCEN\\HIST01.SCN";
    request.options = RealismFile::kDefaultBits;
    request.random = WatcomRandom(7);
    CHECK_THROWS_AS(a.runner.run(request), Stop);
  }
  REQUIRE(has_snapshot(SaveDir(dir / "SAVE")));
  REQUIRE_FALSE(expected.empty());

  // The next launch: no intro, the mission restored at the status station,
  // which goes on as the one never suspended. Its station opens with a
  // fade, which holds the game a few frames (as when the player changes
  // station): the states are compared at the same mission time.
  GameRig b(install, dir);
  std::vector<std::uint8_t> resumed;
  b.host.on_poll = [&](ui::Gui&) {
    const int frame = b.mission_frame();
    if (frame < 0) {
      return;
    }
    MissionScreen& screen = b.runner.session()->screen();
    if (frame == 0) {
      CHECK(state_difference(sim_bytes(screen.mission()), at_suspend) == "");
      CHECK(screen.stations().current() == StationId::kStatus);
      CHECK(b.front_end.location().place == FrontEndPlace::kSingleMission);
      // Consumed at the start.
      CHECK_FALSE(has_snapshot(b.saves));
    }
    const std::array<std::int32_t, 5> now = mission_time(screen.mission());
    if (now == expected_time) {
      resumed = sim_bytes(screen.mission());
      throw Stop();
    }
    REQUIRE(now < expected_time);
  };
  CHECK_THROWS_AS(b.front_end.run(), Stop);
  CHECK(b.host.reports == std::vector<std::string>{});
  CHECK(state_difference(resumed, expected) == "");
  std::filesystem::remove_all(dir);
}

// A career at Darwin on 1 June 1945 in a Balao (as career_rules_test),
// its last patrol zone the one its patrol is in.
Career patrol_career() {
  Career c;
  CareerRecord& r = c.record;
  r.name = "Tester";
  r.day = 1;
  r.month = 5;
  r.year = 15;
  r.status = CareerStatus::kInPort;
  r.boat = "(SS-337) Carbonero";
  r.class_name = "BALAO";
  r.class_index = 7;
  r.rank = 1;
  r.equipment = 0x6f;
  r.home_port = "Darwin";
  r.location = "Darwin";
  r.fuel_capacity = 117000;
  r.fuel_use = 7209;
  r.transit_speed = 20;
  r.roster_slot = 3;
  r.last_patrol_zone = 9;
  c.summary.preferred_command = -1;
  c.summary.current_command = 2;
  return c;
}

TEST_CASE("A suspended career patrol resumes inside the career", "[resume]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_resume_patrol_test";
  std::filesystem::remove_all(dir);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");

  // A patrol in zone 9, suspended at its 200th frame.
  std::size_t vehicles = 0;
  std::int32_t minute = 0;
  std::int32_t hundredths = 0;
  {
    GameRig a(install, dir);
    Career career = patrol_career();
    WatcomRandom random(99);
    PatrolRequest request;
    request.career = &career;
    request.plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
    request.options = option::kModeCareer | RealismFile::kDefaultBits;
    request.random = random;
    a.host.on_poll = [&](ui::Gui& gui) {
      const int frame = a.mission_frame();
      if (frame < 0) {
        a.close_briefing(gui);
      } else if (frame == 200) {
        REQUIRE(a.runner.suspend());
        const Mission& m = a.runner.session()->screen().mission();
        vehicles = m.sim().vehicles().size();
        minute = m.sim().clock().minute();
        hundredths = m.sim().clock().hundredths();
        throw Stop();
      }
    };
    PatrolResult result;
    CHECK_THROWS_AS(a.runner.run_patrol(request, result), Stop);
    // The snapshot leaves the patrol's career as it was.
    CHECK(career.patrol_state.empty());
  }
  const std::optional<std::vector<std::uint8_t>> file = SaveDir(dir / "SAVE").read(kResumeFileName);
  REQUIRE(file);
  const ResumeSnapshot snapshot = ResumeSnapshot::parse(*file);
  CHECK(snapshot.kind == ResumeKind::kPatrol);
  CHECK(snapshot.location == FrontEndLocation{FrontEndPlace::kCareerBase, 3});
  REQUIRE(snapshot.career);
  CHECK(snapshot.career->record.on_patrol != 0);

  // The next launch: the career's base goes straight back to the patrol
  // (no orders, no departure), restored; aborted, the patrol ends the
  // career's flow as one ended in play, back to the main menu.
  GameRig b(install, dir);
  bool resumed = false;
  b.host.on_poll = [&](ui::Gui&) {
    const int frame = b.mission_frame();
    if (frame == 0) {
      const Mission& m = b.runner.session()->screen().mission();
      CHECK(b.front_end.location() == FrontEndLocation{FrontEndPlace::kCareerBase, 3});
      CHECK(m.sim().vehicles().size() == vehicles);
      CHECK(m.sim().clock().minute() == minute);
      CHECK(m.sim().clock().hundredths() == hundredths);
      CHECK((m.sim().conditions().options & option::kModeCareer) != 0);
      resumed = true;
    } else if (frame == 5) {
      // EXIT WITHOUT SAVING.
      b.runner.session()->screen().mission().set_end_code(1);
    } else if (frame < 0 && resumed && b.front_end.location().place == FrontEndPlace::kMainMenu) {
      throw Stop();
    }
  };
  CHECK_THROWS_AS(b.front_end.run(), Stop);
  CHECK(resumed);
  CHECK(b.host.reports.empty());
  CHECK_FALSE(has_snapshot(b.saves));
  std::filesystem::remove_all(dir);
}

TEST_CASE("The launch resumes the menus of the snapshot, a damaged one starts anew", "[resume]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_resume_menu_test";
  std::filesystem::remove_all(dir);
  const SaveDir saves(dir / "SAVE");

  // The first menu shown at the launch.
  const auto first_place = [&]() {
    GameRig rig(install, dir);
    std::optional<FrontEndPlace> place;
    rig.host.on_poll = [&](ui::Gui&) {
      place = rig.front_end.location().place;
      throw Stop();
    };
    CHECK_THROWS_AS(rig.front_end.run(), Stop);
    return std::pair(place, rig.host.reports);
  };

  ResumeSnapshot menu;
  menu.location.place = FrontEndPlace::kSingleMission;
  REQUIRE(write_resume_snapshot(saves, menu));
  auto [place, reports] = first_place();
  CHECK(place == FrontEndPlace::kSingleMission);
  CHECK(reports.empty());
  CHECK_FALSE(has_snapshot(saves));

  // A byte changed: reported, deleted, the main menu.
  std::vector<std::uint8_t> damaged = menu.serialize();
  damaged[12] ^= 1;
  REQUIRE(saves.write(kResumeFileName, damaged));
  std::tie(place, reports) = first_place();
  CHECK(place == FrontEndPlace::kMainMenu);
  REQUIRE(reports.size() == 1);
  CHECK(reports[0].starts_with("RESUME.SAV"));
  CHECK_FALSE(has_snapshot(saves));

  // A short file.
  std::vector<std::uint8_t> mission_file = menu.serialize();
  mission_file.resize(10);
  REQUIRE(saves.write(kResumeFileName, mission_file));
  std::tie(place, reports) = first_place();
  CHECK(place == FrontEndPlace::kMainMenu);
  CHECK(reports.size() == 1);
  std::filesystem::remove_all(dir);
}

TEST_CASE("The resume snapshot keeps the mission and its save slot", "[resume]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.options = RealismFile::kDefaultBits;
  setup.random = WatcomRandom(3);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
  for (int i = 0; i < 50; ++i) {
    mission->simulate_frame(10, false);
  }
  const ResumeSnapshot s = mission_resume_snapshot(install, *mission, {}, {4, "Night attack"},
                                                   {FrontEndPlace::kSingleMission, -1});
  const std::vector<std::uint8_t> bytes = s.serialize();
  const ResumeSnapshot back = ResumeSnapshot::parse(bytes);
  CHECK(back.kind == ResumeKind::kMission);
  CHECK(back.slot.number == 4);
  CHECK(back.slot.name == "Night attack");
  REQUIRE(back.mission);
  CHECK(back.mission->serialize() == s.mission->serialize());
  CHECK(back.serialize() == bytes);
  // A truncated mission image is damage.
  std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 100);
  CHECK_THROWS_AS(ResumeSnapshot::parse(cut), DataError);
}

}  // namespace
}  // namespace sh
