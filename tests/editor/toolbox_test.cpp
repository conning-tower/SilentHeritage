// The TOOLBOX's object panels, driven by clicks on their controls: each
// field edits its record with the original ranges and steps, the heading
// dial, the orders and mission menus per object kind and their click modes,
// convoys, the add tools, SELECT A CITY, and headless renders of the player
// and ship panels (saved as BMPs when SH_SCREENS names a directory).

#include "editor/toolbox.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "chart_rig.h"
#include "game_data.h"
#include "ui/dial_control.h"

namespace sh::editor {
namespace {

using test::ChartRig;
using test::ship_class;

// The middle of a control box of the panels (dialog coordinates).
constexpr int kCycleDy = 10;
constexpr int kDigitDx = 8;

// The first SHIPS.DAT surface class of `role`.
std::size_t role_class(const EditorData& data, ShipRole role) {
  for (std::size_t i = 0; i < data.tables.ships.size(); ++i) {
    const ShipClass& c = data.tables.ships[i];
    if (c.role == role && c.ship_type != ShipType::kSubmarine) {
      return i;
    }
  }
  FAIL("no ship class of that role");
  return 0;
}

// A deep-water view pixel with room around it.
std::array<int, 2> open_water(const ChartRig& rig, std::int32_t depth = 300) {
  return rig.find_pixel([&](int x, int y) {
    for (int dy = -30; dy <= 30; dy += 10) {
      for (int dx = -30; dx <= 30; dx += 10) {
        if (rig.depth_at(x + dx, y + dy) <= depth) {
          return false;
        }
      }
    }
    return true;
  });
}

// A new vessel of `cls` in open water, selected with its toolbox panel up.
Vessel& add_selected(ChartRig& rig, std::size_t cls, std::array<int, 2> at, Vessel::Kind kind = Vessel::Kind::kShip) {
  Vessel& v = rig.scenario.add_vessel(cls, rig.chart->screen_to_world(at[0], at[1]), kind);
  rig.chart->select(v);
  rig.frames(2);
  return v;
}

std::vector<std::string> labels(const ChartRig& rig) { return rig.fake.menus.back().labels; }
std::vector<bool> greyed(const ChartRig& rig) { return rig.fake.menus.back().disabled; }

}  // namespace

TEST_CASE("Heading dial: the bearing of the pointer from the centre", "[editor]") {
  // heading_from_delta(dy - 1, dx - 1) + 90 degrees.
  CHECK(ui::dial_bearing(30, 1) == 90);
  CHECK(ui::dial_bearing(1, 30) == 180);
  CHECK(ui::dial_bearing(-28, 1) == 270);
  CHECK(ui::dial_bearing(1, -28) == 0);
  CHECK(ui::dial_bearing(31, 31) == 135);
  CHECK(ui::dial_bearing(-29, -29) == 315);
}

TEST_CASE("Toolbox digit and time cycles", "[editor]") {
  const auto heading = [](std::int32_t v) { return v <= 359; };
  CHECK(cycle_digit(300, 100, heading) == 0);  // 4xx..9xx are past 359
  CHECK(cycle_digit(259, 100, heading) == 359);
  CHECK(cycle_digit(355, 10, heading) == 305);
  CHECK(cycle_digit(359, 1, heading) == 350);
  // No digit fits: the value stays (the original hangs).
  CHECK(cycle_digit(50, 1, [](std::int32_t v) { return v >= 100; }) == 50);
  // Hours tens and units 0..9, minutes tens 0..5.
  CHECK(cycle_time_digit(0, 0) == 600);
  CHECK(cycle_time_digit(99 * 60 + 59, 0) == 9 * 60 + 59);
  CHECK(cycle_time_digit(9 * 60, 1) == 0);
  CHECK(cycle_time_digit(59, 2) == 9);
  CHECK(cycle_time_digit(59, 3) == 50);
  CHECK(time_digit(13 * 60 + 47, 0) == 1);
  CHECK(time_digit(13 * 60 + 47, 1) == 3);
  CHECK(time_digit(13 * 60 + 47, 2) == 4);
  CHECK(time_digit(13 * 60 + 47, 3) == 7);
}

TEST_CASE("Toolbox player panel: heading, engine, crew, depth digits, presets", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  PlayerSub& p = rig.scenario.player();
  const SubClass& cls = rig.data.tables.subs[*rig.data.find_sub(scn_text(p.rec.sub_class))];
  // In water deeper than 1.5 x the test depth.
  const auto test = static_cast<std::int32_t>(cls.test_depth_ft);
  const auto deep = open_water(rig, test * 2);
  REQUIRE(rig.scenario.move_object(p, rig.chart->screen_to_world(deep[0], deep[1])));
  p.rec.heading = 300;
  p.rec.depth = 100;
  rig.chart->select(p);
  rig.frames(2);
  CHECK(rig.chart->toolbox().heading_text() == "300");
  rig.scenario.modified = false;

  // The heading digits: hundreds, tens, units, then the dial.
  rig.click_panel(32 + kDigitDx, 251 + kCycleDy);
  CHECK(p.rec.heading == 0);
  rig.click_panel(51 + kDigitDx, 251 + kCycleDy);
  CHECK(p.rec.heading == 10);
  rig.click_panel(70 + kDigitDx, 251 + kCycleDy);
  CHECK(p.rec.heading == 11);
  CHECK(rig.chart->toolbox().heading_text() == "11");
  CHECK(rig.scenario.modified);
  rig.click_panel(60 + 30, 197 + 1);
  CHECK(p.rec.heading == 90);
  rig.click_panel(60 + 1, 197 + 30);
  CHECK(p.rec.heading == 180);

  // Engine orders Stop .. Back 1/3 (0..9), crews 0..2.
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(p.rec.engine_order == 1);
  p.rec.engine_order = 9;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(p.rec.engine_order == 0);
  p.rec.crew = CrewQuality::kElite;
  rig.click_panel(108 + 50, 196 + kCycleDy);
  CHECK(p.rec.crew == CrewQuality::kGreen);

  // Depth digits: from the draft to 1.5 x the test depth.
  const std::int32_t max = test + test / 2;
  rig.click_panel(165 + kDigitDx, 223 + kCycleDy);
  CHECK(p.rec.depth == 200);
  for (std::int32_t h = 300; h <= max; h += 100) {
    rig.click_panel(165 + kDigitDx, 223 + kCycleDy);
    CHECK(p.rec.depth == h);
  }
  rig.click_panel(165 + kDigitDx, 223 + kCycleDy);
  CHECK(p.rec.depth == 100);  // 0xx is above the draft: wraps to 1xx
  rig.click_panel(201 + kDigitDx, 223 + kCycleDy);
  CHECK(p.rec.depth == 101);
  rig.click_panel(183 + kDigitDx, 223 + kCycleDy);
  CHECK(p.rec.depth == 111);

  // PERI / RAD / SURF.
  rig.click_panel(115 + 20, 249 + 10);
  CHECK(p.rec.depth == static_cast<std::int32_t>(cls.periscope_depth_ft));
  rig.click_panel(172 + 20, 249 + 10);
  CHECK(p.rec.depth == static_cast<std::int32_t>(cls.radar_depth_ft));
  rig.click_panel(229 + 20, 249 + 10);
  CHECK(p.rec.depth == static_cast<std::int32_t>(cls.draft_ft));
}

TEST_CASE("Toolbox player class and name through the selection dialogs", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  PlayerSub& p = rig.scenario.player();
  const std::size_t current = *rig.data.find_sub(scn_text(p.rec.sub_class));
  const std::size_t other = current == 0 ? 1 : 0;
  const SubClass& to = rig.data.tables.subs[other];
  p.rec.depth = 5000;  // past any test depth: clamped to 1.5 x

  // Cancelled: nothing changes.
  const ScnPlayerSub before = p.rec;
  rig.click_panel(108 + 50, 150 + kCycleDy);
  CHECK(rig.fake.opened_with == std::vector<std::int32_t>{static_cast<std::int32_t>(current)});
  CHECK(p.rec == before);

  rig.fake.sub_choice = static_cast<std::int32_t>(other);
  rig.fake.sub_name = "(SS-999) TESTFISH";
  rig.click_panel(108 + 50, 150 + kCycleDy);
  CHECK(scn_text(p.rec.sub_class) == to.name);
  CHECK(scn_text(p.rec.name) == "(SS-999) TESTFISH");
  const auto test = static_cast<std::int32_t>(to.test_depth_ft);
  CHECK(p.rec.depth == test + test / 2);
  CHECK(rig.scenario.modified);

  // The name button: CANCEL keeps the name, a choice replaces it.
  rig.fake.opened_with.clear();
  rig.fake.sub_name.clear();
  rig.click_panel(11 + 100, 281 + kCycleDy);
  CHECK(rig.fake.opened_with == std::vector<std::int32_t>{static_cast<std::int32_t>(other)});
  CHECK(scn_text(p.rec.name) == "(SS-999) TESTFISH");
  rig.fake.sub_name = "(SS-998) OTHER";
  rig.click_panel(11 + 100, 281 + kCycleDy);
  CHECK(scn_text(p.rec.name) == "(SS-998) OTHER");
}

TEST_CASE("Toolbox ship panel: speed to the top speed, damage, crew of both parts", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  Vessel& v = add_selected(rig, ship_class(rig.data, ShipType::kDestroyer), open_water(rig));
  const std::int32_t top = rig.scenario.ship_max_speed(v);
  REQUIRE(top >= 10);

  rig.click_panel(143 + kDigitDx, 239 + kCycleDy);
  CHECK(v.ship.speed == 1);
  CHECK(v.sub.speed == 1);
  rig.click_panel(125 + kDigitDx, 239 + kCycleDy);
  CHECK(v.ship.speed == 11);
  // At the top speed the units wrap to 0.
  v.set_speed(top);
  rig.click_panel(143 + kDigitDx, 239 + kCycleDy);
  CHECK(v.ship.speed == (top % 10 == 9 ? top - 9 : top / 10 * 10));
  // "Max:" clamps a faster speed.
  v.set_speed(top + 5);
  rig.chart->redraw_toolbox();
  rig.frames(2);
  CHECK(v.ship.speed == top);

  // Damage 0..99 on both parts.
  v.set_damage(99);
  rig.click_panel(239 + kDigitDx, 239 + kCycleDy);
  CHECK(v.ship.damage_pct == 90);
  CHECK(v.sub.damage_pct == 90);
  rig.click_panel(221 + kDigitDx, 239 + kCycleDy);
  CHECK(v.ship.damage_pct == 0);

  rig.click_panel(108 + 50, 196 + kCycleDy);
  CHECK(v.ship.crew == CrewQuality::kVeteran);
  CHECK(v.sub.crew == CrewQuality::kVeteran);

  // The dial sets both headings.
  rig.click_panel(60 + 30, 197 + 1);
  CHECK(v.ship.heading == 90);
  CHECK(v.sub.heading == 90);
}

TEST_CASE("Toolbox enemy sub panel: depth above the bottom, orders None / Patrol only", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  Vessel& s = add_selected(rig, ship_class(rig.data, ShipType::kSubmarine), open_water(rig), Vessel::Kind::kSubmarine);
  // Depth digits: 64 ft at most.
  s.sub.depth = 60;
  rig.click_panel(209 + kDigitDx, 239 + kCycleDy);
  CHECK(s.sub.depth == 0);
  rig.click_panel(227 + kDigitDx, 239 + kCycleDy);
  CHECK(s.sub.depth == 1);
  s.sub.depth = 64;
  rig.click_panel(227 + kDigitDx, 239 + kCycleDy);
  CHECK(s.sub.depth == 60);
  // Damage digits sit lower on this panel.
  rig.click_panel(93 + kDigitDx, 281 + kCycleDy);
  CHECK(s.sub.damage_pct == 1);
  CHECK(s.ship.damage_pct == 1);

  // Orders: None and "Patrol", which is Follow Path with an open course
  // (never Intercept).
  rig.fake.menu_choice = 1;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(labels(rig) == std::vector<std::string>{"None", "Patrol"});
  CHECK(s.sub.orders == ShipOrders::kFollowPath);
  CHECK(rig.chart->click_mode() == ClickMode::kSubCourse);
  CHECK(rig.chart->panel() == EditorPanel::kPlotCourse);
}

TEST_CASE("Toolbox ship orders by role, convoys and their leader", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  const auto at = open_water(rig);
  const std::size_t merchant = role_class(rig.data, ShipRole::kMerchant);
  const std::size_t escort_class = role_class(rig.data, ShipRole::kEscort);

  // Without a convoy an escort cannot take Escort orders.
  Vessel& lone = rig.scenario.add_vessel(escort_class, chart.screen_to_world(at[0], at[1]), Vessel::Kind::kShip);
  CHECK(vessel_orders_menu(rig.scenario, lone).disabled == std::vector<bool>{false, true, false});
  rig.scenario.remove(lone);

  // A merchant with no convoy: Convoy Member greyed; Leader takes id 1 and
  // plots its course.
  Vessel& leader = add_selected(rig, merchant, at);
  rig.fake.menu_choice = 1;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(labels(rig) == std::vector<std::string>{"None", "Convoy Leader", "Convoy Member"});
  CHECK(greyed(rig) == std::vector<bool>{false, false, true});
  CHECK(leader.ship.orders == ShipOrders::kConvoyLeader);
  CHECK(leader.ship.convoy_group == 1);
  CHECK(leader.ship.zigzag == 0);
  CHECK(chart.click_mode() == ClickMode::kCourse);
  rig.frames(2);
  CHECK(chart.shown_panel() == EditorPanel::kPlotCourse);
  chart.click(at[0] + 20, at[1]);
  chart.click(at[0] + 20, at[1] + 20);
  REQUIRE(chart.finish_mode());
  rig.frames(2);

  // A second merchant joins it as a member: PICK CONVOY.
  Vessel& member = add_selected(rig, merchant, {at[0] + 25, at[1] + 25});
  rig.fake.menu_choice = 2;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(greyed(rig) == std::vector<bool>{false, false, false});
  CHECK(member.ship.orders == ShipOrders::kConvoyMember);
  CHECK(chart.click_mode() == ClickMode::kPickConvoy);
  CHECK(chart.pick_title() == "PICK CONVOY");
  rig.frames(2);
  const auto lp = *chart.world_to_screen(leader.position(rig.scenario.object_env()));
  chart.click(lp[0], lp[1]);
  CHECK(member.ship.convoy_group == 1);
  CHECK(member.ship.heading == leader.ship.heading);
  rig.frames(2);
  // A member's heading and speed are the leader's: locked.
  const std::int32_t heading = member.ship.heading;
  rig.click_panel(70 + kDigitDx, 251 + kCycleDy);
  rig.click_panel(143 + kDigitDx, 239 + kCycleDy);
  CHECK(member.ship.heading == heading);
  CHECK(member.ship.speed == leader.ship.speed);

  // The leader's new heading and speed reach the member.
  rig.chart->select(leader);
  rig.frames(2);
  rig.click_panel(60 + 1, 197 + 30);
  rig.click_panel(143 + kDigitDx, 239 + kCycleDy);
  rig.frames(1);
  CHECK(member.ship.heading == 180);
  CHECK(member.ship.speed == leader.ship.speed);

  // An escort: Escort / Patrol; Escort plots a looped course then picks
  // its convoy.
  Vessel& escort = add_selected(rig, escort_class, {at[0] - 25, at[1] + 25});
  rig.fake.menu_choice = 1;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(labels(rig) == std::vector<std::string>{"None", "Escort", "Patrol"});
  CHECK(greyed(rig) == std::vector<bool>{false, false, false});
  CHECK(escort.ship.orders == ShipOrders::kEscort);
  CHECK(chart.click_mode() == ClickMode::kEscortCourse);
  rig.frames(2);
  chart.click(at[0] - 45, at[1] + 25);
  chart.click(at[0] - 45, at[1] + 45);
  REQUIRE(chart.finish_mode());
  CHECK(chart.click_mode() == ClickMode::kPickConvoy);
  rig.frames(2);
  chart.click(lp[0], lp[1]);
  CHECK(escort.ship.convoy_group == 1);
  rig.frames(2);
  // The leader's new speed sets its escorts' (twice it, at most their top
  // speed).
  rig.chart->select(leader);
  rig.frames(2);
  rig.click_panel(143 + kDigitDx, 239 + kCycleDy);
  rig.frames(1);
  CHECK(escort.ship.speed == std::min(2 * leader.ship.speed, rig.scenario.ship_max_speed(escort)));

  // Removing the leader's role asks first; NO keeps everything.
  rig.chart->select(leader);
  rig.frames(2);
  rig.fake.answer = false;
  rig.fake.menu_choice = 0;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(rig.fake.questions.back() == "REMOVE CONVOY LEADER?");
  CHECK(leader.ship.orders == ShipOrders::kConvoyLeader);
  rig.fake.answer = true;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(leader.ship.orders == ShipOrders::kNone);
  CHECK(member.ship.orders == ShipOrders::kNone);
  CHECK(escort.ship.orders == ShipOrders::kPatrol);
  CHECK(chart.panel() == EditorPanel::kToolbox);

  // Small craft: None / Fishing, no course.
  const std::size_t small = role_class(rig.data, ShipRole::kSmallCraft);
  Vessel& boat = add_selected(rig, small, {at[0], at[1] - 25});
  rig.fake.menu_choice = 1;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(labels(rig) == std::vector<std::string>{"None", "Fishing"});
  CHECK(boat.ship.orders == ShipOrders::kFishing);
  CHECK(chart.click_mode() == ClickMode::kSelect);
  CHECK(chart.panel() == EditorPanel::kToolbox);
}

TEST_CASE("Toolbox convoy cycle: the convoys with a leader", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  const auto at = open_water(rig);
  const std::size_t merchant = role_class(rig.data, ShipRole::kMerchant);
  for (const std::int32_t id : {1, 3}) {
    Vessel& l = rig.scenario.add_vessel(merchant, rig.chart->screen_to_world(at[0] + id, at[1]), Vessel::Kind::kShip);
    l.ship.orders = ShipOrders::kConvoyLeader;
    l.ship.convoy_group = id;
  }
  Vessel& escort = rig.scenario.add_vessel(role_class(rig.data, ShipRole::kEscort),
                                           rig.chart->screen_to_world(at[0], at[1] + 10), Vessel::Kind::kShip);
  escort.ship.orders = ShipOrders::kEscort;
  escort.ship.convoy_group = 1;
  rig.chart->select(escort);
  rig.frames(2);
  rig.click_panel(75 + 30, 280 + kCycleDy);
  CHECK(escort.ship.convoy_group == 3);
  rig.click_panel(75 + 30, 280 + kCycleDy);
  CHECK(escort.ship.convoy_group == 1);
  // The Alerted option (Off / On).
  rig.click_panel(210 + 30, 280 + kCycleDy);
  CHECK(escort.ship.alert == 1);
  rig.click_panel(210 + 30, 280 + kCycleDy);
  CHECK(escort.ship.alert == 0);
}

TEST_CASE("Toolbox aircraft panel: mission, bombs, guns, regenerate", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  std::optional<std::size_t> armed;
  std::optional<std::size_t> gunless;
  for (std::size_t i = 0; i < rig.data.tables.aircraft.size(); ++i) {
    const AircraftType& t = rig.data.tables.aircraft[i];
    if (!armed && t.guns != 0 && t.bombs > 0 && t.bombs < 10) {
      armed = i;
    }
    if (!gunless && t.guns == 0) {
      gunless = i;
    }
  }
  REQUIRE(armed);
  Aircraft& a = rig.scenario.add_aircraft(*armed, AircraftMission::kSearchOrbit2);
  const auto at = open_water(rig);
  const WorldPoint w = chart.screen_to_world(at[0], at[1]);
  a.rec.x = w.x;
  a.rec.y = w.y;
  a.rec.orbit_radius = 4800;
  chart.select(a);
  rig.frames(2);

  // Bombs 0 .. the type's load.
  const auto bombs = static_cast<std::int32_t>(rig.data.tables.aircraft[*armed].bombs);
  CHECK(a.rec.bomb_load == bombs);
  rig.click_panel(174 + kDigitDx, 225 + kCycleDy);
  CHECK(a.rec.bomb_load == 0);
  rig.click_panel(174 + kDigitDx, 225 + kCycleDy);
  CHECK(a.rec.bomb_load == 1);
  // Guns and Regenerate toggle.
  rig.click_panel(186 + 20, 252 + kCycleDy);
  CHECK(a.rec.strafes == 0);
  rig.click_panel(235 + 20, 281 + kCycleDy);
  CHECK(a.rec.relaunch == 0);
  // The convoy cycle is greyed but for an escort mission.
  rig.click_panel(75 + 30, 281 + kCycleDy);
  CHECK(a.rec.escort_group == 0);

  // Missions: Escort greyed without a convoy; Follow Path plots a path from
  // the aircraft, Patrol asks for a new radius.
  rig.fake.menu_choice = 2;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(labels(rig) == std::vector<std::string>{"Patrol", "Escort", "Follow Path"});
  CHECK(greyed(rig) == std::vector<bool>{false, true, false});
  CHECK(rig.fake.menus.back().initial == 0);
  CHECK(a.rec.mission == AircraftMission::kFollowPath);
  CHECK(a.rec.orbit_radius == 0);
  CHECK(a.rec.waypoints.count == 1);
  CHECK(a.rec.waypoints.x[0] == w.x);
  CHECK(chart.click_mode() == ClickMode::kAircraftPath);
  CHECK(chart.prompt_text() == "Select waypoint #2");
  rig.key(ui::kKeyEsc);  // a path of one point cannot end
  chart.click(at[0] + 20, at[1]);
  REQUIRE(chart.finish_mode());
  rig.frames(2);
  rig.fake.menu_choice = 0;
  rig.click_panel(108 + 50, 173 + kCycleDy);
  CHECK(rig.fake.menus.back().initial == 2);
  CHECK(a.rec.mission == AircraftMission::kSearchOrbit2);
  CHECK(chart.click_mode() == ClickMode::kPatrolRadius);
  CHECK(chart.pick_title() == "SELECT RADIUS");
  CHECK_FALSE(chart.pick_exit_enabled());
  chart.click(at[0] + 30, at[1]);
  CHECK(a.rec.orbit_radius > 0);
  rig.frames(2);

  // A type without guns greys Guns.
  if (gunless) {
    rig.fake.aircraft_choice = static_cast<std::int32_t>(*gunless);
    rig.click_panel(108 + 50, 150 + kCycleDy);
    CHECK(scn_text(a.rec.aircraft_type) == rig.data.tables.aircraft[*gunless].name);
    CHECK(a.rec.strafes == 0);
    rig.frames(2);
    rig.click_panel(186 + 20, 252 + kCycleDy);
    CHECK(a.rec.strafes == 0);
  }
}

TEST_CASE("Toolbox add tools: ship or sub, aircraft, airstrike with SELECT A CITY", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install);
  ChartScreen& chart = *rig.chart;
  // ADD SHIP: cancelled, a surface class, a submarine class.
  rig.click_panel(82 + 20, 71 + 10);
  CHECK(rig.fake.opened_with == std::vector<std::int32_t>{0});
  CHECK(chart.click_mode() == ClickMode::kSelect);
  rig.fake.ship_choice = static_cast<std::int32_t>(ship_class(rig.data, ShipType::kSubmarine));
  rig.click_panel(82 + 20, 71 + 10);
  CHECK(chart.click_mode() == ClickMode::kAddSub);
  rig.key(ui::kKeyEsc);
  rig.fake.ship_choice = static_cast<std::int32_t>(ship_class(rig.data, ShipType::kDestroyer));
  rig.click_panel(82 + 20, 71 + 10);
  CHECK(chart.click_mode() == ClickMode::kAddShip);
  rig.key(ui::kKeyEsc);

