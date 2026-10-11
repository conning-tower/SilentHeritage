// Gameplay QA regressions: behaviours checked by playing the game.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <set>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "game/mission_session.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "ui/default_skin.h"
#include "audio/mel_layer.h"
#include "audio/sample_table.h"
#include "game/command_bar.h"
#include "game/engine_sounds.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game_data.h"
#include "platform/bios_keys.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"
#include "sim_classes.h"
#include "ui/hotspot.h"

namespace sh {

namespace {

// Returns the hotkey action the BIOS word `word` runs, if any.
std::optional<HotkeyAction> hotkey_for(std::uint16_t word) {
  for (const Hotkey& h : hotkey_table()) {
    if (ui::hotkey_matches(h.key, word)) {
      return h.action;
    }
  }
  return std::nullopt;
}

// HIST01 with every station, no sound, an injected clock.
struct SessionRig {
  explicit SessionRig(const Install& game)
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        tables(GameTables::load(game)),
        resources(ui::GuiResources::load(game)),
        saves(std::filesystem::temp_directory_path() / "sh_qa_test_saves", &game, paths.dir(GameDir::kSave)) {
    session = std::make_unique<MissionSession>(
        MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail},
        Mission::load(install, tables, "SCEN/HIST01.SCN", {}), saves);
  }
  ~SessionRig() { std::filesystem::remove_all(std::filesystem::temp_directory_path() / "sh_qa_test_saves"); }

  // Runs `n` frames of 5 ticks.
  void frames(int n) {
    for (int i = 0; i < n; ++i) {
      tick += 5;
      session->screen().frame(5);
    }
  }

  const Install& install;
  ShPaths paths;
  ExeImage exe;
  GameTables tables;
  ui::GuiResources resources;
  ui::DefaultSkin skin{resources};
  Image8 screen_image{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen_image, [this] { return tick; }, skin};
  SaveDir saves;
  GameOptions options;
  DetailOptions detail;
  std::unique_ptr<MissionSession> session;
};

}  // namespace

TEST_CASE("The XO's message box writes its lines in the GUI text colour", "[qa][content]") {
  // show_message_box / ask_yes_no (0x4c38d / 0x4c670): the line fields
  // draw through the GUI glyph table (+0x2c = -1), so the lines are dark
  // on the grey box instead of FONT1's raw light grey.
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install);
  rig.frames(30);
  ControlBar& bar = rig.session->screen().bar();
  for (const bool yes_no : {false, true}) {
    if (yes_no) {
      bar.ask_yes_no("QUIT", "Do you really want", "to quit?", [](bool) {});
    } else {
      bar.message_box("Executive Officer", "We cannot return to base, Captain!", "The enemy is in visual range!");
    }
    rig.frames(30);
    std::set<std::uint8_t> colours;
    for (int y = 248; y < 258; ++y) {
      for (int x = 250; x < 390; ++x) {
        colours.insert(rig.screen_image.at(x, y));
      }
    }
    CHECK(colours == std::set<std::uint8_t>{2, 6});
    rig.gui.mouse().post_key(0x1c0d, 0);  // Enter closes the box.
    rig.frames(30);
  }
}

TEST_CASE("Every '+' and '-' key changes the time compression", "[qa]") {
  // US main row (Shift+'='), keypad, and a layout with a dedicated '+'
  // key (SDLK_PLUS), which used to put no word in the buffer.
  CHECK(hotkey_for(*bios_key_word(SDLK_EQUALS, SDL_KMOD_LSHIFT)) == HotkeyAction::kCompressionUp);
  CHECK(hotkey_for(*bios_key_word(SDLK_KP_PLUS, 0)) == HotkeyAction::kCompressionUp);
  REQUIRE(bios_key_word(SDLK_PLUS, 0) == std::uint16_t{0x0d2b});
  CHECK(hotkey_for(*bios_key_word(SDLK_PLUS, 0)) == HotkeyAction::kCompressionUp);
  CHECK(hotkey_for(*bios_key_word(SDLK_MINUS, 0)) == HotkeyAction::kCompressionDown);
  CHECK(hotkey_for(*bios_key_word(SDLK_KP_MINUS, 0)) == HotkeyAction::kCompressionDown);
  // Letters match their hotkeys in either case ('P' periscope depth).
  CHECK(hotkey_for(*bios_key_word(SDLK_P, 0)) == HotkeyAction::kPeriscopeDepth);
  CHECK(hotkey_for(*bios_key_word(SDLK_P, SDL_KMOD_LSHIFT)) == HotkeyAction::kPeriscopeDepth);
}

TEST_CASE("Periscope depth from the surface in HIST01 with the expert sailing model", "[qa][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.options = 0x20;  // Expert sailing model: the tanks flood at 17.25 t/s.
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", std::move(setup));
  HeadlessRunner runner(*mission);
  runner.run(2);
  PlayerSub& p = mission->player();
  SimContext ctx = mission->sim().context();
  p.order_periscope_depth(ctx);
  // As in sub_dive_update, the boat stays on the surface while the tanks
  // flood (about 33 s), then sinks by buoyancy (the ordered planes decay to
  // 1 within a few updates), reaching 65 ft after about two minutes.
  runner.run(30);
  CHECK(p.depth_ft() == 15);
  runner.run(60);
  CHECK(p.depth_ft() > 30);
  CHECK(p.depth_ft() < 65);
  runner.run(60);
  CHECK(p.depth_ft() == 65);
  runner.run(120);
  CHECK(p.depth_ft() == 65);
}

