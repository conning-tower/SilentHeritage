// PATROL AREA and the ORDERS popup: the zones listed with and without
// "Limit by date: Patrol Areas", the dated map, a zone chosen through the
// list or the map changing the world, CANCEL and "Save changes?" CANCEL
// keeping it, the black screen of a press on the map, and the popup's
// cells and results. SH_SCREENSHOT_DIR receives pictures of both.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/dialogs_patrol.h"
#include "editor_ui_fixture.h"
#include "game_data.h"

namespace sh::editor {
namespace {

using test::EditorFixture;

constexpr std::int32_t year_of(std::int32_t full) { return full - kScenarioYearBase; }

// Whether zone `z` is active on (`month` 0-based, `year` since 1930), as
// months counted from year 0.
bool active(const PatrolZone& z, std::int32_t month, std::int32_t year) {
  const std::int32_t at = (year + kScenarioYearBase) * 12 + month;
  return z.first_year * 12 + z.first_month - 1 <= at && at <= z.last_year * 12 + z.last_month - 1;
}

// The screen centre of list row `row` (on screen) of PATROL AREA.
constexpr int kListX = 432 + 60;
constexpr int list_row_y(int row) { return 100 + 1 + row * 16 + 8; }
constexpr int kOkX = 484 + 50;
constexpr int kOkY = 381 + 12;
constexpr int kCancelY = 414 + 12;

// Lets the dialog's fade-in (which presents frames, so runs script steps)
// end before the mouse moves.
void wait_fade_in(EditorFixture& fx) {
  for (int i = 0; i < 4; ++i) {
    fx.then([] {});
  }
}

}  // namespace

TEST_CASE("PATROL AREA lists the zones with a version 7 world, limited by date", "[editor][patrol]") {
  SH_REQUIRE_GAME_DATA(install);
  const EditorData& data = test::editor_data(install);
  const std::vector<PatrolZone>& zones = data.zones;
  const std::vector<bool> flags = patrol_zone_flags(install, data.se, zones);
  REQUIRE(flags.size() == zones.size());
  REQUIRE(std::ranges::count(flags, true) > 1);

  const PatrolAreaList all = patrol_area_list(zones, flags, "WORLD-1.WLD", 11, year_of(1941), false);
  CHECK(all.zones.size() == static_cast<std::size_t>(std::ranges::count(flags, true)));
  CHECK(zones[all.zones[all.current]].world == "WORLD-1.WLD");
  // A world in lower case matches no zone: the caller upper-cases it.
  CHECK(patrol_area_list(zones, flags, "world-1.wld", 11, year_of(1941), false).current == 0);

  for (const auto& [month, year] : {std::pair{11, 1941}, {5, 1942}, {0, 1944}, {7, 1945}}) {
    const PatrolAreaList limited = patrol_area_list(zones, flags, "", month, year_of(year), true);
    CHECK_FALSE(limited.zones.empty());
    for (std::size_t i = 0; i < zones.size(); ++i) {
      const bool listed = std::ranges::find(limited.zones, i) != limited.zones.end();
      CHECK(listed == (flags[i] && active(zones[i], month, year_of(year))));
    }
  }
}

TEST_CASE("PATROL AREA picks the Pacific map by the date", "[editor][patrol]") {
  SH_REQUIRE_GAME_DATA(install);
  const SeExe& se = test::editor_data(install).se;
  CHECK(campaign_month_index(11, year_of(1941)) == 0);
  CHECK(campaign_month_index(7, year_of(1945)) == 44);
  CHECK(dated_pacific_map(se, 11, year_of(1941)) == "SPAC_A.SHP");
  CHECK(dated_pacific_map(se, 7, year_of(1942)) == "SPAC_H.SHP");   // month 8
  CHECK(dated_pacific_map(se, 8, year_of(1942)) == "SPAC_I.SHP");   // month 9 .. 19
  CHECK(dated_pacific_map(se, 6, year_of(1943)) == "SPAC_I.SHP");
  CHECK(dated_pacific_map(se, 7, year_of(1945)) == "SPAC_S.SHP");
}

TEST_CASE("PATROL AREA changes the world on OK only", "[editor][patrol]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  const std::vector<PatrolZone>& zones = fx.data.zones;
  const PatrolAreaList list =
      patrol_area_list(zones, patrol_zone_flags(install, fx.data.se, zones), "WORLD-1.WLD", s.header.month,
                       s.header.year, true);
  REQUIRE(list.zones.size() > 1);
  REQUIRE(list.current == 0);
  const PatrolZone& other = zones[list.zones[1]];
  REQUIRE(other.world != "WORLD-1.WLD");
  s.add_airstrike();
  s.modified = false;
  REQUIRE(s.objects().size() == 2);

  SECTION("A list row, then OK") {
    wait_fade_in(fx);
    fx.screenshot("se_patrol_area");
    fx.click(kListX, list_row_y(1));
    fx.screenshot("se_patrol_area_selected");
    fx.click(kOkX, kOkY);
    CHECK(cmd_patrol_area(fx.ui, s, fx.options, [] { return -1; }));
    CHECK(scn_text(s.header.world) == other.world);
    CHECK(s.world().zone() != nullptr);
    REQUIRE(s.objects().size() == 1);
    CHECK(s.selected() == &s.player());
    // The palette comes back.
    CHECK(fx.ui.dac() == fx.ui.palette());
  }
  SECTION("Hovering a zone on the map selects it") {
    const auto& r = other.orders_rect;
    const int x = (r[0] + r[2]) / 2 + 11;
    const int y = (r[1] + r[3]) / 2 + 49;
    wait_fade_in(fx);
    fx.host.script.emplace_back([x, y](ui::Gui& gui) { gui.mouse().update(x, y, 0, 0); });
    fx.click(kOkX, kOkY);
    CHECK(cmd_patrol_area(fx.ui, s, fx.options, [] { return -1; }));
    CHECK(scn_text(s.header.world) == other.world);
  }
  SECTION("CANCEL keeps everything") {
    fx.click(kListX, list_row_y(1));
    fx.click(kOkX, kCancelY);
    CHECK_FALSE(cmd_patrol_area(fx.ui, s, fx.options, [] { return -1; }));
    CHECK(scn_text(s.header.world) == "WORLD-1.WLD");
    CHECK(s.objects().size() == 2);
  }
  SECTION("OK on the current zone keeps the objects") {
    fx.click(kOkX, kOkY);
    CHECK_FALSE(cmd_patrol_area(fx.ui, s, fx.options, [] { return -1; }));
    CHECK(s.objects().size() == 2);
  }
  SECTION("Pressing on a zone accepts it") {
    // Regression test for the original bug "PATROL AREA map click" (SE
    // blacked the screen out and left the dialog open).
    const auto& r = other.orders_rect;
    const int x = (r[0] + r[2]) / 2 + 11;
    const int y = (r[1] + r[3]) / 2 + 49;
    wait_fade_in(fx);
    fx.host.script.emplace_back([x, y](ui::Gui& gui) { gui.mouse().update(x, y, ui::kButtonLeft, 0); });
    fx.host.script.emplace_back([x, y](ui::Gui& gui) { gui.mouse().update(x, y, 0, 0); });
    CHECK(cmd_patrol_area(fx.ui, s, fx.options, [] { return -1; }));
    CHECK(scn_text(s.header.world) == other.world);
    CHECK(fx.ui.dac() == fx.ui.palette());
  }
}

TEST_CASE("PATROL AREA asks to save changes first", "[editor][patrol]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  Scenario& s = fx.scenario;
  s.modified = true;
  int saves = 0;
  SECTION("CANCEL: no dialog") {
    fx.click(203 + 67 + 50, 142 + 169 + 12);
    CHECK_FALSE(cmd_patrol_area(fx.ui, s, fx.options, [&] { return ++saves; }));
    CHECK(saves == 0);
    CHECK(fx.host.script.empty());
  }
  SECTION("SAVE saves, then the dialog runs") {
    fx.click(203 + 67 + 50, 142 + 131 + 12);
    fx.click(kOkX, kCancelY);
    CHECK_FALSE(cmd_patrol_area(fx.ui, s, fx.options, [&] { return ++saves; }));
    CHECK(saves == 1);
  }
}

TEST_CASE("The ORDERS popup returns the chosen order", "[editor][patrol]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorFixture fx(install);
  const std::vector<std::string> labels{"None", "Patrol", "Escort"};
  const std::vector<bool> disabled{false, false, true};
  // Height 0x2e + 3 x 0x17 + 0xc = 127, centred at y 176.
  const auto cell = [](int i) { return std::pair{203 + 12 + 102, 176 + 46 + 23 * i + 10}; };
  const VgaPalette before = fx.ui.dac();

  SECTION("A click") {
    fx.screenshot("se_orders");
    const auto [x, y] = cell(1);
    fx.click(x, y);
    CHECK(orders_popup_run(fx.ui, labels, disabled, 0) == 1);
    CHECK(fx.ui.dac() == before);
  }
  SECTION("A greyed cell does nothing; Esc cancels") {
    const auto [x, y] = cell(2);
    fx.click(x, y);
    fx.key(ui::kKeyEsc);
    CHECK(orders_popup_run(fx.ui, labels, disabled, 0) == -3);
  }
  SECTION("Enter takes the initial cell") {
    fx.key(ui::kKeyEnter);
    CHECK(orders_popup_run(fx.ui, labels, disabled, 1) == 1);
  }
}

}  // namespace sh::editor