  // ADD AIRCRAFT: the type, then the mission (cancelled: no aircraft).
  rig.fake.aircraft_choice = 0;
  rig.click_panel(127 + 20, 71 + 10);
  CHECK(rig.scenario.objects().size() == 1);
  rig.fake.menu_choice = 0;
  rig.click_panel(127 + 20, 71 + 10);
  REQUIRE(rig.scenario.objects().size() == 2);
  auto* a = dynamic_cast<Aircraft*>(rig.scenario.objects()[1].get());
  REQUIRE(a != nullptr);
  CHECK(a->rec.mission == AircraftMission::kSearchOrbit2);
  CHECK(chart.click_mode() == ClickMode::kPatrolCenter);
  rig.frames(2);
  CHECK(rig.scenario.selected() == a);
  const auto at = open_water(rig);
  chart.click(at[0], at[1]);
  chart.click(at[0] + 30, at[1]);
  rig.frames(2);
  CHECK(chart.panel() == EditorPanel::kToolbox);

  // AIRSTRIKE: SELECT A CITY lists the enemy cities alphabetically.
  if (rig.scenario.world().airstrike_targets() == 0) {
    rig.click_panel(172 + 20, 71 + 10);
    CHECK(rig.fake.messages.back() == "CANNOT SETUP AIRSTRIKE!");
    return;
  }
  rig.click_panel(172 + 20, 71 + 10);
  REQUIRE(rig.scenario.objects().size() == 3);
  auto* strike = dynamic_cast<SpecialMission*>(rig.scenario.objects()[2].get());
  REQUIRE(strike != nullptr);
  CHECK(chart.click_mode() == ClickMode::kPickCity);
  rig.frames(2);
  CHECK(chart.shown_panel() == EditorPanel::kSelectCity);
  CHECK(rig.scenario.selected() == strike);
  const std::vector<CityChoice>& cities = chart.cities();
  REQUIRE_FALSE(cities.empty());
  for (std::size_t i = 1; i < cities.size(); ++i) {
    CHECK(cities[i - 1].name <= cities[i].name);
  }
  // The second row of the list, then SELECT.
  if (cities.size() > 1) {
    rig.click_panel(10 + 40, 135 + 16 + 8);
    CHECK(chart.city_selection() == 1);
  }
  rig.click_panel(110 + 30, 45 + 10);
  CHECK(strike->base() == static_cast<std::int32_t>(cities[static_cast<std::size_t>(chart.city_selection())].base));
  rig.frames(2);
  CHECK(chart.shown_panel() == EditorPanel::kToolbox);