TEST_CASE("The engines are heard and the telegraph orders spoken", "[qa][content]") {
  // player_update_engine_sounds (0x5cb90) had only its order logic.
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  HeadlessRunner runner(*mission);
  runner.run(5);
  SampleTable samples(&install, paths.dir(GameDir::kSfx));
  MelLayer layer(samples, [] { return 0; });
  GameOptions options;
  SoundPort port(&layer, install, paths, options);
  EngineSounds sounds(port);
  PlayerSub& p = mission->player();
  bool quiet = false;

  // Ahead full on the diesels: 80 % of their power, the fourth loop.
  REQUIRE(p.telegraph() == 4);
  sounds.update(p, nullptr, true, quiet);
  CHECK(sounds.loop_sfx() == 0x4c);
  CHECK(layer.sfx_is_playing(0x4c));
  CHECK_FALSE(layer.speech_is_playing());

  // A new order is spoken at once, the bell rings on the next update.
  p.set_telegraph(1);
  sounds.update(p, nullptr, true, quiet);
  CHECK(layer.speech_is_playing());
  CHECK_FALSE(layer.sfx_is_playing(EngineSounds::kSfxTelegraph));
  sounds.update(p, nullptr, true, quiet);
  CHECK(layer.sfx_is_playing(EngineSounds::kSfxTelegraph));
  // The loop follows the shaft power down to ahead 1/3's.
  runner.run(30);
  sounds.update(p, nullptr, true, quiet);
  CHECK(sounds.loop_sfx() == 0x4f);
  CHECK(layer.sfx_is_playing(0x4f));
  CHECK_FALSE(layer.sfx_is_playing(0x4c));

  // After a crash dive order the telegraph change is not spoken.
  layer.speech_stop();
  quiet = true;
  p.set_telegraph(5);
  sounds.update(p, nullptr, true, quiet);
  CHECK_FALSE(layer.speech_is_playing());
  CHECK_FALSE(quiet);

  // Without the Engine Sounds option the loop stops.
  sounds.refresh();
  sounds.update(p, nullptr, false, quiet);
  CHECK(sounds.loop_sfx() == 0);
  CHECK_FALSE(layer.sfx_is_playing(0x4f));
}

TEST_CASE("The helmsman answers the helm orders and reports on course", "[qa]") {
  // player_update_helm_and_dive (0x5c4f2): HELM03xx.RAW with crew messages
  // #8 / #9 / #10, ONCOURSE.RAW when a steered course is reached.
  const FlatSeaFloor deep(-3000 * kUnitsPerFoot);
  Simulation sim(MissionConditions{}, deep);
  PlayerSub& boat = sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{2000000, 0, 2000000}, Angle10(0), 3));
  const auto step = [&] {
    sim.simulate(Fixed16::from_raw(0x3334));
    return sim.take_events();
  };
  step();
  boat.set_helm(-2);
  MissionEvents e = step();
  CHECK(e.speech == std::vector<std::string>{"HELM0380.RAW"});
  REQUIRE(e.crew_messages.size() == 1);
  CHECK(e.crew_messages[0].id == 9);
  CHECK(e.crew_messages[0].arg == 10);
  CHECK(step().speech.empty());

  boat.set_course(Angle10(0));
  std::vector<std::string> heard;
  for (int i = 0; i < 600; ++i) {
    for (const std::string& s : step().speech) {
      heard.push_back(s);
    }
  }
  CHECK(heard == std::vector<std::string>{"ONCOURSE.RAW"});
  const std::int32_t h = boat.heading().tenths();
  CHECK((h <= 10 || h >= 3590));
}

TEST_CASE("Firing from any station is heard once and spoken", "[qa]") {
  // The TDC's tube buttons fired silently (the key path played the launch
  // sound twice): fire_tube (0x7cd70) plays sfx 0x14 and the order, and
  // the reload ends with the "tube loaded" speech.
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  HeadlessRunner(*mission).run(2);
  Simulation& sim = mission->sim();
  sim.take_events();
  SimContext ctx = sim.context();
  REQUIRE(mission->player_combat().fire_tube(ctx, mission->player(), 6) != nullptr);
  MissionEvents e = sim.take_events();
  CHECK(e.sfx == std::vector<MissionEvents::Sfx>{{PlayerCombat::kSfxFire, -1}});
  CHECK(e.speech == std::vector<std::string>{"WO0021.RAW"});

  // Fired again while it reloads: "tube not loaded".
  SimContext again = sim.context();
  CHECK(mission->player_combat().fire_tube(again, mission->player(), 6) == nullptr);
  CHECK(sim.take_events().speech == std::vector<std::string>{"WO0022.RAW"});

  std::vector<std::string> heard;
  for (int s = 0; s < 1200 && heard.empty(); ++s) {
    sim.simulate(Fixed16::from_int(1));
    heard = sim.take_events().speech;
  }
  CHECK(heard == std::vector<std::string>{"WO0017.RAW"});
}

}  // namespace sh
