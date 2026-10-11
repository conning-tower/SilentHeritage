// The extended variant's TORPEDOES list (not in the original;
// `EditorData::load` with `extended_torpedoes`): every American torpedo,
// the MARK18-2 and MARK27 included, a reload row each, the dialog grown
// past three rows; and the original's list without it.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

#include "editor/data.h"
#include "editor/dialogs_settings.h"
#include "editor/loadout.h"
#include "game_data.h"
#include "editor_ui_fixture.h"

namespace sh::editor {

namespace {

// The editor of `EditorFixture` with its own data.
struct ExtendedFixture {
  explicit ExtendedFixture(const Install& game, bool extended)
      : install(game),
        data(EditorData::load(game, extended)),
        scenario(game, data, options, [] { return 1000; }),
        ui(game, data.se, host) {
    ui.palette() = load_pcx(game, "ART/M8.PCX").palette;
    sprite_apply_palette(ui.sbchart().frame(0), ui.palette());
    ui.dac() = ui.palette();
    ui.palette_grey_out(50, 255);
  }

  const Install& install;
  EditorData data;
  EditorOptions options;
  Scenario scenario;
  test::ScriptedHost host;
  EditorUi ui;
};

std::vector<std::string> listed(const EditorData& data) {
  std::vector<std::string> names;
  for (const std::size_t i : data.torpedo_choices.torpedoes) {
    names.push_back(data.tables.weapons.torpedoes[i].short_name);
  }
  return names;
}

}  // namespace

TEST_CASE("TORPEDOES lists the Mark 18-2 and 27 with the extended torpedoes only", "[editor][torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  const EditorData original = EditorData::load(install);
  CHECK(listed(original) == std::vector<std::string>{"MARK10", "MARK14", "MARK18-1"});
  CHECK(original.torpedo_choices.count == 4);
  CHECK_FALSE(original.torpedo_choices.extended);
  CHECK(original.tables.find_torpedo("MARK27") == nullptr);

  const EditorData extended = EditorData::load(install, true);
  CHECK(listed(extended) == std::vector<std::string>{"MARK10", "MARK14", "MARK18-1", "MARK18-2", "MARK27"});
  CHECK(extended.torpedo_choices.count == 6);
  CHECK(extended.torpedo_choices.extended);
}

TEST_CASE("The extended TORPEDOES dialog grows by a row per type past three", "[editor][torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  std::vector<Image8> shots;
  for (const bool extended : {false, true}) {
    ExtendedFixture fx(install, extended);
    Scenario& s = fx.scenario;
    set_scn_text(s.player().rec.sub_class, "GATO");
    // January 1945: every type in service.
    s.header.month = 0;
    s.header.year = 1945 - kScenarioYearBase;
    s.reset_torpedo_load();
    fx.host.script.emplace_back([&](ui::Gui&) {
      shots.push_back(fx.ui.screen());
      const char* dir = std::getenv("SH_SCREENSHOT_DIR");
      if (dir != nullptr && *dir != '\0') {
        save_bmp(std::filesystem::path(dir) / (extended ? "se_torpedoes_extended.bmp" : "se_torpedoes_original.bmp"),
                 fx.ui.screen(), to_palette(fx.ui.dac()));
      }
    });
    fx.host.script.emplace_back([](ui::Gui& gui) { gui.mouse().post_key(0x011b, 0); });
    CHECK_FALSE(cmd_torpedoes(fx.ui, s, fx.options));
  }
  REQUIRE(shots.size() == 2);
  // The original's dialog (100,80)-(518,429); the extended one, five rows,
  // 44 pixels taller and 22 higher: (100,58)-(518,451).
  CHECK(shots[0].at(300, 440) != shots[1].at(300, 440));
  CHECK(shots[0].at(300, 65) != shots[1].at(300, 65));
}

TEST_CASE("The extended TORPEDOES refuses a fourth type", "[editor][torpedo_types]") {
  SH_REQUIRE_GAME_DATA(install);
  ExtendedFixture fx(install, true);
  Scenario& s = fx.scenario;
  set_scn_text(s.player().rec.sub_class, "GATO");
  s.header.month = 0;
  s.header.year = 1945 - kScenarioYearBase;
  // Mark 14 everywhere.
  s.reset_torpedo_load();
  TorpedoForm f(s, fx.options);
  constexpr std::int32_t kMark10 = 1;
  constexpr std::int32_t kMark14 = 2;
  constexpr std::int32_t kMark18_1 = 3;
  constexpr std::int32_t kMark18_2 = 4;
  constexpr std::int32_t kMark27 = 5;
  REQUIRE(f.tube(0) == kMark14);
  CHECK(f.cycle_tube(0, true) == kMark18_1);
  CHECK(f.cycle_tube(1, true) == kMark18_1);
  CHECK(f.cycle_tube(1, true) == kMark18_2);
  // Mark 14, 18-1 and 18-2 aboard: the Mark 27 and Mark 10 are skipped.
  CHECK(f.cycle_tube(2, true) == kMark18_1);
  CHECK(f.cycle_tube(2, true) == kMark18_2);
  CHECK(f.cycle_tube(2, true) == 0);
  CHECK(f.cycle_tube(2, true) == kMark14);
  CHECK(f.adds_fourth_type(kMark27, -1, -1));
  CHECK(f.adds_fourth_type(kMark10, -1, -1));
  CHECK_FALSE(f.adds_fourth_type(kMark18_2, -1, -1));
  // Nor can a reload row bring one aboard.
  CHECK(f.cycle_reload(true, kMark27, false, true) == 0);
  CHECK(f.reloads(true, kMark27) == 0);
  // The only 18-2 may give its place to the Mark 27.
  CHECK(f.cycle_tube(1, true) == kMark27);
  CHECK_FALSE(f.adds_fourth_type(kMark27, -1, -1));
  CHECK(f.adds_fourth_type(kMark18_2, -1, -1));
}

}  // namespace sh::editor
