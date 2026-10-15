// The editor on a wide screen (`Features::wide_screen`): the chart's map
// across the window's width with its panels kept on it, the menu bar across
// the screen, the dialogs centred; without the improvement, the original's
// 640-wide screen whatever the window.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

#include "chart_rig.h"
#include "core/features.h"
#include "editor/chart_screen.h"
#include "editor/editor.h"
#include "editor_rig.h"
#include "game_data.h"
#include "ui/dialog.h"

namespace sh::editor {
namespace {

using test::ChartRig;
using test::EditorRig;
using test::kFilesX;
using test::kTitleY;
using test::menu_item_y;

// A 16:9 window's screen, and where a 640-wide frame is centred on it.
constexpr int kWide = 854;
constexpr int kFrameLeft = (kWide - kScreenWidth) / 2;

// "Save changes to scenario?" (`ask_save_changes`, at 203, 142 of the
// 640-wide screen): its CANCEL and DISCARD buttons' centres.
constexpr std::array<int, 2> kSaveChangesCancel{203 + 67 + 50, 142 + 169 + 12};
constexpr std::array<int, 2> kSaveChangesDiscard{203 + 67 + 50, 142 + 92 + 12};

Features wide_screen() {
  Features f;
  f.wide_screen = true;
  return f;
}

// Drags the open panel by its title strip so that its left edge goes to
// `x`.
void drag_panel(ChartRig& rig, int x) {
  const ui::Dialog* d = rig.chart->panel_dialog();
  REQUIRE(d != nullptr);
  const int grab_x = d->x() + 100;
  const int grab_y = d->y() + 10;
  rig.gui.mouse().update(grab_x, grab_y, 0, 0);
  rig.frames(2);
  rig.gui.mouse().update(grab_x, grab_y, ui::kButtonLeft, 0);
  rig.frames(2);
  rig.gui.mouse().update(x + 100, grab_y, ui::kButtonLeft, 0);
  rig.frames(2);
  rig.gui.mouse().update(x + 100, grab_y, 0, 0);
  rig.frames(2);
}

}  // namespace

TEST_CASE("Wide screen: the chart is laid out again across the screen's width", "[editor][wide]") {
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install, "SCEN/HIST01.SCN");
  ChartScreen& chart = *rig.chart;
  REQUIRE(chart.shown_panel() == EditorPanel::kToolbox);
  const std::int32_t centre_y = chart.view().center_y();

  chart.set_width(kWide);
  rig.frames(3);
  CHECK(rig.screen.width() == kWide);
  CHECK(chart.view().x1() == kWide - 1);
  CHECK(chart.view().y1() == ChartScreen::kViewY1);
  // The map is drawn across, at the same height (its centre moved only to
  // keep the view on the map).
  CHECK(chart.view().center_y() == centre_y);
  bool drawn = false;
  for (int y = 0; y <= ChartScreen::kViewY1 && !drawn; ++y) {
    drawn = rig.screen.at(kWide - 100, y) != 0;
  }
  CHECK(drawn);
  rig.save("editor_wide_chart");

  // The map takes the pointer there: the status bar reads its position.
  chart.mouse_moved(kWide - 20, 200);
  const WorldPoint w = chart.screen_to_world(kWide - 20, 200);
  CHECK(chart.status_latitude() == format_latlon(rig.data.se, rig.scenario.world().world().latitude_at(w.y)));
  // The toolbox stays where it was, its buttons working: NEXT selects the
  // next object.
  const ui::Dialog* toolbox = chart.panel_dialog();
  REQUIRE(toolbox != nullptr);
  CHECK(toolbox->x() == 0);
  rig.click_panel(258 + 12, 43 + 8);
  CHECK(rig.scenario.selected() == rig.scenario.objects()[1].get());

