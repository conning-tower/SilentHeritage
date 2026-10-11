// SAVE SCENARIO and the Load / Delete dialog: the name entry's rules
// (upper case, 8 characters, cut at a space), a list click copying a name,
// CANCEL and an empty name restoring it, the list's result, the button
// greyed without entries and the briefing view's arrows. SH_SCREENSHOT_DIR
// receives pictures of both.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include "editor/dialogs_file.h"
#include "editor_ui_fixture.h"
#include "game_data.h"

namespace sh::editor {
namespace {

using test::EditorFixture;

void type(EditorFixture& fx, std::string_view text) {
  for (const char c : text) {
    fx.key(static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
  }
}

// SAVE SCENARIO at (164, 110).
constexpr int kSaveX = 164 + 178 + 50;
constexpr int kSaveY = 110 + 245 + 12;
constexpr int kSaveCancelX = 164 + 35 + 50;
constexpr int save_row_y(int row) { return 110 + 54 + 1 + row * 16 + 8; }
// The Load / Delete dialog at (134, 50).
constexpr int kListButtonX = 134 + 234 + 50;
constexpr int kListButtonY = 50 + 325 + 12;
constexpr int list_row_y(int row) { return 50 + 54 + 1 + row * 16 + 8; }
constexpr int kDownX = 134 + 337 + 10;
constexpr int kDownY = 50 + 288 + 12;
constexpr int kUpY = 50 + 212 + 12;

std::string long_briefing() {
  std::string s;
  for (int i = 1; i <= 20; ++i) {
    s += "Line " + std::to_string(i) + " of the briefing.\n";
  }
  return s;
}

}  // namespace

TEST_CASE("The save name is upper-cased and cut at a space", "[editor][file]") {
  std::string name = "my scen";
  save_name_on_done(name);
  CHECK(name == "MY");
  name = "Duck";
  save_name_on_done(name);
  CHECK(name == "DUCK");
}

TEST_CASE("SAVE SCENARIO edits the scenario name", "[editor][file]") {
  SH_REQUIRE_INSTALL(install);
  EditorFixture fx(install);
  const std::vector<std::string> names{"ALPHA", "BRAVO", "CHARLIE"};

  SECTION("Typed and accepted by Enter, 8 characters at most") {
    std::string name;
    type(fx, "sitduck long");
    fx.screenshot("se_save_scenario");
    fx.key(ui::kKeyEnter);
    CHECK(run_save_scenario_dialog(fx.ui, names, name));
    CHECK(name == "SITDUCK");
    std::string longer;
    type(fx, "abcdefghij");
    fx.key(ui::kKeyEnter);
    CHECK(run_save_scenario_dialog(fx.ui, names, longer));
    CHECK(longer == "ABCDEFGH");
  }
  SECTION("A list click copies the name") {
    std::string name = "OLD";
    fx.click(164 + 34 + 60, save_row_y(1));
    fx.click(kSaveX, kSaveY);
    CHECK(run_save_scenario_dialog(fx.ui, names, name));
    CHECK(name == "BRAVO");
  }
  SECTION("CANCEL restores the name") {
    std::string name = "OLD";
    type(fx, "X");
    fx.click(kSaveCancelX, kSaveY);
    CHECK_FALSE(run_save_scenario_dialog(fx.ui, names, name));
    CHECK(name == "OLD");
  }
  SECTION("An empty name is refused") {
    std::string name = "OLD";
    fx.key(ui::kKeyBackspace);
    fx.key(ui::kKeyBackspace);
    fx.key(ui::kKeyBackspace);
    fx.key(ui::kKeyEnter);
    CHECK_FALSE(run_save_scenario_dialog(fx.ui, names, name));
    CHECK(name == "OLD");
  }
}

TEST_CASE("The Load / Delete dialog returns the chosen scenario", "[editor][file]") {
  SH_REQUIRE_INSTALL(install);
  EditorFixture fx(install);
  const std::vector<ScenarioListEntry> entries{
      {"HIST01.SCN", "Pearl Harbor\n", long_briefing()},
      {"HIST02.SCN", "Wake Island\n", "Short."},
      {"HIST03.SCN", "Java Sea\n", "Also short."},
  };

  SECTION("A row, then the button") {
    fx.screenshot("se_load_delete");
    fx.click(kDownX, kDownY);  // the long briefing scrolls
    fx.click(kDownX, kDownY);
    fx.screenshot("se_load_delete_scrolled");
    fx.click(kDownX, kUpY);
    fx.click(134 + 14 + 60, list_row_y(2));
    fx.click(kListButtonX, kListButtonY);
    const std::optional<std::size_t> chosen =
        run_scenario_list_dialog(fx.ui, ScenarioListMode::kLoadHistorical, entries);
    REQUIRE(chosen);
    CHECK(*chosen == 2);
    // The editor's skin is back.
    CHECK(&fx.ui.gui().skin() == &fx.ui.skin());
  }
  SECTION("Entry 0 is selected at first") {
    fx.key(ui::kKeyTab);
    fx.click(kListButtonX, kListButtonY);
    const std::optional<std::size_t> chosen =
        run_scenario_list_dialog(fx.ui, ScenarioListMode::kDeleteScenario, entries);
    REQUIRE(chosen);
    CHECK(*chosen == 0);
  }
  SECTION("CANCEL") {
    fx.key(ui::kKeyEsc);
    CHECK_FALSE(run_scenario_list_dialog(fx.ui, ScenarioListMode::kLoadScenario, entries));
  }
  SECTION("Without entries the button does nothing") {
    fx.screenshot("se_load_delete_empty");
    fx.click(kListButtonX, kListButtonY);
    fx.key(ui::kKeyEsc);
    CHECK_FALSE(run_scenario_list_dialog(fx.ui, ScenarioListMode::kLoadScenario, {}));
  }
}

}  // namespace sh::editor
