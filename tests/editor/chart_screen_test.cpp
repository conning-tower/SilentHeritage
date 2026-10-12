// The editor's chart screen: the screen / world conversions, hit testing at
// each zoom, the click modes, course plotting with its screen checks, the
// range circles, the status bar, and a headless render of a historical
// scenario (saved as a BMP when SH_SCREENS names a directory).

#include "editor/chart_screen.h"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "chart_rig.h"
#include "game/stations/chart.h"
#include "game_data.h"
#include "sim/line_of_sight.h"

namespace sh::editor {
namespace {

using test::ChartRig;
using test::ship_class;

// Zooms the view to `zoom` around view pixel (x, y), as a zoom box click.
void zoom_at(ChartRig& rig, std::int32_t zoom, int x, int y) {
  const std::array<std::int32_t, 2> c = rig.chart->view().screen_to_chart(x, y);
  rig.chart->view().set_zoom(zoom << 16);
  rig.chart->view().set_center(c[0], c[1]);
}

}  // namespace

TEST_CASE("Chart screen and world points convert both ways", "[editor]") {
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install);
  for (const std::int32_t zoom : {1, 4, 16, 64}) {
    INFO(zoom);
    zoom_at(rig, zoom, 320, 230);
    for (const auto& [x, y] : {std::pair{10, 10}, std::pair{320, 230}, std::pair{600, 440}}) {
      const WorldPoint w = rig.chart->screen_to_world(x, y);
      const auto s = rig.chart->world_to_screen(w);
      REQUIRE(s);
      // The world point is the pixel's corner area: back to the same pixel
      // within one.
      CHECK(std::abs((*s)[0] - x) <= 1);
      CHECK(std::abs((*s)[1] - y) <= 1);
    }
  }
}

TEST_CASE("Chart hit testing follows the list order and the zoom", "[editor]") {
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  const auto water = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) > 200; });
  const WorldPoint p = chart.screen_to_world(water[0], water[1]);
  const std::size_t dd = ship_class(rig.data, ShipType::kDestroyer);
  Vessel& first = rig.scenario.add_vessel(dd, p, Vessel::Kind::kShip);
  Vessel& second = rig.scenario.add_vessel(dd, p, Vessel::Kind::kShip);
  const auto at = *chart.world_to_screen(p);

  // Zoomed out: the SBCHART mark's box, two pixels taller each way.
  const SpriteFrame& mark = rig.sbchart.frame(0x16);
  const std::optional<ui::Rect> r = chart.hit_rect(first);
  REQUIRE(r);
  CHECK(*r == ui::Rect{at[0] + mark.x0, at[1] + mark.y0 - 2, at[0] + mark.x1, at[1] + mark.y1 + 2});
  rig.scenario.select(&rig.scenario.player());
  CHECK(chart.pick_object_at(at[0], at[1]) == &first);
  CHECK(chart.cursor() == 4);
  // Mode 0 skips the selection: the next one in the list.
  rig.scenario.select(&first);
  CHECK(chart.pick_object_at(at[0], at[1]) == &second);
  CHECK(chart.pick_object_at(at[0] + 40, at[1] + 40) == nullptr);
  CHECK(chart.cursor() == 1);

  // From 16x: the class icon's larger side scaled by zoom / 300, 5 at least.
  zoom_at(rig, 16, at[0], at[1]);
  const auto at16 = *chart.world_to_screen(p);
  const Shp icons = Shp::load(install, "ART/SHIPICON.SHP");
  const SpriteFrame& icon = icons.frame(rig.data.tables.ships[dd].category);
  const std::int32_t half =
      std::max(fx::trunc(fx::mul(std::max(icon.width(), icon.height()) << 16, fx::div(16 << 16, 300 << 16))), 5);
  CHECK(chart.hit_rect(first) == ui::Rect{at16[0] - half, at16[1] - half, at16[0] + half, at16[1] + half});

  // Mode 3 never picks the player.
  rig.scenario.select(&first);
  const PlayerSub& player = rig.scenario.player();
  const auto ps = chart.world_to_screen({player.rec.x, player.rec.y});
  if (ps && (*ps)[0] >= 0 && (*ps)[0] <= ChartScreen::kViewX1 && (*ps)[1] >= 0 && (*ps)[1] <= ChartScreen::kViewY1) {
    CHECK(chart.pick_object_at((*ps)[0], (*ps)[1]) == &player);
    chart.set_click_mode(ClickMode::kRemove);
    CHECK(chart.pick_object_at((*ps)[0], (*ps)[1]) != &player);
  }
}

