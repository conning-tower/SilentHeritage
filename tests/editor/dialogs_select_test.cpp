// The SELECT dialogs driven through the GUI: the sub class of the selected
// row (SE took the row, an original bug), the chain to SELECT SUB NAME and its
// copy on SELECT only, the ship and aircraft indexes on SELECT and CANCEL.
// SH_SCREENSHOT_DIR receives a picture of each dialog.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "editor/class_sheets.h"
#include "editor/dialogs_select.h"
#include "editor_ui_fixture.h"
#include "game_data.h"

namespace sh::editor {
namespace {

using test::EditorFixture;

constexpr std::int32_t year_of(std::int32_t full) { return full - kScenarioYearBase; }

// Clicks row `row` of the list at (x, y) of the dialog at (dx, dy), its
// rows 16 pixels high.
void click_row(EditorFixture& fx, int dx, int dy, int x, int y, int row) {
  fx.click(dx + x + 100, dy + y + row * 16 + 9);
}

// The buttons (dialog corner + button centre).
constexpr int kSubSelectX = 137 + 252 + 50;
constexpr int kSubButtonsY = 47 + 320 + 12;
constexpr int kNameCancelX = 154 + 16 + 50;
constexpr int kNameSelectX = 154 + 217 + 50;
constexpr int kNameButtonsY = 100 + 225 + 12;
constexpr int kShipSelectX = 137 + 252 + 50;
constexpr int kShipCancelX = 137 + 16 + 50;
constexpr int kShipButtonsY = 10 + 400 + 12;
constexpr int kAirSelectX = 137 + 252 + 50;
constexpr int kAirCancelX = 137 + 16 + 50;
constexpr int kAirButtonsY = 47 + 340 + 12;

}  // namespace

TEST_CASE("SELECT SUB TYPE maps the date-limited row to its class", "[editor][select]") {
  // Regression test for the original bug "Sub class by list position"
  // (SE stored the row as the SUBS.DAT class).
  SH_REQUIRE_INSTALL(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  // May 1945: the BARRACUDA and NARWHAL (classes 1 and 2) left service.
  s.header.month = 4;
  s.header.year = year_of(1945);
  const ClassList list = sub_type_list(fx.data, fx.options, s.header.month, s.header.year, 0);
  REQUIRE(list.entries.size() > 2);
  REQUIRE(list.entries[2] != 2);
  const auto row2 = static_cast<std::int32_t>(list.entries[2]);
  auto sub_class = static_cast<std::int32_t>(list.entries[0]);
  ScnChars<31> name{};
  set_scn_text(name, "(SS-1) Old");

  SECTION("SELECT, then SELECT SUB NAME cancelled") {
    fx.screenshot("se_select_sub_type");
    click_row(fx, 137, 47, 15, 215, 2);
    fx.screenshot("se_select_sub_type_row2");
    fx.click(kSubSelectX, kSubButtonsY);
    fx.screenshot("se_select_sub_name");
    fx.click(kNameCancelX, kNameButtonsY);
    CHECK(select_sub_type(fx.ui, s, fx.options, sub_class, name) == ui::kResultCancel);
    CHECK(sub_class == row2);
    CHECK(scn_text(name) == "(SS-1) Old");
  }
  SECTION("SELECT SUB NAME lists the boats of the selected class") {
    click_row(fx, 137, 47, 15, 215, 2);
    fx.click(kSubSelectX, kSubButtonsY);
    click_row(fx, 154, 100, 14, 54, 0);
    fx.click(kNameSelectX, kNameButtonsY);
    CHECK(select_sub_type(fx.ui, s, fx.options, sub_class, name) == ui::kResultAccept);
    CHECK(sub_class == row2);
    CHECK(scn_text(name) == sub_name_list(fx.data, static_cast<std::size_t>(row2), "").rows.front());
  }
  SECTION("CANCEL keeps the class") {
    sub_class = row2;
    click_row(fx, 137, 47, 15, 215, 0);
    fx.key(ui::kKeyEsc);
    CHECK(select_sub_type(fx.ui, s, fx.options, sub_class, name) == ui::kResultCancel);
    CHECK(sub_class == row2);
  }
}

TEST_CASE("SELECT SUB NAME copies the boat on SELECT only", "[editor][select]") {
  SH_REQUIRE_INSTALL(install);
  EditorFixture fx(install);
  const auto gato = static_cast<std::int32_t>(*fx.data.find_sub("GATO"));
  const ClassList boats = sub_name_list(fx.data, static_cast<std::size_t>(gato), "");
  REQUIRE(boats.rows.size() > 1);
  ScnChars<31> name{};
  set_scn_text(name, boats.rows[0]);
  click_row(fx, 154, 100, 14, 54, 1);
  fx.click(kNameCancelX, kNameButtonsY);
  CHECK(select_sub_name(fx.ui, fx.data, gato, name) == ui::kResultCancel);
  CHECK(scn_text(name) == boats.rows[0]);
  click_row(fx, 154, 100, 14, 54, 1);
  fx.click(kNameSelectX, kNameButtonsY);
  CHECK(select_sub_name(fx.ui, fx.data, gato, name) == ui::kResultAccept);
  CHECK(scn_text(name) == boats.rows[1]);
}

TEST_CASE("SELECT SHIP TYPE maps the row to SHIPS.DAT, -1 on CANCEL", "[editor][select]") {
  SH_REQUIRE_INSTALL(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.header.year = year_of(1943);
  const ClassList list = ship_type_list(fx.data, fx.options, fx.ui.resources().small, s.header.month, s.header.year, -1);
  REQUIRE(list.entries.size() > 3);
  std::int32_t ship = static_cast<std::int32_t>(list.entries[0]);
  fx.screenshot("se_select_ship_type");
  click_row(fx, 137, 10, 15, 292, 3);
  fx.screenshot("se_select_ship_type_row3");
  fx.click(kShipSelectX, kShipButtonsY);
  select_ship_type(fx.ui, s, fx.options, ship);
  CHECK(ship == static_cast<std::int32_t>(list.entries[3]));
  fx.click(kShipCancelX, kShipButtonsY);
  select_ship_type(fx.ui, s, fx.options, ship);
  CHECK(ship == -1);
}

TEST_CASE("SELECT AIRCRAFT TYPE maps the row to AIRCRAFT.DAT, also on CANCEL", "[editor][select]") {
  SH_REQUIRE_INSTALL(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.header.year = year_of(1943);
  const ClassList list = aircraft_type_list(fx.data, fx.options, s.header.month, s.header.year, -1);
  REQUIRE(list.entries.size() > 2);
  std::int32_t aircraft = static_cast<std::int32_t>(list.entries[0]);
  fx.screenshot("se_select_aircraft_type");
  click_row(fx, 137, 47, 15, 235, 2);
  fx.click(kAirSelectX, kAirButtonsY);
  CHECK(select_aircraft_type(fx.ui, s, fx.options, aircraft) == ui::kResultAccept);
  CHECK(aircraft == static_cast<std::int32_t>(list.entries[2]));
  aircraft = static_cast<std::int32_t>(list.entries[0]);
  click_row(fx, 137, 47, 15, 235, 1);
  fx.click(kAirCancelX, kAirButtonsY);
  CHECK(select_aircraft_type(fx.ui, s, fx.options, aircraft) == ui::kResultCancel);
  CHECK(aircraft == static_cast<std::int32_t>(list.entries[1]));
}

}  // namespace sh::editor
