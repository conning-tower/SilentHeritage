// Tests of the chart's strategic contact groups (refresh, symbols at their
// chart positions, the reports in the hover popup), its other info popups
// (ports, ships) and the view hold of the zoom box.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "assets/patrol_zones.h"
#include "game/career.h"
#include "game/patrol.h"
#include "game/patrol_contacts.h"
#include "game/stations/chart.h"
#include "game/stations/chart_reports.h"
#include "game_data.h"
#include "sim/ship.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

ChartStation& open_chart(MissionScreen& screen, ControlRig& rig) {
  auto chart = std::make_unique<ChartStation>(screen.env());
  ChartStation& ref = *chart;
  screen.install(StationId::kChart, std::move(chart));
  rig.gui.mouse().update(320, 200, 0, 0);
  rig.frames(1);
  rig.settle();
  rig.key(0x3f00);  // F5
  rig.settle();
  rig.frames(2);
  return ref;
}

ChartStation& open_chart(ControlRig& rig) { return open_chart(*rig.screen, rig); }

// A career in port in June 1943 with a Balao (as career_patrol_test).
Career test_career() {
  Career c;
  CareerRecord& r = c.record;
  r.name = "Tester";
  r.month = 5;
  r.day = 1;
  r.year = 13;
  r.status = CareerStatus::kInPort;
  r.boat = "(SS-337) Carbonero";
  r.class_name = "BALAO";
  r.rank = 1;
  r.equipment = 0x6f;
  r.home_port = "Darwin";
  return c;
}

// A ControlRig whose screen plays a career patrol off Malaysia (PZ.DAT
// zone 9) in place of HIST01.
struct PatrolRig {
  explicit PatrolRig(const Install& install) : rig(install), career(test_career()) {
    WatcomRandom random(99);
    PatrolPlan plan = build_patrol_scenario(career, parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT").at(9),
                                            9, 20000, 100, rig.tables, random);
    patrol = std::make_unique<Patrol>(install, rig.tables, career, std::move(plan));
    MissionSetup setup;
    setup.options = option::kModeCareer | RealismFile::kDefaultBits;
    setup.random = random;
    rig.screen = std::make_unique<MissionScreen>(
        MissionScreenSetup{install, rig.paths, rig.exe, rig.tables, rig.gui, nullptr, rig.options, rig.detail, false},
        patrol->create_mission(std::move(setup)));
  }

  ControlRig rig;
  Career career;
  std::unique_ptr<Patrol> patrol;
};

// Returns how many of `frame`'s pixels drawn at (x, y) show `colour` on
// `screen`, and the frame's pixel count.
std::pair<int, int> frame_pixels(const Image8& screen, const SpriteFrame& frame, int x, int y, std::uint8_t colour) {
  const DecodedFrame d = frame.decode();
  int hits = 0;
  int total = 0;
  for (int py = 0; py < d.opaque.height(); ++py) {
    for (int px = 0; px < d.opaque.width(); ++px) {
      if (d.opaque.at(px, py) != 0) {
        ++total;
        hits += screen.at(x + frame.x0 + px, y + frame.y0 + py) == colour ? 1 : 0;
      }
    }
  }
  return {hits, total};
}

}  // namespace

TEST_CASE("The zoom box holds the chart against edge scrolling", "[chartgroups]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  chart.view().set_zoom(8 << 16);
  chart.center_button();
  rig.frames(2);
  const std::int32_t cx = chart.view().center_x();
  // chart_view_edge_scroll is skipped while 0x118a80 is set (the zoom box).
  rig.key(0x2c5a);  // Z
  REQUIRE(chart.zoom_box() == 16);
  rig.gui.mouse().update(0, 200, 0, 0);
  rig.frames(3);
  CHECK(chart.view().center_x() == cx);
  // The zoom applied, the next panel switch releases the view: the edge
  // scrolls again.
  rig.click(0, 200);
  REQUIRE(chart.zoom_box() == 0);
  const std::int32_t zoomed_cx = chart.view().center_x();
  rig.gui.mouse().update(0, 200, 0, 0);
  rig.frames(3);
  CHECK(chart.view().center_x() < zoomed_cx);
}