TEST_CASE("Chart click modes: placing, removing, patrol orbits", "[editor][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  CHECK(chart.click_mode() == ClickMode::kSelect);
  CHECK(chart.panel() == EditorPanel::kToolbox);

  // REMOVE with the player alone: the message, no mode.
  chart.start_remove();
  CHECK(rig.fake.messages == std::vector<std::string>{"CANNOT DELETE OBJECT"});
  CHECK(chart.click_mode() == ClickMode::kSelect);

  // ADD SUB for a class of type SS; a click on land does nothing, one in
  // deep water places it, selects it and ends the mode.
  chart.start_add_vessel(ship_class(rig.data, ShipType::kSubmarine));
  CHECK(chart.click_mode() == ClickMode::kAddSub);
  CHECK(chart.previous_click_mode() == ClickMode::kSelect);
  CHECK(chart.panel() == EditorPanel::kPick);
  CHECK(chart.pick_title() == "ADD SUB");
  CHECK(chart.prompt_text() == "Select a location");
  rig.frames(2);
  CHECK(chart.shown_panel() == EditorPanel::kPick);
  const auto land = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) <= 0; }, 20);
  chart.click(land[0], land[1]);
  CHECK(rig.scenario.objects().size() == 1);
  const auto water = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) > 300; });
  chart.click(water[0], water[1]);
  REQUIRE(rig.scenario.objects().size() == 2);
  auto* sub = dynamic_cast<Vessel*>(rig.scenario.objects()[1].get());
  REQUIRE(sub != nullptr);
  CHECK(sub->kind == Vessel::Kind::kSubmarine);
  CHECK(rig.scenario.selected() == sub);
  CHECK(chart.click_mode() == ClickMode::kSelect);
  CHECK(chart.panel() == EditorPanel::kToolbox);
  // The next click in water moves that sub (confirmed in DOSBox).
  const WorldPoint placed = sub->position(rig.scenario.object_env());
  const auto further = rig.find_pixel([&](int x, int y) {
    return rig.depth_at(x, y) > 300 && (std::abs(x - water[0]) > 40 || std::abs(y - water[1]) > 40);
  });
  chart.click(further[0], further[1]);
  CHECK(rig.scenario.objects().size() == 2);
  CHECK_FALSE(sub->position(rig.scenario.object_env()) == placed);

  // Esc ends ADD SHIP without placing.
  chart.start_add_vessel(ship_class(rig.data, ShipType::kDestroyer));
  CHECK(chart.click_mode() == ClickMode::kAddShip);
  rig.frames(2);
  rig.key(ui::kKeyEsc);
  CHECK(chart.click_mode() == ClickMode::kSelect);
  CHECK(chart.panel() == EditorPanel::kToolbox);

  // REMOVE: the player is not picked; NO keeps the object, YES removes it.
  chart.start_remove();
  CHECK(chart.click_mode() == ClickMode::kRemove);
  CHECK(chart.panel() == EditorPanel::kRemove);
  CHECK(chart.prompt_text() == "Select an object to remove");
  const auto at = *chart.world_to_screen(sub->position(rig.scenario.object_env()));
  rig.fake.answer = false;
  chart.click(at[0], at[1]);
  CHECK(rig.fake.questions == std::vector<std::string>{"REMOVE SUBMARINE?"});
  CHECK(rig.scenario.objects().size() == 2);
  CHECK(chart.click_mode() == ClickMode::kRemove);
  rig.fake.answer = true;
  chart.click(at[0], at[1]);
  CHECK(rig.scenario.objects().size() == 1);
  CHECK(chart.click_mode() == ClickMode::kSelect);

  // A patrolling aircraft: SELECT POINT (no EXIT, Esc ignored), then
  // SELECT RADIUS, back to the toolbox; an escort then picks its convoy.
  for (const AircraftMission mission : {AircraftMission::kSearchOrbit2, AircraftMission::kSearchOrbit}) {
    Aircraft& a = rig.scenario.add_aircraft(0, mission);
    chart.start_aircraft(a);
    CHECK(chart.click_mode() == ClickMode::kPatrolCenter);
    CHECK(chart.pick_title() == "SELECT POINT");
    CHECK_FALSE(chart.pick_exit_enabled());
    rig.frames(2);
    CHECK(rig.scenario.selected() == &a);
    rig.key(ui::kKeyEsc);
    CHECK(chart.click_mode() == ClickMode::kPatrolCenter);
    chart.click(water[0], water[1]);
    CHECK(a.rec.x == chart.screen_to_world(water[0], water[1]).x);
    CHECK(chart.click_mode() == ClickMode::kPatrolRadius);
    CHECK(chart.pick_title() == "SELECT RADIUS");
    CHECK(chart.prompt() == Prompt::kPatrolRadius);
    chart.click(water[0] + 30, water[1]);
    const WorldPoint edge = chart.screen_to_world(water[0] + 30, water[1]);
    CHECK(a.rec.orbit_radius == static_cast<std::int32_t>(fx::approx_distance(a.rec.x - edge.x, a.rec.y - edge.y)));
    if (mission == AircraftMission::kSearchOrbit) {
      CHECK(chart.click_mode() == ClickMode::kPickConvoy);
      CHECK(chart.pick_title() == "PICK CONVOY");
      CHECK(chart.panel() == EditorPanel::kPick);
      rig.key(ui::kKeyEsc);
    }
    CHECK(chart.click_mode() == ClickMode::kSelect);
    CHECK(chart.panel() == EditorPanel::kToolbox);
    rig.frames(2);
  }
}