  // Dragged to the wide screen's right edge, the toolbox comes back onto
  // a narrower map.
  drag_panel(rig, kWide);
  REQUIRE(chart.panel_dialog() != nullptr);
  CHECK(chart.panel_dialog()->x() + chart.panel_dialog()->width() == kWide);
  chart.set_width(kScreenWidth);
  rig.frames(3);
  CHECK(rig.screen.width() == kScreenWidth);
  CHECK(chart.view().x1() == ChartScreen::kViewX1);
  REQUIRE(chart.panel_dialog() != nullptr);
  CHECK(chart.panel_dialog()->x() + chart.panel_dialog()->width() == kScreenWidth);
  CHECK(chart.shown_panel() == EditorPanel::kToolbox);
}

TEST_CASE("Wide screen: the editor's map spans the window, the menu bar too, the dialogs are centred",
          "[editor][wide]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true, wide_screen(), kWide);
  rig.then([&] {
    CHECK(rig.editor.ui().screen().width() == kWide);
    CHECK(rig.editor.chart().view().x1() == kWide - 1);
    rig.editor.scenario().modified = true;
  });
  rig.screenshot("se_wide_start");
  rig.press(kWide - 40, 200, ui::kButtonRight);
  rig.then([&] {
    // The bar's fill runs to the screen's right edge, where its frame is.
    const Image8& screen = rig.editor.ui().screen();
    const std::uint8_t fill = screen.at(300, 10);
    for (int x = 300; x < kWide - 8; ++x) {
      REQUIRE(screen.at(x, 10) == fill);
    }
    CHECK(screen.at(kWide - 1, 10) != fill);
  });
  rig.screenshot("se_wide_menu_bar");
  // The titles stay where they were: Files / Exit.
  rig.click(kFilesX + 15, kTitleY);
  rig.click(kFilesX + 100, menu_item_y(6));
  rig.screenshot("se_wide_save_changes");
  // "Save changes to scenario?" centred: CANCEL stays, DISCARD exits.
  rig.click(kFrameLeft + kSaveChangesCancel[0], kSaveChangesCancel[1]);
  rig.then([&] { CHECK(rig.editor.running()); });
  rig.menu(kFilesX, 6);
  rig.click(kFrameLeft + kSaveChangesDiscard[0], kSaveChangesDiscard[1]);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK(rig.host.script.empty());
}

TEST_CASE("Wide screen: the editor follows the window's width while it runs", "[editor][wide]") {
  SH_REQUIRE_INSTALL(install);
  EditorRig rig(install, true, wide_screen(), kWide);
  rig.then([&] { rig.host.width = kScreenWidth; });
  rig.then([&] {
    CHECK(rig.editor.ui().screen().width() == kScreenWidth);
    CHECK(rig.editor.chart().view().x1() == ChartScreen::kViewX1);
    rig.host.width = 1000;
  });
  rig.then([&] {
    CHECK(rig.editor.ui().screen().width() == 1000);
    CHECK(rig.editor.chart().view().x1() == 999);
    CHECK(rig.editor.chart().shown_panel() == EditorPanel::kToolbox);
  });
  rig.menu(kFilesX, 6);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK(rig.host.script.empty());
}

TEST_CASE("Without the wide screen the editor keeps the 640-wide screen on a wide window", "[editor][wide]") {
  SH_REQUIRE_INSTALL(install);
  Features features = Features::extended();
  features.wide_screen = false;
  EditorRig rig(install, true, features, kWide);
  rig.then([&] {
    CHECK(rig.editor.ui().screen().width() == kScreenWidth);
    CHECK(rig.editor.chart().view().x1() == ChartScreen::kViewX1);
    rig.editor.scenario().modified = true;
  });
  // The dialogs where the original has them.
  rig.menu(kFilesX, 6);
  rig.click(kSaveChangesDiscard[0], kSaveChangesDiscard[1]);
  CHECK(rig.editor.run() == EditorExit::kExit);
  CHECK(rig.host.script.empty());
}

}  // namespace sh::editor
