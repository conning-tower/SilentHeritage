// Tests of the chart station: the view's transforms at each zoom against
// the original formulas, the zoom keys and box, follow mode, the waypoint
// rules, and screenshots of the map (SH_SCREENS).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "game/stations/chart.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

// The original's 16.16 helpers, written out (`imul; add 0x8000` and the
// guarded `idiv`), to check the view against.
std::int64_t mul(std::int64_t a, std::int64_t b) { return (a * b + 0x8000) >> 16; }
std::int64_t div(std::int64_t a, std::int64_t b) { return (a * 65536) / b; }

ChartStation& open_chart(ControlRig& rig) {
  auto chart = std::make_unique<ChartStation>(rig.screen->env());
  ChartStation& ref = *chart;
  rig.screen->install(StationId::kChart, std::move(chart));
  // Away from the screen edges, which scroll the chart.
  rig.gui.mouse().update(320, 200, 0, 0);
  rig.frames(1);
  rig.settle();
  rig.key(0x3f00);  // F5
  rig.settle();
  rig.frames(2);
  return ref;
}

}  // namespace

TEST_CASE("Chart scale legend and value", "[stations][content]") {
  SH_REQUIRE_GAME_EXE();
  CHECK(chart_scale_value(1) == "100");  // 176000 yd = 100 statute miles
  CHECK(chart_scale_legend(1) == "STATUTE MILES");
  CHECK(chart_scale_value(16) == "6");   // 11000 yd
  CHECK(chart_scale_value(32) == "550");  // 5500 yd, in tens
  CHECK(chart_scale_legend(32) == "(x10) YARDS");
}

TEST_CASE("Chart view transforms follow the original formulas", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  const World& world = rig.mission().world();
  ChartView view(world, 0, 0, 0x27f, 0x1ca);
  const std::int64_t w = world.chart.width - 1;  // the declared width
  const std::int64_t h = world.chart.height;
  const std::int64_t cell = world.cell_size();
  const std::int64_t kx = div(w << 16, static_cast<std::int64_t>(world.grid.width - 1) << 16);
  const std::int64_t ky = div(h << 16, static_cast<std::int64_t>(world.grid.height - 1) << 16);
  const fx::Vec3 sub = rig.mission().player().position();
  for (std::int32_t zoom : {1, 2, 4, 8, 16, 64, 256}) {
    INFO("zoom " << zoom);
    view.set_zoom(zoom << 16);
    const auto c = view.world_to_chart(sub.x, sub.z);
    view.set_center(c[0], c[1]);
    // chart_view_set_zoom: half extents and step.
    CHECK(view.half_width() == static_cast<std::int32_t>(div(mul(640LL << 16, 0x10000), zoom << 16) >> 1));
    CHECK(view.half_height() == static_cast<std::int32_t>(div(mul(459LL << 16, 0x10000), zoom << 16) >> 1));
    CHECK(view.step() == static_cast<std::int32_t>(div(0x10000, zoom << 16)));
    // world_to_chart_xy.
    const std::int64_t px = div(mul(sub.x, kx), cell);
    const std::int64_t py = div(mul(sub.z, ky), cell);
    const std::int64_t sx16 = div(px - (view.center_x() - view.half_width()), view.step());
    const std::int64_t sy16 = div(((h << 16) - py) - (view.center_y() - view.half_height()), view.step());
    const ChartPoint p = view.world_to_screen(sub.x, sub.z);
    CHECK(p.x16 == sx16);
    CHECK(p.y16 == sy16);
    // Centred on the boat unless clamped at the chart's edge.
    if (view.center_x() == c[0]) {
      CHECK(std::abs(p.x - 320) <= 1);
    }
    if (view.center_y() == c[1]) {
      CHECK(std::abs(p.y - 229) <= 1);
    }
    // screen_to_world inverts it within a chart pixel's worth of units.
    const auto back = view.screen_to_world(p.x, p.y);
    const std::int64_t tolerance = cell * 2 / zoom + 2 * cell / std::max<std::int64_t>(1, kx >> 16);
    CHECK(std::abs(back[0] - sub.x) <= tolerance);
    CHECK(std::abs(back[1] - sub.z) <= tolerance);
  }
}

TEST_CASE("The chart opens on F5 centred on the boat", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  REQUIRE(rig.screen->loop().open_station() == &chart);
  CHECK(chart.panel() == ChartPanel::kMap);
  CHECK(chart.view().zoom() == 0x10000);
  rig.save("chart_1x");
}