TEST_CASE("Chart SELECT A CITY cancel removes the mission", "[editor][content]") {
  // Regression test for the original bugs of SELECT A CITY's cancel (SE
  // cleared the modified flag and could select a removed object).
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  PlayerSub& player = rig.scenario.player();
  SpecialMission& strike = rig.scenario.add_airstrike();
  rig.scenario.select(&strike);
  chart.start_pick_city(false, &player);
  CHECK(chart.click_mode() == ClickMode::kPickCity);
  CHECK(chart.panel() == EditorPanel::kSelectCity);
  CHECK(chart.prompt_text() == "Select an enemy city to bomb");
  rig.frames(2);
  const std::int32_t memory = rig.scenario.header.required_memory;
  rig.scenario.modified = true;
  rig.key(ui::kKeyEsc);
  CHECK(rig.scenario.objects().size() == 1);
  CHECK(rig.scenario.modified);
  CHECK(rig.scenario.header.required_memory == memory - 500);
  CHECK(rig.scenario.selected() == &player);
  CHECK(chart.click_mode() == ClickMode::kSelect);
}

TEST_CASE("Chart courses: water, land, loop closing at 10 pixels, 24 points", "[editor][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  zoom_at(rig, 4, 320, 230);
  rig.frames(2);
  // A destroyer in open water with room around it.
  const auto deep = [&](int x, int y) {
    for (int dy = -60; dy <= 60; dy += 10) {
      for (int dx = -60; dx <= 60; dx += 10) {
        if (rig.depth_at(x + dx, y + dy) <= 100) {
          return false;
        }
      }
    }
    return true;
  };
  const auto c = rig.find_pixel(deep, 80, 80);
  Vessel& v = rig.scenario.add_vessel(ship_class(rig.data, ShipType::kDestroyer), chart.screen_to_world(c[0], c[1]),
                                      Vessel::Kind::kShip);
  v.ship.orders = ShipOrders::kPatrol;
  const auto at = *chart.world_to_screen(v.position(rig.scenario.object_env()));

  chart.start_course(ClickMode::kCourse);
  CHECK(chart.panel() == EditorPanel::kPlotCourse);
  CHECK(chart.prompt_text() == "Select waypoint #1");
  // DONE refuses a course of fewer than 2 points.
  CHECK_FALSE(chart.finish_mode());
  chart.click(at[0] + 40, at[1]);
  CHECK(v.ship.waypoints.count == 2);  // the start, then the point
  CHECK(v.ship.waypoints.x[0] == v.ship.x);
  CHECK(v.ship.waypoints.x[2] == 0);  // open
  CHECK(chart.prompt_text() == "Select waypoint #3");
  chart.click(at[0] + 40, at[1] + 40);
  CHECK(v.ship.waypoints.count == 3);
  // 11 pixels from the start: a new point; then within 10: the loop closes.
  chart.click(at[0] + 11, at[1] + 11);
  CHECK(v.ship.waypoints.count == 4);
  chart.click(at[0] + 10, at[1] - 10);
  CHECK(v.ship.waypoints.count == 4);
  CHECK(v.ship.waypoints.x[4] == -1);
  CHECK(chart.finish_mode());
  CHECK(chart.click_mode() == ClickMode::kSelect);

  // A course can take 24 points; the 24th ends the mode.
  rig.scenario.select(&v);
  chart.start_course(ClickMode::kCourse);
  for (int i = 0; i < 30 && chart.click_mode() == ClickMode::kCourse; ++i) {
    chart.click(at[0] + (i % 2 == 0 ? 30 : -30), at[1] - 50 + 4 * i);
  }
  CHECK(v.ship.waypoints.count == Scenario::kMaxCoursePoints);
  CHECK(chart.click_mode() == ClickMode::kSelect);

  // Land: a point on it, or a leg across it, is refused.
  zoom_at(rig, 1, 320, 230);
  rig.scenario.select(&v);
  chart.start_course(ClickMode::kCourse);
  const auto land = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) <= 0; }, 20);
  chart.click(land[0], land[1]);
  CHECK(v.ship.waypoints.count <= 1);
  const auto from = *chart.world_to_screen(v.position(rig.scenario.object_env()));
  const TerrainGrid& grid = rig.scenario.world().grid();
  const auto across = rig.find_pixel(
      [&](int x, int y) {
        const WorldPoint w = chart.screen_to_world(x, y);
        const WorldPoint s = v.position(rig.scenario.object_env());
        return rig.depth_at(x, y) > 100 && land_on_line(grid, {s.x, s.y}, {w.x, w.y}).land;
      },
      20);
  chart.click(across[0], across[1]);
  CHECK(v.ship.waypoints.count <= 1);
  (void)from;
}

