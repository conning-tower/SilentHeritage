// Tests of the chart's weapons overlay (realism option 0x40, realistic
// charts off: torpedoes, depth charges, shells, splashes, debris, the
// escorts' sonar wedges), the downed pilot's report zoomed out, and the
// view's map extent at the chart's edges.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "game/saves_options.h"
#include "game/stations/chart.h"
#include "game/stations/chart_reports.h"
#include "game_data.h"
#include "sim/aircraft_strike.h"
#include "sim/depth_charge.h"
#include "sim/effects.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/torpedo.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

ChartStation& open_chart(ControlRig& rig) {
  auto chart = std::make_unique<ChartStation>(rig.screen->env());
  ChartStation& ref = *chart;
  rig.screen->install(StationId::kChart, std::move(chart));
  rig.gui.mouse().update(600, 400, 0, 0);
  rig.frames(1);
  rig.settle();
  rig.key(0x3f00);  // F5
  rig.settle();
  rig.frames(2);
  return ref;
}

// Redraws the chart without advancing the simulation.
void redraw(ControlRig& rig, ChartStation& chart) {
  chart.invalidate();
  rig.frames(1, 0);
}

// Zooms the chart to `zoom` centred on world point (`x`, `z`) and redraws
// it. The centre is taken at 1x: like the original's, the transform
// saturates for points more than ~32 chart pixels off a highly zoomed view.
void show(ControlRig& rig, ChartStation& chart, std::int32_t zoom, std::int32_t x, std::int32_t z) {
  chart.view().set_zoom(1 << 16);
  const auto c = chart.view().world_to_chart(x, z);
  chart.view().set_zoom(zoom << 16);
  chart.view().set_center(c[0], c[1]);
  redraw(rig, chart);
}

void set_weapons_overlay(ControlRig& rig, bool on) {
  std::uint32_t& options = rig.mission().sim().conditions().options;
  options = on ? options | option::kRealisticChartsOff : options & ~option::kRealisticChartsOff;
}

std::uint8_t pixel_at(ControlRig& rig, ChartStation& chart, const fx::Vec3& p) {
  const ChartPoint s = chart.view().world_to_screen(p.x, p.z);
  return rig.screen->screen().at(s.x, s.y);
}