TEST_CASE("Group symbols: frames, colours and the 8-hour fade", "[chartgroups]") {
  SH_REQUIRE_INSTALL(install);
  const ExeImage exe = ExeImage::load(install);
  ContactGroup g;
  g.kind = ContactGroupKind::kRoute;
  g.route_kind = RouteGroupKind::kConvoy;
  // No contact: no symbol, no frame recorded.
  CHECK(!group_symbol(g, exe));
  CHECK(g.report.symbol_frame == 0);
  g.report.valid = true;
  g.report.timer = 0x707fffff;
  g.report.size = 2;
  g.report.heading = 900;
  std::optional<GroupSymbol> s = group_symbol(g, exe);
  REQUIRE(s);
  CHECK(s->frame == 0x21);  // size 2 + 0x1f
  CHECK(s->colour == 0x7b);
  CHECK(g.report.symbol_frame == 0x21);
  REQUIRE(s->heading);
  CHECK(s->heading->tenths() == 2700);  // the tick trails behind
  // An unknown size: frame 0x20. After 4 h: fade (0x7080 - 14400) * 5 /
  // 0x7080 = 2.
  g.report.size = -1;
  g.report.heading = -1;
  g.report.timer = 14400 << 16;
  s = group_symbol(g, exe);
  CHECK(s->frame == 0x20);
  CHECK(s->colour == 0x7b + 2);
  CHECK(!s->heading);
  // Task forces: frame by warship type (table 0x1033f4: BB, CV 0x21, CA
  // 0x20, CL 0x1f), warship colour 0xfa; the fade caps at 4.
  g.route_kind = RouteGroupKind::kTaskForce;
  g.warship_type = 3;
  g.report.timer = -5;
  s = group_symbol(g, exe);
  CHECK(s->frame == 0x1f);
  CHECK(s->colour == 0xfa + 4);
  g.route_kind = RouteGroupKind::kSubmarines;
  g.report.timer = 0x707fffff;
  CHECK(group_symbol(g, exe)->frame == 0x1f);
  // Base patrols by their size, anchored ships by theirs (already less
  // one), sampans the small symbol in the merchant colour.
  ContactGroup patrol;
  patrol.kind = ContactGroupKind::kPatrol;
  patrol.report.valid = true;
  patrol.report.timer = 0x707fffff;
  patrol.report.size = 1;
  s = group_symbol(patrol, exe);
  CHECK(s->frame == 0x20);
  CHECK(s->colour == 0xfa);
  patrol.kind = ContactGroupKind::kSampans;
  s = group_symbol(patrol, exe);
  CHECK(s->frame == 0x1f);
  CHECK(s->colour == 0x7b);
}

TEST_CASE("Group reports use SH.EXE's strings", "[chartgroups][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const World world = parse_world(install.read("DATA/WORLD-10.WLD"), "WORLD-10.WLD");
  ContactGroup g;
  g.kind = ContactGroupKind::kRoute;
  g.route_kind = RouteGroupKind::kConvoy;
  HqReport& r = g.report;
  r.valid = true;
  r.size = 1;
  r.heading = 1234;
  r.hour = 9;
  r.minute = 5;
  r.month = 5;
  r.day = 3;
  r.x = 0x400000;
  r.z = 0x400000;
  g.speed = 7 << 16;
  const std::string loc = chart_location_line(exe, world, r.x, r.z, 0xf2606, 0xf2620);
  CHECK(loc.starts_with("Loc: "));
  CHECK(loc.find(", ") != std::string::npos);
  std::vector<std::string> lines = group_report_lines(g, exe, world);
  CHECK(lines == std::vector<std::string>{"Contact Report:", "Medium Convoy", "0905 hrs, Jun 3", loc, "Heading: 123",
                                          "Speed: 07 kts"});
  // Materialized: sighted; an unknown heading hides the speed too.
  g.materialized = true;
  r.heading = -1;
  r.size = -1;
  lines = group_report_lines(g, exe, world);
  CHECK(lines[0] == "Enemy sighted:");
  CHECK(lines[1] == "Convoy");
  CHECK(lines[4] == "Heading: Unknown");
  CHECK(lines[5] == "Speed: Unknown");
  g.route_kind = RouteGroupKind::kTaskForce;
  g.warship_type = 1;
  CHECK(group_report_lines(g, exe, world)[1] == "CV Group");
  g.route_kind = RouteGroupKind::kSubmarines;
  g.size = 2;
  CHECK(group_report_lines(g, exe, world)[1] == "Submarines");
  g.route_kind = RouteGroupKind::kPatrol;
  r.size = 0;
  CHECK(group_report_lines(g, exe, world)[1] == "Small Patrol");

  ContactGroup base;
  base.kind = ContactGroupKind::kPatrol;
  base.report = r;
  base.report.size = 2;
  lines = group_report_lines(base, exe, world);
  CHECK(lines == std::vector<std::string>{"Contact Report:", "Large Patrol", "0905 hrs, Jun 3",
                                          chart_location_line(exe, world, r.x, r.z, 0xf27a1, 0xf27bb), "", ""});
  base.kind = ContactGroupKind::kAnchored;
  CHECK(group_report_lines(base, exe, world)[0] == "Enemy sighted:");
  CHECK(group_report_lines(base, exe, world)[1] == "Anchored ships");
  base.kind = ContactGroupKind::kSampans;
  CHECK(group_report_lines(base, exe, world)[0] == "Ships sighted:");
  CHECK(chart_popup_lines(group_report_lines(base, exe, world)).size() == 4);
}