TEST_CASE("Chart PLOT COURSE's DONE and CLEAR follow the course on the open panel", "[editor]") {
  // Regression: the buttons were enabled only when the panel was made,
  // so DONE stayed greyed after the second point.
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  // Open water right of the panel, with room around it.
  const auto c = rig.find_pixel(
      [&](int x, int y) {
        for (int dy = -20; dy <= 60; dy += 10) {
          for (int dx = -20; dx <= 60; dx += 10) {
            if (rig.depth_at(x + dx, y + dy) <= 100) {
              return false;
            }
          }
        }
        return true;
      },
      350, 40);
  Vessel& v = rig.scenario.add_vessel(ship_class(rig.data, ShipType::kDestroyer), chart.screen_to_world(c[0], c[1]),
                                      Vessel::Kind::kShip);
  v.ship.orders = ShipOrders::kPatrol;
  chart.start_course(ClickMode::kCourse);
  rig.frames(2);
  REQUIRE(chart.shown_panel() == EditorPanel::kPlotCourse);
  constexpr int kDoneX = 215 + 32;
  constexpr int kClearX = 141 + 32;
  constexpr int kButtonsY = 45 + 10;
  rig.click_panel(kDoneX, kButtonsY);  // greyed: nothing
  CHECK(chart.click_mode() == ClickMode::kCourse);
  rig.click_screen(c[0] + 40, c[1]);
  CHECK(v.ship.waypoints.count == 2);
  rig.click_panel(kClearX, kButtonsY);
  CHECK(v.ship.waypoints.count == 0);
  rig.click_screen(c[0] + 40, c[1]);
  rig.click_screen(c[0] + 40, c[1] + 40);
  CHECK(v.ship.waypoints.count == 3);
  rig.click_panel(kDoneX, kButtonsY);
  CHECK(chart.click_mode() == ClickMode::kSelect);
  CHECK(chart.panel() == EditorPanel::kToolbox);
  CHECK(v.ship.waypoints.count == 3);
}

TEST_CASE("Chart range circles: the selection's sighting range, the radar", "[editor]") {
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  // The player selected: its own sighting range is 0, no circles.
  CHECK(chart.range_circle_radii().empty());
  const auto water = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) > 200; });
  Vessel& v = rig.scenario.add_vessel(ship_class(rig.data, ShipType::kBattleship),
                                      chart.screen_to_world(water[0], water[1]), Vessel::Kind::kShip);
  REQUIRE(v.visual_range() > 0);
  rig.scenario.header.equipment = 0;
  CHECK(chart.range_circle_radii() == std::vector<std::int32_t>{v.visual_range() * 0x30});
  rig.scenario.header.equipment = static_cast<std::uint32_t>(Equipment::kSj1Radar);
  CHECK(chart.range_circle_radii() == std::vector<std::int32_t>{v.visual_range() * 0x30, 35000 * 0x30});
  // The circle: 60 points at the radius.
  const std::vector<WorldPoint> pts = chart_circle_points({1000, 2000}, 48000);
  REQUIRE(pts.size() == 60);
  CHECK(pts[0] == WorldPoint{1000 + 48000, 2000});
  CHECK(pts[15] == WorldPoint{1000, 2000 + 48000});
  CHECK(pts[30] == WorldPoint{1000 - 48000, 2000});
  CHECK(pts[45] == WorldPoint{1000, 2000 - 48000});
}