// Returns how many pixels differ between `a` and `b` in the square of
// half-size `r` around (x, y).
int changed(const Image8& a, const Image8& b, int x, int y, int r) {
  int n = 0;
  for (int py = y - r; py <= y + r; ++py) {
    for (int px = x - r; px <= x + r; ++px) {
      n += a.at(px, py) != b.at(px, py) ? 1 : 0;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("The chart view's map is the chart's declared size", "[chartweapons][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const World world = parse_world(install.read("DATA/WORLD-1.WLD"), "WORLD-1.WLD");
  // The rows hold one more column than world_load_awld_chart's declared
  // width; that column is never shown (its last pixel is stray).
  REQUIRE(world.chart.width == 1201);
  REQUIRE(world.chart.at(1200, 799) == 0xe0);
  ChartView view(world, 0, 0, 0x27f, 0x1ca);
  view.set_zoom(4 << 16);
  view.set_center(2000 << 16, 2000 << 16);
  CHECK(view.center_x() == (1200 << 16) - view.half_width());
  CHECK(view.center_y() == (800 << 16) - view.half_height());
  // The map's east edge (the last grid column) is chart x 1200 (the 2.0
  // pixels per cell of world_to_chart_xy), the screen's right edge.
  const std::int32_t east = (world.grid.width - 1) * world.cell_size();
  CHECK(view.world_to_screen(east, 0).x16 == 640 << 16);
  // At the bottom right the view shows the chart's own border: the neat
  // line and the graduated lat/lon frame of rows 788..799.
  Image8 screen(kScreenWidth, kScreenHeight, 0);
  view.draw(screen);
  for (int y = 0x1ca - 47; y <= 0x1ca; ++y) {
    for (const int x : {0, 320, 639}) {
      const auto c = view.screen_to_chart(x, y);
      INFO("screen " << x << "," << y);
      CHECK(screen.at(x, y) == world.chart.at(c[0] >> 16, c[1] >> 16));
    }
  }
  CHECK(screen.at(639, 0x1ca) == world.chart.at(1199, 799));
}

TEST_CASE("Torpedoes keep the crew's sighting for the chart", "[chartweapons]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  TorpedoVehicle t(*tables.find_torpedo("MARK14"), nullptr, fx::Vec3{0, -48, 0}, Angle10(0), Angle10(0),
                   Fixed16::from_int(31), 0, Angle10(0), 10);
  CHECK(!t.sighted());
  t.mark_sighted(GameClock());
  CHECK(t.sighted());
  t.refresh_sighted();  // torpedo_clear_sighted, every frame before the lookouts
  CHECK(!t.sighted());
}

TEST_CASE("Realistic charts off: shells, debris, splashes and depth charges", "[chartweapons]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  Simulation& sim = rig.mission().sim();
  const fx::Vec3 own = rig.mission().player().position();
  show(rig, chart, 256, own.x, own.z);
  // Around the boat, clear of the MAP CONTROLS panel.
  const auto at = [&](int dx, int dy) {
    const ChartPoint s = chart.view().world_to_screen(own.x, own.z);
    const auto w = chart.view().screen_to_world(s.x + dx, s.y + dy);
    return fx::Vec3{w[0], 0, w[1]};
  };
  Shell& shell = sim.add(std::make_unique<Shell>(at(-200, -150) + fx::Vec3{0, 0x4000, 0}, Angle10(0), Angle10(450),
                                                 1000, 5, nullptr));
  Debris& debris = sim.add(std::make_unique<Debris>(at(200, -150)));
  Splash& splash = sim.add(std::make_unique<Splash>(at(-60, -150).x, at(-60, -150).z, false, 100));
  DepthCharge& dc = sim.add(std::make_unique<DepthCharge>(at(200, 60), Angle10(900), Angle10(0), 100));
  set_weapons_overlay(rig, false);
  redraw(rig, chart);
  const Image8 off = rig.screen->screen();
  CHECK(pixel_at(rig, chart, shell.position()) != 0x31);
  set_weapons_overlay(rig, true);
  redraw(rig, chart);
  const Image8& on = rig.screen->screen();
  rig.save("chart2_weapons_256x");
  // shell_chart_draw: a dot in colour 0x31; debris_chart_draw: the dot
  // under CHART.SHP frame 0x32.
  CHECK(pixel_at(rig, chart, shell.position()) == 0x31);
  const ChartPoint d = chart.view().world_to_screen(debris.position().x, debris.position().z);
  CHECK(changed(off, on, d.x, d.y, 12) > 1);
  // splash_chart_draw: frame 0x2b + ticks while ticks < 7.
  REQUIRE(!splash.removed());
  REQUIRE(splash.ticks() < 7);
  const ChartPoint sp = chart.view().world_to_screen(splash.position().x, splash.position().z);
  CHECK(changed(off, on, sp.x, sp.y, 12) > 0);
  // dc_chart_draw: frame 0x2a from 64x.
  const ChartPoint c = chart.view().world_to_screen(dc.position().x, dc.position().z);
  CHECK(changed(off, on, c.x, c.y, 12) > 0);
  // Below 64x the depth charge is not drawn; the rest is.
  show(rig, chart, 32, dc.position().x, dc.position().z);
  const Image8 at32 = rig.screen->screen();
  set_weapons_overlay(rig, false);
  redraw(rig, chart);
  const ChartPoint c32 = chart.view().world_to_screen(dc.position().x, dc.position().z);
  CHECK(changed(at32, rig.screen->screen(), c32.x, c32.y, 2) == 0);
}

TEST_CASE("Realistic charts off: torpedo boxes and tracks", "[chartweapons]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  Simulation& sim = rig.mission().sim();
  const fx::Vec3 own = rig.mission().player().position();
  // A Mark 14 already 100 yards out, heading west (a box from 20 yards).
  const fx::Vec3 start{own.x - 2000 * kUnitsPerYard, -48, own.z};
  TorpedoVehicle& t = sim.add(std::make_unique<TorpedoVehicle>(*rig.tables.find_torpedo("MARK14"), nullptr, start,
                                                               Angle10(2700), Angle10(0), Fixed16::from_int(46), 0,
                                                               Angle10(2700), 10));
  for (int i = 0; i < 4; ++i) {
    sim.simulate(Fixed16::from_int(1));
  }
  REQUIRE(!t.removed());
  set_weapons_overlay(rig, true);
  show(rig, chart, 1024, t.position().x, t.position().z);
  rig.save("chart2_torpedo_1024x");
  // The box's corners 0, 1, 5, 4 joined in colour 0.
  const auto& corners = t.collision().box.corners;
  for (const std::size_t i : {0u, 1u, 4u, 5u}) {
    const ChartPoint p = chart.view().world_to_screen(corners[i].x, corners[i].z);
    INFO("corner " << i);
    CHECK(rig.screen->screen().at(p.x, p.y) == 0);
  }
  // Pass -1: its track, the newest leg in colour 0x80.
  REQUIRE(t.motion().wake_count > 0);
  // Realistic charts: only while the crew sees it.
  set_weapons_overlay(rig, false);
  redraw(rig, chart);
  const ChartPoint p = chart.view().world_to_screen(corners[0].x, corners[0].z);
  CHECK((rig.screen->screen().at(p.x, p.y) == 0) == t.sighted());
}

TEST_CASE("Realistic charts off: the escorts' sonar wedge", "[chartweapons]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  Ship* ship = rig.mission().ships().front();
  REQUIRE(ship != nullptr);
  set_weapons_overlay(rig, true);
  // A ping 90 degrees to starboard.
  ship->ai().sonar.chart_beam = 900;
  show(rig, chart, 16, ship->position().x, ship->position().z);
  rig.save("chart2_sonar_wedge_16x");
  // Drawn once: the beam is cleared.
  CHECK(ship->ai().sonar.chart_beam == -1);
  // 1000 yards along the beam's true bearing the sea is tinted (table
  // 0x117844: 0x80..0x95 -> 0xc), 1000 yards the other way it is not.
  const double b = (ship->heading().tenths() + 900) * std::numbers::pi / 1800.0;
  const auto along = [&](double d) {
    return fx::Vec3{ship->position().x + static_cast<std::int32_t>(std::sin(b) * d * kUnitsPerYard), 0,
                    ship->position().z + static_cast<std::int32_t>(std::cos(b) * d * kUnitsPerYard)};
  };
  const Image8 tinted = rig.screen->screen();
  ship->ai().sonar.chart_beam = -1;
  redraw(rig, chart);
  const std::uint8_t sea = pixel_at(rig, chart, along(1000));
  if (sea >= 0x80 && sea < 0x96) {
    const ChartPoint p = chart.view().world_to_screen(along(1000).x, along(1000).z);
    CHECK(tinted.at(p.x, p.y) == 0xc);
  }
  const ChartPoint q = chart.view().world_to_screen(along(-1000).x, along(-1000).z);
  CHECK(tinted.at(q.x, q.y) == rig.screen->screen().at(q.x, q.y));
  // Realistic charts: no wedge, but the beam is still consumed.
  set_weapons_overlay(rig, false);
  ship->ai().sonar.chart_beam = 900;
  redraw(rig, chart);
  CHECK(ship->ai().sonar.chart_beam == -1);
  const ChartPoint p = chart.view().world_to_screen(along(1000).x, along(1000).z);
  CHECK(rig.screen->screen().at(p.x, p.y) == sea);
}

TEST_CASE("The downed pilot's report zoomed out", "[chartweapons][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  Mission& mission = rig.mission();
  const fx::Vec3 own = mission.player().position();
  const GameClock& clock = mission.sim().clock();
  DownedPilot& pilot = mission.sim().add(
      std::make_unique<DownedPilot>(fx::Vec3{own.x + 40000, 0, own.z + 40000}, Angle10(0), mission.air_theatre()));
  // pilot_init stamps the report with the clock.
  CHECK(pilot.report_hour() == clock.hour());
  CHECK(pilot.report_minute() == clock.minute());
  CHECK(pilot.report_month() == clock.month());
  CHECK(pilot.report_day() == clock.day());
  // Hovering the marker at the radioed position (CHART.SHP frame 0x22, the
  // rect pilot_chart_rect gives zoomed out while the report lasts).
  show(rig, chart, 4, pilot.reported_x(), pilot.reported_z());
  const ChartPoint p = chart.view().world_to_screen(pilot.reported_x(), pilot.reported_z());
  rig.gui.mouse().update(p.x, p.y, 0, 0);
  rig.frames(2, 0);
  const std::vector<std::string> lines = pilot_report_lines(pilot, rig.exe, mission.world());
  CHECK(chart.popup_text() == chart_popup_lines(lines));
  rig.save("chart2_pilot_report_4x");
  const std::string month = std::string("JanFebMarAprMayJunJulAugSepOctNovDec").substr(
      static_cast<std::size_t>(pilot.report_month()) * 3, 3);
  const auto two = [](std::int32_t v) { return (v < 10 ? "0" : "") + std::to_string(v); };
  CHECK(lines[0] == (pilot.sighted() ? "Downed pilot sighted:" : "Downed pilot position report:"));
  CHECK(lines[1] == two(pilot.report_hour()) + two(pilot.report_minute()) + " hrs, " + month + " " +
                        std::to_string(pilot.report_day()));
  CHECK(lines[2] == chart_location_line(rig.exe, mission.world(), pilot.reported_x(), pilot.reported_z(), 0xf215b,
                                        0xf2175));
  CHECK(lines[2].starts_with("Loc: "));
  CHECK(lines[3].empty());
  rig.gui.mouse().update(600, 400, 0, 0);
  rig.frames(2, 0);
  CHECK(chart.popup_text().empty());
}

}  // namespace sh