  // Its panel: strike size, the delay and the hold ("Flight Delay").
  rig.click_panel(36 + 50, 174 + kCycleDy);
  CHECK(std::get<BomberGroupRecord>(strike->rec).size == BomberGroupSize::k30Planes);
  rig.click_panel(125 + kDigitDx, 223 + kCycleDy);
  CHECK(std::get<BomberGroupRecord>(strike->rec).launch_delay_min == 60);
  rig.click_panel(151 + kDigitDx, 276 + kCycleDy);
  CHECK(std::get<BomberGroupRecord>(strike->rec).hold_min == 10);
  // Its location button opens SELECT A CITY again.
  rig.click_panel(36 + 50, 150 + kCycleDy);
  CHECK(chart.click_mode() == ClickMode::kPickCity);
  rig.frames(2);
  // CANCEL removes it (intended), the player selected and the change kept.
  rig.scenario.modified = false;
  const std::size_t count = rig.scenario.objects().size();
  rig.click_panel(200 + 30, 45 + 10);
  CHECK(rig.scenario.objects().size() == count - 1);
  CHECK(rig.scenario.selected() == &rig.scenario.player());
  CHECK(rig.scenario.modified);
  rig.frames(2);
  CHECK(chart.shown_panel() == EditorPanel::kToolbox);
}

