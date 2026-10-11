// The editor program: the menu bar read from SE.EXE (File / Run only when
// started by the game), its dispatch driven through the GUI (right click,
// a title, an item), Exit with "Save changes to scenario?", Run handing
// AEONSCEN.TMP over, Alt-Q / QUIT TO DOS? and a screenshot of the start.

#include "editor/editor.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "assets/scn_file.h"
#include "core/install.h"
#include "editor_rig.h"
#include "game/shpath.h"
#include "game_data.h"
#include "ui/menu_bar.h"

namespace sh::editor {
namespace {

using test::EditorRig;
using test::kEditX;
using test::kFilesX;
using test::kTitleY;
using test::menu_item_y;

std::vector<std::string> labels(const ui::PopupMenu& menu) {
  std::vector<std::string> out;
  for (const ui::PopupItem& item : menu.items) {
    out.push_back(item.label);
  }
  return out;
}

}  // namespace

TEST_CASE("The menu bar comes from SE.EXE, with Run only when started by the game", "[editor][menu][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const SeExe se = SeExe::load(install);
  MenuCommand selection = MenuCommand::kNone;
  const EditorMenu from_game(se, true, selection);
  const ui::MenuBar& bar = from_game.bar();
  REQUIRE(bar.items.size() == 3);
  CHECK(bar.shadow);
  CHECK(bar.items[0].label == "?");
  CHECK(bar.items[1].label == "Files");
  CHECK(bar.items[1].x == kFilesX);
  CHECK(bar.items[2].label == "Edit");
  CHECK(bar.items[2].x == kEditX);
  CHECK(labels(*bar.items[1].menu) == std::vector<std::string>{"Run", "New", "Load Historical", "Load Scenario",
                                                                "Save Scenario", "Delete Scenario", "Exit"});
  CHECK(labels(*bar.items[2].menu) == std::vector<std::string>{"Password", "Briefing Text", "Patrol Area",
                                                                "Environment", "Torpedoes", "Equipment", "Options"});
  bar.items[1].menu->items[0].action();
  CHECK(selection == MenuCommand::kRun);
  bar.items[2].menu->items[6].action();
  CHECK(selection == MenuCommand::kOptions);
  bar.items[0].menu->items[0].action();
  CHECK(selection == MenuCommand::kAbout);

  const EditorMenu from_dos(se, false, selection);
  CHECK(labels(*from_dos.bar().items[1].menu).front() == "New");
  CHECK(from_dos.bar().items[1].menu->items.size() == 6);
}

TEST_CASE("File / Exit leaves the editor, asking to save a modified scenario", "[editor][menu]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true);
  rig.then([&] { rig.editor.scenario().modified = true; });
  rig.press(320, 200, ui::kButtonRight);
  rig.screenshot("se_menu_bar");
  rig.click(kFilesX + 15, kTitleY);
  rig.screenshot("se_files_menu");
  rig.click(kFilesX + 100, menu_item_y(6));
  // "Save changes to scenario?": CANCEL stays, DISCARD exits.
  rig.click(203 + 67 + 50, 142 + 169 + 12);
  rig.then([&] { CHECK(rig.editor.running()); });
  rig.menu(kFilesX, 6);
  rig.click(203 + 67 + 50, 142 + 92 + 12);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK(rig.host.script.empty());
}

TEST_CASE("File / Run writes AEONSCEN.TMP and ends with Run", "[editor][menu]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true);
  rig.screenshot("se_start");
  rig.menu(kFilesX, 0);
  rig.key(0x1559);  // YES to "NO SHIPS IN RANGE?" (Exit On Break Contact is on)
  CHECK(rig.editor.run() == EditorExit::kRun);
  const std::filesystem::path file = rig.dir.path() / "CUSTOM" / "AEONSCEN.TMP";
  REQUIRE(std::filesystem::exists(file));
  CHECK(read_file(file) == write_scn_file(rig.editor.scenario().save()));
}

TEST_CASE("Started from DOS, Files starts at New", "[editor][menu]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, false);
  // Item 0 is New: the scenario is replaced, then Exit is item 5.
  rig.then([&] { rig.editor.scenario().header.hour = 3; });
  rig.menu(kFilesX, 0);
  rig.then([&] { CHECK(rig.editor.scenario().header.hour == 12); });
  rig.menu(kFilesX, 5);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK_FALSE(std::filesystem::exists(rig.dir.path() / "CUSTOM" / "AEONSCEN.TMP"));
}

TEST_CASE("Alt-Q asks QUIT TO DOS? and YES ends the game too", "[editor][menu]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true);
  rig.key(0x1000);
  rig.key(0x314e);  // NO
  rig.then([&] { CHECK(rig.editor.running()); });
  rig.key(0x1000);
  rig.key(0x1559);  // YES
  CHECK(rig.editor.run() == EditorExit::kQuitToDos);
}

TEST_CASE("Edit menu items run their dialogs", "[editor][menu]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true);
  // Edit / Briefing Text, cancelled by Esc; '?' / About, closed by Enter.
  rig.menu(kEditX, 1);
  rig.screenshot("se_menu_briefing");
  rig.key(ui::kKeyEsc);
  rig.then([&] { CHECK_FALSE(rig.editor.scenario().modified); });
  rig.menu(5, 0);
  rig.key(ui::kKeyEnter);
  rig.menu(kFilesX, 6);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK(rig.host.script.empty());
}

TEST_CASE("New and the loads rebuild the chart view on the scenario's world", "[editor][menu]") {
  // Regression: the view kept the world a load had freed (the debug
  // build's sanitizers catch its next draw) and its zoom (sb_setup_world
  // resets it to 1 before centring on the player).
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true);
  const auto zoom_in = [&] {
    rig.then([&] {
      rig.editor.chart().view().set_zoom(4 << 16);
      REQUIRE(rig.editor.chart().view().zoom_int() == 4);
    });
  };
  const auto check_view = [&] {
    rig.then([&] {
      CHECK(rig.editor.chart().view().zoom_int() == 1);
    });
  };
  zoom_in();
  rig.menu(kFilesX, 1);  // New
  check_view();
  zoom_in();
  rig.menu(kFilesX, 2);  // Load Historical: HIST01
  rig.click(134 + 14 + 60, 50 + 54 + 1 + 8);
  rig.click(134 + 234 + 50, 50 + 325 + 12);
  check_view();
  rig.then([&] { CHECK(rig.editor.scenario().objects().size() > 1); });
  rig.menu(kFilesX, 6);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK(rig.host.script.empty());
}

}  // namespace sh::editor