TEST_CASE("A materialized group's contact follows its nearest member", "[chartgroups]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  std::vector<GroupMember> members;
  for (Ship* ship : rig.mission().ships()) {
    members.push_back({ship, false});
  }
  REQUIRE(members.size() >= 2);
  const fx::Vec3 sub = rig.mission().player().position();
  ContactGroup g;
  g.kind = ContactGroupKind::kPatrol;
  g.patrol_size = 2;
  // Not materialized: untouched.
  refresh_group_contact(g, members, sub.x, sub.z, {10, 20, 4, 7}, false);
  CHECK(!g.report.valid);
  g.materialized = true;
  refresh_group_contact(g, members, sub.x, sub.z, {10, 20, 4, 7}, false);
  REQUIRE(g.report.valid);
  const Vehicle* nearest = nullptr;
  std::uint32_t best = 1000000;
  for (const GroupMember& m : members) {
    const std::uint32_t yd = fx::approx_distance(m.vehicle->position().x - sub.x, m.vehicle->position().z - sub.z) / 0x30;
    if (yd < best) {
      best = yd;
      nearest = m.vehicle;
    }
  }
  CHECK(g.report.x == nearest->position().x);
  CHECK(g.report.z == nearest->position().z);
  CHECK(g.report.heading == -1);
  CHECK(g.report.size == 2);
  CHECK(g.report.hour == 10);
  CHECK(g.report.minute == 20);
  CHECK(g.report.month == 4);
  CHECK(g.report.day == 7);
  CHECK(g.report.timer == 0x707fffff);
  // A route group takes its convoy leader at once, with its heading and
  // speed.
  ContactGroup convoy;
  convoy.materialized = true;
  convoy.size = 1;
  members.back().leader = true;
  refresh_group_contact(convoy, members, sub.x, sub.z, {10, 20, 4, 7}, false);
  CHECK(convoy.report.x == members.back().vehicle->position().x);
  CHECK(convoy.report.heading == members.back().vehicle->heading().tenths());
  CHECK(convoy.heading == convoy.report.heading);
  CHECK(convoy.speed == members.back().vehicle->speed().raw());
  CHECK(convoy.report.size == 1);
}