TEST_CASE("Extended: cancelling SELECT SHIP TYPE leaves the scenario unmodified", "[editor][extended]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const bool fixes : {false, true}) {
    Features features;
    features.editor_fixes = fixes;
    ChartRig rig(install, {}, features);
    Vessel& v = add_selected(rig, ship_class(rig.data, ShipType::kDestroyer), open_water(rig));
    const std::string before = v.class_name();
    rig.scenario.modified = false;
    rig.fake.ship_choice = -1;
    rig.click_panel(108 + 50, 150 + kCycleDy);
    REQUIRE(!rig.fake.opened_with.empty());
    CHECK(v.class_name() == before);
    // SE marks it modified anyway.
    CHECK(rig.scenario.modified == !fixes);
    // A class chosen marks it in both.
    rig.fake.ship_choice = static_cast<std::int32_t>(ship_class(rig.data, ShipType::kSubmarine));
    rig.click_panel(108 + 50, 150 + kCycleDy);
    CHECK(rig.scenario.modified);
  }
}

TEST_CASE("Extended: no memory budget, no gauge, never too large", "[editor][extended]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const bool unlimited : {false, true}) {
    Features features;
    features.editor_no_size_limit = unlimited;
    ChartRig rig(install, {}, features);
    add_selected(rig, ship_class(rig.data, ShipType::kDestroyer), open_water(rig));
    rig.scenario.header.required_memory = rig.scenario.memory_budget() + 1;
    CHECK(rig.scenario.too_large() == !unlimited);
    // The gauge's bar at (119, 44) of the toolbox, in colour 0x4b past
    // 100 %; absent without a budget.
    rig.chart->redraw_toolbox();
    rig.frames(2);
    const ui::Dialog* d = rig.chart->panel_dialog();
    REQUIRE(d != nullptr);
    int bar = 0;
    for (int y = 44 + 4; y < 44 + 13; ++y) {
      for (int x = 119 + 2; x < 193 - 1; ++x) {
        const std::uint8_t c = rig.screen.at(d->x() + x, d->y() + y);
        bar += c == 0x4b ? 1 : 0;
      }
    }
    if (unlimited) {
      CHECK(bar == 0);
    } else {
      CHECK(bar > 100);
    }
  }
}

TEST_CASE("Toolbox panels render like the original's", "[editor]") {
  SH_REQUIRE_GAME_DATA(install);
  ChartRig rig(install, "SCEN/HIST01.SCN");
  rig.gui.mouse().update(600, 440, 0, 0);
  rig.frames(3);
  CHECK(rig.chart->shown_panel() == EditorPanel::kToolbox);
  rig.save("editor_toolbox_player");
  for (const auto& o : rig.scenario.objects()) {
    if (o->type() == ObjectType::kShip) {
      rig.chart->select(*o);
      break;
    }
  }
  rig.frames(3);
  CHECK(rig.scenario.selected()->type() == ObjectType::kShip);
  rig.save("editor_toolbox_ship");
}

}  // namespace sh::editor