TEST_CASE("The zoom box opens on Z and zooms where clicked", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  rig.key(0x2c5a);  // Z
  CHECK(chart.zoom_box() == 2);
  CHECK(rig.screen->ui_state().pause != 0);
  rig.key(0x2c5a);
  CHECK(chart.zoom_box() == 4);
  rig.key(0x2d58);  // X shrinks the box
  CHECK(chart.zoom_box() == 2);
  rig.key(0x2c5a);
  rig.key(0x2c5a);
  rig.key(0x2c5a);
  CHECK(chart.zoom_box() == 16);
  rig.gui.mouse().update(200, 150, 0, 0);
  rig.frames(2);
  rig.save("chart_zoom_box");
  const auto target = chart.view().screen_to_chart(200, 150);
  rig.click(200, 150);
  CHECK(chart.zoom_box() == 0);
  CHECK(chart.view().zoom() == 16 << 16);
  CHECK(chart.view().center_x() == target[0]);
  CHECK(chart.view().center_y() == target[1]);
  CHECK(rig.screen->ui_state().pause == 0);
  // X without a box halves the zoom.
  rig.key(0x2d58);
  CHECK(chart.view().zoom() == 8 << 16);
}

TEST_CASE("Follow mode keeps the boat in the middle", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  chart.view().set_zoom(64 << 16);
  chart.toggle_follow();
  CHECK(rig.options.chart_follow_sub != 0);
  rig.frames(10, 10);  // the view polls every 25 ticks
  const fx::Vec3 sub = rig.mission().player().position();
  const ChartPoint p = chart.view().world_to_screen(sub.x, sub.z);
  CHECK(std::abs(p.x - 320) <= 1);
  CHECK(std::abs(p.y - 229) <= 1);
  rig.save("chart_64x_follow");
  chart.toggle_follow();
  CHECK(rig.options.chart_follow_sub == 0);
}

TEST_CASE("Waypoints go in deep water with no land on the way", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  chart.view().set_zoom(8 << 16);
  const fx::Vec3 sub = rig.mission().player().position();
  const auto c = chart.view().world_to_chart(sub.x, sub.z);
  chart.view().set_center(c[0], c[1]);
  chart.start_plotting();
  rig.frames(2);
  CHECK(chart.panel() == ChartPanel::kPlot);
  CHECK(chart.click_mode() == ChartClickMode::kPlot);
  const SeaFloor& floor = rig.mission().floor();
  const std::int32_t draft = rig.mission().player().sub().draft_ft;
  // A deep water point and a land point on screen.
  std::optional<std::array<int, 2>> water;
  std::optional<std::array<int, 2>> land;
  for (int y = 20; y < 440 && (!water || !land); y += 7) {
    for (int x = 20; x < 620; x += 7) {
      const auto w = chart.view().screen_to_world(x, y);
      const std::int32_t height = floor.height_at(w[0], w[1]);
      if (!land && height > 0) {
        land = std::array<int, 2>{x, y};
      }
      if (!water && height < -draft * 16 * 4 && std::abs(x - 320) < 80 && std::abs(y - 229) < 80) {
        water = std::array<int, 2>{x, y};
      }
    }
  }
  REQUIRE(water);
  CHECK(chart.add_waypoint((*water)[0], (*water)[1]));
  CHECK(rig.mission().player().route().count == 1);
  CHECK(rig.mission().player().following_route());
  if (land) {
    CHECK_FALSE(chart.add_waypoint((*land)[0], (*land)[1]));
    CHECK(rig.mission().player().route().count == 1);
  }
  rig.frames(30, 10);
  rig.save("chart_8x_course");
  // ESC ends the plotting, the course stays.
  rig.key(ui::kKeyEsc);
  CHECK(chart.panel() == ChartPanel::kMap);
  CHECK(chart.click_mode() == ChartClickMode::kSelectTarget);
  CHECK(rig.mission().end_code() == 0);
  CHECK(rig.mission().player().route().count == 1);
  chart.clear_course();
  CHECK(rig.mission().player().route().count == 0);
  CHECK_FALSE(rig.mission().player().following_route());
}

TEST_CASE("The chart shows the ships from 8x", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  REQUIRE(!rig.mission().ships().empty());
  const fx::Vec3 ship = rig.mission().ships().front()->position();
  for (std::int32_t zoom : {32, 256}) {
    chart.view().set_zoom(zoom << 16);
    const auto c = chart.view().world_to_chart(ship.x, ship.z);
    chart.view().set_center(c[0], c[1]);
    rig.key(0x2c5a);  // the box, then a click redraws at the box's zoom
    const ChartPoint p = chart.view().world_to_screen(ship.x, ship.z);
    rig.click(p.x, p.y);
    CHECK(chart.view().zoom() == zoom * 2 << 16);
    rig.frames(4, 10);
    rig.save(zoom == 32 ? "chart_64x_ships" : "chart_512x_ship");
  }
  // A click on a ship makes it the TDC target.
  const ChartPoint p = chart.view().world_to_screen(ship.x, ship.z);
  rig.click(p.x, p.y);
  CHECK(rig.mission().player_combat().tdc.target() == rig.mission().ships().front());
}