TEST_CASE("A career patrol's groups on the strategic chart and their reports", "[chartgroups][content]") {
  SH_REQUIRE_GAME_DATA(install);
  PatrolRig p(install);
  ControlRig& rig = p.rig;
  ChartStation& chart = open_chart(*rig.screen, rig);
  ContactGroups* groups = p.patrol->groups();
  REQUIRE(groups != nullptr);
  REQUIRE(!groups->groups().empty());
  // Give the first live group a contact report 60 pixels east of the boat
  // (at 1x).
  ContactGroup* g = nullptr;
  for (const auto& group : groups->groups()) {
    if (!group->finished && !group->materialized) {
      g = group.get();
      break;
    }
  }
  REQUIRE(g != nullptr);
  const fx::Vec3 sub = rig.mission().player().position();
  const auto east = chart.view().screen_to_world(chart.view().world_to_screen(sub.x, sub.z).x + 60,
                                                 chart.view().world_to_screen(sub.x, sub.z).y);
  g->report.valid = true;
  g->report.x = east[0];
  g->report.z = east[1];
  g->report.timer = 0x707fffff;
  g->report.heading = 450;
  g->report.size = 0;
  g->report.hour = 6;
  g->report.minute = 30;
  g->report.month = 5;
  g->report.day = 2;
  rig.gui.mouse().update(600, 400, 0, 0);
  for (const std::int32_t zoom : {1, 4}) {
    INFO("zoom " << zoom);
    chart.view().set_zoom(zoom << 16);
    const auto c = chart.view().world_to_chart(sub.x, sub.z);
    chart.view().set_center(c[0], c[1]);
    chart.invalidate();
    rig.frames(3);
    std::int32_t x = 0;
    std::int32_t y = 0;
    REQUIRE(chart.view().point_on_screen(g->report.x, g->report.z, x, y));
    const ExeImage& exe = rig.exe;
    ContactGroup copy = *g;
    const std::optional<GroupSymbol> s = group_symbol(copy, exe);
    REQUIRE(s);
    const Shp sprites = Shp::load(install, rig.paths.path(GameDir::kArt, "CHART.SHP"));
    const auto [hits, total] = frame_pixels(rig.screen->screen(), sprites.frame(s->frame), x, y, s->colour);
    CHECK(hits == total);
    CHECK(g->report.symbol_frame == static_cast<std::int32_t>(s->frame));
    rig.save(zoom == 1 ? "cgchart_groups_1x" : "cgchart_groups_4x");
    // Hovering the symbol shows the group's report.
    rig.gui.mouse().update(x, y, 0, 0);
    rig.frames(2);
    CHECK(chart.popup_text() == chart_popup_lines(group_report_lines(*g, exe, rig.mission().world())));
    rig.save(zoom == 1 ? "cgchart_report_1x" : "cgchart_report_4x");
    rig.gui.mouse().update(600, 400, 0, 0);
    rig.frames(2);
    CHECK(chart.popup_text().empty());
  }
}

TEST_CASE("Ports show their info zoomed out only", "[chartgroups][content]") {
  SH_REQUIRE_GAME_DATA(install);
  PatrolRig p(install);
  ControlRig& rig = p.rig;
  ChartStation& chart = open_chart(*rig.screen, rig);
  const auto& bases = rig.mission().bases();
  REQUIRE(!bases.empty());
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::size_t i = 0;
  // A port on screen, away from the boat and the panel.
  for (; i < bases.size(); ++i) {
    if (chart.view().point_on_screen(bases[i].station.x, bases[i].station.z, x, y) && x > 20 && x < 620 && y > 20 &&
        y < 280) {
      const ChartPoint s = chart.view().world_to_screen(rig.mission().player().position().x,
                                                        rig.mission().player().position().z);
      if (std::abs(s.x - x) > 8 || std::abs(s.y - y) > 8) {
        break;
      }
    }
  }
  REQUIRE(i < bases.size());
  rig.gui.mouse().update(x + 3, y - 3, 0, 0);  // within 4 x zoom pixels
  rig.frames(2);
  const std::vector<std::string> text = chart.popup_text();
  REQUIRE(!text.empty());
  CHECK(text[0] == bases[i].name);
  CHECK(text[1].starts_with("Loc: "));
  if (text.size() > 3) {
    CHECK((text[2] == "Japanese" || text[2] == "American"));
    CHECK((text[3].ends_with("Naval Base") || text[3].ends_with("Port") || text[3].ends_with(" ")));
  }
  rig.save("cgchart_port");
  rig.gui.mouse().update(x + 5, y - 5, 0, 0);  // outside at 1x
  rig.frames(2);
  CHECK(chart.popup_text().empty());
}

TEST_CASE("Ships show their contact info zoomed in", "[chartgroups][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  Ship* ship = rig.mission().ships().front();
  chart.view().set_zoom(64 << 16);
  const auto c = chart.view().world_to_chart(ship->position().x, ship->position().z);
  chart.view().set_center(c[0], c[1]);
  chart.invalidate();
  rig.frames(2);
  const ChartPoint p = chart.view().world_to_screen(ship->position().x, ship->position().z);
  rig.gui.mouse().update(p.x, p.y, 0, 0);
  rig.frames(2);
  const std::vector<std::string> text = chart.popup_text();
  REQUIRE(text.size() >= 4);
  CHECK((text[0] == "Unidentified ship" || text[0].starts_with("(")));
  const std::size_t r = ship->sighted() ? 1 : 2;
  CHECK(text[r].starts_with("Range: "));
  CHECK(text[r].ends_with(" yards "));
  CHECK(text[r + 1].starts_with("Bearing: "));
  CHECK((text[r + 2].starts_with("Speed: ") || text[r + 2] == "Status: sinking"));
  rig.save("cgchart_ship_info");
}

}  // namespace sh