TEST_CASE("Chart selection moves, status bar and hover popup", "[editor][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  const auto water = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) > 300; });
  // Mode 0 on empty water moves the selection (the player) there.
  chart.click(water[0], water[1]);
  const WorldPoint w = chart.screen_to_world(water[0], water[1]);
  CHECK(rig.scenario.player().rec.x == w.x);
  CHECK(rig.scenario.player().rec.y == w.y);
  // Not onto land.
  const auto land = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) <= 0; }, 20);
  chart.click(land[0], land[1]);
  CHECK(rig.scenario.player().rec.x == w.x);

  const auto other = rig.find_pixel([&](int x, int y) { return rig.depth_at(x, y) > 100 && x > water[0] + 60; });
  chart.mouse_moved(other[0], other[1]);
  const WorldPoint o = chart.screen_to_world(other[0], other[1]);
  const World& world = rig.scenario.world().world();
  CHECK(chart.status_latitude() == format_latlon(rig.data.se, world.latitude_at(o.y)));
  CHECK(chart.status_longitude() == format_latlon(rig.data.se, world.longitude_at(o.x)));
  CHECK(chart.status_depth() == std::to_string(rig.depth_at(other[0], other[1])) + " ft");
  CHECK(chart.status_hint() == "Press ESC/Right Mouse Button for Menu Bar");

  // Pointing at the player: its info box, clear of the toolbox.
  rig.gui.mouse().update(water[0], water[1], 0, 0);
  rig.frames(3);
  const std::vector<std::string> lines = chart.popup_text();
  REQUIRE_FALSE(lines.empty());
  CHECK(lines[0] == rig.scenario.player().describe(rig.scenario.object_env())[0]);
  chart.mouse_moved(water[0], water[1]);
  // The player is the selection, so mode 0 does not hover it.
  CHECK(chart.status_hint() == "Press ESC/Right Mouse Button for Menu Bar");
}

TEST_CASE("Chart screen of a historical scenario", "[editor]") {
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install, "SCEN/HIST01.SCN");
  rig.frames(3);
  // The toolbox is up, the view centred on the player, objects drawn.
  CHECK(rig.chart->shown_panel() == EditorPanel::kToolbox);
  const PlayerSub& p = rig.scenario.player();
  const auto at = rig.chart->world_to_screen({p.rec.x, p.rec.y});
  REQUIRE(at);
  CHECK(rig.screen.at(0, 0) != 0);
  rig.save("editor_chart_hist01");
  // Zoom box to 16x, clicked on the player: the class icons.
  for (int i = 0; i < 4; ++i) {
    rig.chart->zoom_in();
  }
  CHECK(rig.chart->zoom_box() == 16);
  rig.gui.mouse().update((*at)[0], (*at)[1], 0, 0);
  rig.frames(2);
  rig.gui.mouse().update((*at)[0], (*at)[1], ui::kButtonLeft, 0);
  rig.frames(1);
  rig.gui.mouse().update((*at)[0], (*at)[1], 0, 0);
  rig.frames(3);
  CHECK(rig.chart->view().zoom_int() == 16);
  CHECK(rig.chart->zoom_box() == 0);
  rig.gui.mouse().update(600, 50, 0, 0);
  rig.frames(3);
  rig.save("editor_chart_hist01_16x");
  // The next object (a ship) centred, its icon and selection brackets.
  rig.chart->next_object();
  rig.chart->center_on_selection();
  rig.frames(3);
  CHECK(rig.scenario.selected() == rig.scenario.objects()[1].get());
  rig.save("editor_chart_hist01_ship");
}

TEST_CASE("Extended: the chart palette alone has M8.PCX's colours", "[editor][extended][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  const World& world = rig.scenario.world().world();
  const VgaPalette by_game = se_chart_palette(install, world, true);
  const VgaPalette alone = se_chart_palette(install, world, false);
  const VgaPalette completed = se_chart_palette(install, world, false, true);
  CHECK(completed == by_game);
  CHECK(se_chart_palette(install, world, true, true) == by_game);
  // SE alone leaves exactly 86-127 and 255 black.
  for (std::size_t i = 0; i < 256; ++i) {
    const bool same = std::equal(alone.begin() + 3 * i, alone.begin() + 3 * i + 3, by_game.begin() + 3 * i);
    if ((i >= 86 && i <= 127) || i == 255) {
      CHECK(alone[3 * i] + alone[3 * i + 1] + alone[3 * i + 2] == 0);
    } else {
      CHECK(same);
    }
  }
}

}  // namespace sh::editor