TEST_CASE("The chart draws the coast tiles above 6x", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  const SeaFloor& floor = rig.mission().floor();
  std::optional<std::array<std::int32_t, 2>> land;
  for (int y = 20; y < 440 && !land; y += 3) {
    for (int x = 20; x < 620; x += 3) {
      const auto w = chart.view().screen_to_world(x, y);
      if (floor.height_at(w[0], w[1]) > 0) {
        land = w;
        break;
      }
    }
  }
  REQUIRE(land);
  chart.view().set_zoom(8 << 16);
  const auto c = chart.view().world_to_chart((*land)[0], (*land)[1]);
  chart.view().set_center(c[0], c[1]);
  CHECK(chart.view().tiled());
  rig.key(0x2c5a);
  const ChartPoint p = chart.view().world_to_screen((*land)[0], (*land)[1]);
  rig.click(p.x, p.y);
  CHECK(chart.view().zoom() == 16 << 16);
  rig.frames(2);
  rig.save("chart_16x_coast");
}

TEST_CASE("The NAVIGATION button shows the nav panel", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  const auto at = chart.panel_position();
  rig.click(at[0] + 0x8b + 10, at[1] + 0x45 + 8);
  rig.frames(2);
  CHECK(chart.panel() == ChartPanel::kNavigation);
  rig.save("chart_nav_panel");
  // Its MAP CONTROLS button goes back.
  rig.click(0x16 + 0x6a + 10, 0x59 + 0x146 + 8);
  rig.frames(2);
  CHECK(chart.panel() == ChartPanel::kMap);
}

TEST_CASE("Pointing at the boat shows its position", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  const PlayerSub& sub = rig.mission().player();
  const ChartPoint p = chart.view().world_to_screen(sub.position().x, sub.position().z);
  rig.gui.mouse().update(p.x, p.y, 0, 0);
  rig.frames(3);
  const std::vector<std::string> lines = chart.popup_text();
  REQUIRE(lines.size() == 5);
  CHECK(lines[0] == rig.mission().player_name());
  const World& world = rig.mission().world();
  CHECK(lines[1] == "Loc: " + chart_format_latlon(world.latitude_at(sub.position().z)) + ", " +
                        chart_format_latlon(world.longitude_at(sub.position().x)));
  CHECK(lines[2].starts_with("Heading: "));
  CHECK(lines[4] == "Depth: " + std::to_string(sub.depth_ft()) + " feet");
  rig.save("chart_info_popup");
  rig.gui.mouse().update(p.x + 100, p.y + 100, 0, 0);
  rig.frames(3);
  CHECK(chart.popup_text().empty());
}

TEST_CASE("Latitudes and longitudes as the chart prints them", "[stations]") {
  CHECK(chart_format_latlon({Fixed16::from_int(7), 7, 25, 'N'}) == "7\x1a" "25\x1b" "N");
  CHECK(chart_format_latlon({Fixed16::from_int(151), 151, 5, 'E'}) == "151\x1a" "05\x1b" "E");
}

TEST_CASE("The info box goes around the pointer, off the panel", "[stations]") {
  // Pointer in the top-left quadrant: the first try (angle 0x546, to the
  // lower right) fits.
  const std::array<int, 2> a = chart_place_info_popup(100, 100, 120, 60, std::nullopt);
  CHECK(a[0] >= 0);
  CHECK(a[1] >= 0);
  CHECK(a[0] + 120 - 1 < 0x280);
  CHECK(a[1] + 60 - 1 < 0x1cb);
  CHECK(a[0] + 60 > 100);  // right of the pointer
  // A panel covering that spot pushes the box elsewhere, clear of it.
  const ui::Rect panel{a[0] - 10, a[1] - 10, a[0] + 130, a[1] + 70};
  const std::array<int, 2> b = chart_place_info_popup(100, 100, 120, 60, panel);
  CHECK((b[0] + 119 < panel.x0 || b[1] + 59 < panel.y0 || panel.x1 < b[0] || panel.y1 < b[1]));
  // A box larger than the view fits nowhere: the corner.
  CHECK(chart_place_info_popup(320, 200, 700, 100, std::nullopt) == std::array<int, 2>{0, 0});
}

TEST_CASE("The chart panel drags by its title strip and stays in the view", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  const auto at = chart.panel_position();
  // Press on the title strip, move up and left past the view's corner:
  // the panel stops there.
  rig.gui.mouse().update(at[0] + 100, at[1] + 10, 0, 0);
  rig.frames(1);
  rig.gui.mouse().update(at[0] + 100, at[1] + 10, ui::kButtonLeft, 0);
  rig.frames(1);
  rig.gui.mouse().update(at[0] + 40, at[1] - 400, ui::kButtonLeft, 0);
  rig.frames(1);
  CHECK(chart.panel_position() == std::array<int, 2>{0, 0});
  rig.gui.mouse().update(at[0] + 40, at[1] - 400, 0, 0);
  rig.frames(2);
  // Released: moving the pointer no longer moves the panel.
  rig.gui.mouse().update(320, 300, 0, 0);
  rig.frames(2);
  CHECK(chart.panel_position() == std::array<int, 2>{0, 0});
  CHECK(chart.panel() == ChartPanel::kMap);
}

}  // namespace sh
