// The extended variant's controls in the stations, on HIST01: the view
// drags of the periscope, bridge and deck gun, and the precise dials of the
// gauges' nav panel and the TDC. Each feature on, then off: off, the same
// input gives exactly the original's state and pixels.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "core/features.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/stations/attack_services.h"
#include "game/stations/bridge.h"
#include "game/stations/deckgun.h"
#include "game/stations/gauges.h"
#include "game/stations/nav_panel.h"
#include "game/stations/scope.h"
#include "game/stations/tdc.h"
#include "game/stations/view_drag.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "sim/player_combat.h"
#include "stations_control_fixture.h"
#include "ui/default_skin.h"

namespace sh {

namespace {

class FakeHost final : public AttackStationHost {
 public:
  void open_command_bar(const CommandBarMenu&) override {}
  void close_command_bar() override {}
  void fade_out() override {}
  void fade_in() override {}
  std::unique_ptr<StationPanel> make_nav_panel(PanelSite) override { return nullptr; }
  std::unique_ptr<StationPanel> make_id_book(PanelSite) override { return nullptr; }
  bool rig_for_red() const override { return false; }
  void play_sfx(std::int32_t) override {}
};

// HIST01 a few seconds in with the attack stations' services, the station
// driven directly (its event, process and update slots).
struct Harness {
  Harness(const Install& install, Features features)
      : tables(GameTables::load(install)),
        mission(Mission::load(install, tables, "SCEN/HIST01.SCN", {})),
        resources(ui::GuiResources::load(install)),
        skin(resources),
        gui(
            screen, [this] { return tick; }, skin),
        cursors(Shp::load(install, "ART/CURSORS.SHP")),
        world(install, *mission),
        services(install, *mission, gui, palette, stations, host, world, cursors) {
    HeadlessRunner(*mission).run(3);
    services.features = features;
  }

  // Runs the station's slots over the pending events, then a frame of 5
  // ticks.
  void frame(Station& station) {
    while (auto e = gui.events().get()) {
      station.event(*e);
      station.process();
    }
    station.process();
    tick += 5;
    station.update();
  }
  // Moves the pointer to (x, y) with `buttons`, then a frame.
  void mouse(Station& station, int x, int y, std::uint8_t buttons) {
    gui.mouse().update(x, y, buttons, 0);
    frame(station);
  }
  // Presses at (x0, y), moves `steps` times by `dx` (a frame each), and
  // releases.
  void drag(Station& station, int x0, int y, int dx, int steps) {
    mouse(station, x0, y, 0);
    mouse(station, x0, y, ui::kButtonLeft);
    for (int i = 1; i <= steps; ++i) {
      mouse(station, x0 + dx * i, y, ui::kButtonLeft);
    }
    mouse(station, x0 + dx * steps, y, 0);
  }

  GameTables tables;
  std::unique_ptr<Mission> mission;
  Image8 screen{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::GuiResources resources;
  ui::DefaultSkin skin;
  ui::Gui gui;
  Shp cursors;
  VgaPalette palette{};
  StationRegistry stations;
  FakeHost host;
  WorldViewAdapter world;
  AttackServices services;
};

Features with_drag() {
  Features f;
  f.drag_rotate = true;
  return f;
}
Features with_dials() {
  Features f;
  f.precise_dials = true;
  return f;
}

// The periscope view's centre and a 10-pixel step (200 px/s at 5 ticks a
// frame: no acceleration).
constexpr int kScopeX = 0xdb + 100;
constexpr int kScopeY = 0x77 + 100;

void raise_and_open(Harness& h, ScopeStation& scope) {
  SimContext ctx = h.mission->sim().context();
  h.mission->player().toggle_periscope(ctx);
  REQUIRE(h.mission->player().periscope_raised());
  scope.open();
  h.frame(scope);
}

void save_screenshot(const std::string& name, const Image8& image, const VgaPalette& palette) {
  const char* dir = std::getenv("SH_SCREENSHOT_DIR");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / (name + ".bmp"), image, to_palette(palette));
}

}  // namespace

TEST_CASE("controls: dragging the periscope view trains it, finer at 8x", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install, with_drag());
  ScopeStation scope(h.services);
  raise_and_open(h, scope);
  PlayerCombat& combat = h.mission->player_combat();
  combat.view_bearing = Angle10(100);
  combat.tdc.toggle_bearing_follow(true);

  // 100 px to the left: the sea follows, the view trains right 100 x
  // atan(1/460) = 12.46 deg.
  h.drag(scope, kScopeX + 50, kScopeY, -10, 10);
  CHECK(combat.view_bearing.tenths() == 100 + 125);
  CHECK(!combat.tdc.following());
  CHECK(!scope.capturing());
  save_screenshot("controls_scope_dragged", h.screen, h.palette);

  // The handles drag too; to the right trains left, through north.
  combat.view_bearing = Angle10(50);
  h.drag(scope, 0x30, 0x183 + 0x20, 10, 10);
  CHECK(combat.view_bearing.tenths() == 3600 + 50 - 125);

  // At 8x a pixel is an eighth.
  h.services.zoom = Fixed16::from_int(8);
  combat.view_bearing = Angle10(100);
  h.drag(scope, kScopeX + 50, kScopeY, -10, 10);
  CHECK(combat.view_bearing.tenths() == 100 + 16);
  scope.close();
}

TEST_CASE("controls: off, a drag on the periscope view changes nothing", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  // The same drag with the feature off, and no input at all: the same
  // bearing, pixels and random generator.
  Harness dragged(install, Features::classic());
  Harness still(install, Features::classic());
  ScopeStation a(dragged.services);
  ScopeStation b(still.services);
  raise_and_open(dragged, a);
  raise_and_open(still, b);
  dragged.mission->player_combat().view_bearing = Angle10(100);
  still.mission->player_combat().view_bearing = Angle10(100);
  dragged.drag(a, kScopeX + 50, kScopeY, -10, 10);
  for (int i = 0; i < 13; ++i) {
    still.frame(b);
  }
  CHECK(dragged.mission->player_combat().view_bearing.tenths() == 100);
  CHECK(dragged.screen.pixels().size() == still.screen.pixels().size());
  CHECK(std::equal(dragged.screen.pixels().begin(), dragged.screen.pixels().end(), still.screen.pixels().begin()));
  CHECK(dragged.mission->sim().random().state() == still.mission->sim().random().state());
}

TEST_CASE("controls: a periscope drag draws nothing from the game's generator", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  // The drag, and the bearing it ends on set at the start without input:
  // the generator is where it would be.
  Harness dragged(install, with_drag());
  Harness set(install, Features::classic());
  ScopeStation a(dragged.services);
  ScopeStation b(set.services);
  raise_and_open(dragged, a);
  raise_and_open(set, b);
  dragged.mission->player_combat().view_bearing = Angle10(100);
  set.mission->player_combat().view_bearing = Angle10(225);
  dragged.drag(a, kScopeX + 50, kScopeY, -10, 10);
  for (int i = 0; i < 13; ++i) {
    set.frame(b);
  }
  REQUIRE(dragged.mission->player_combat().view_bearing.tenths() == 225);
  CHECK(dragged.mission->sim().random().state() == set.mission->sim().random().state());
}

TEST_CASE("controls: a bridge drag after a hold starts from the press", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install, with_drag());
  BridgeStation bridge(h.services);
  bridge.open();
  h.frame(bridge);
  PlayerCombat& combat = h.mission->player_combat();
  combat.view_bearing = Angle10(1000);
  // Held on the left half: the original repeat trains left.
  h.mouse(bridge, 200, 100, 0);
  h.mouse(bridge, 200, 100, ui::kButtonLeft);
  for (int i = 0; i < 6; ++i) {
    h.frame(bridge);
  }
  CHECK(combat.view_bearing.tenths() < 1000);
  // Then dragged 50 px right: from the press, 62 tenths left; the repeat
  // stopped and the release adds no step.
  for (int i = 1; i <= 5; ++i) {
    h.mouse(bridge, 200 + 10 * i, 100, ui::kButtonLeft);
  }
  CHECK(combat.view_bearing.tenths() == 1000 - 62);
  CHECK(bridge.capturing());
  for (int i = 0; i < 4; ++i) {
    h.frame(bridge);
  }
  h.mouse(bridge, 250, 100, 0);
  CHECK(combat.view_bearing.tenths() == 1000 - 62);
  CHECK(!bridge.capturing());
  bridge.close();
}

TEST_CASE("controls: off, a bridge drag is the original hold", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  // Moving within the train area while held is the same as holding still.
  Harness moved(install, Features::classic());
  Harness held(install, Features::classic());
  BridgeStation a(moved.services);
  BridgeStation b(held.services);
  for (auto [h, s] : {std::pair{&moved, &a}, std::pair{&held, &b}}) {
    s->open();
    h->frame(*s);
    h->mission->player_combat().view_bearing = Angle10(1000);
    h->mouse(*s, 200, 100, 0);
    h->mouse(*s, 200, 100, ui::kButtonLeft);
  }
  std::vector<std::int32_t> bearings_moved;
  std::vector<std::int32_t> bearings_held;
  for (int i = 1; i <= 10; ++i) {
    moved.mouse(a, 200 + 10 * i, 100, ui::kButtonLeft);
    held.mouse(b, 200, 100, ui::kButtonLeft);
    bearings_moved.push_back(moved.mission->player_combat().view_bearing.tenths());
    bearings_held.push_back(held.mission->player_combat().view_bearing.tenths());
  }
  moved.mouse(a, 300, 100, 0);
  held.mouse(b, 200, 100, 0);
  CHECK(bearings_moved == bearings_held);
  CHECK(moved.mission->player_combat().view_bearing.tenths() == held.mission->player_combat().view_bearing.tenths());
  CHECK(std::equal(moved.screen.pixels().begin(), moved.screen.pixels().end(), held.screen.pixels().begin()));
  CHECK(moved.mission->sim().random().state() == held.mission->sim().random().state());
}

TEST_CASE("controls: the deck gun drag trains and elevates within its stops", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install, with_drag());
  DeckGunStation gun_station(h.services);
  gun_station.open();
  h.frame(gun_station);
  DeckGun& gun = h.mission->player_combat().deck_gun;
  const std::int32_t m = gun.selected();
  const int x = 0x15e + 100;
  const int y = 0x4f + 100;

  // Crew's fire: the sight does not drag.
  gun.set_manual(false);
  gun.mount(m).train = Angle10(m == 0 ? 300 : 2000);
  h.drag(gun_station, x, y, -10, 10);
  CHECK(gun.mount(m).train.tenths() == (m == 0 ? 300 : 2000));

  // Manual: 100 px left trains 125 tenths right; far to the left stops at
  // the tower.
  gun.set_manual(true);
  h.frame(gun_station);
  const std::int32_t start = m == 0 ? 300 : 2000;
  gun.mount(m).train = Angle10(start);
  h.drag(gun_station, x, y, -10, 10);
  CHECK(gun.mount(m).train.tenths() == start + 125);
  // 7.5 degrees short of the tower, the same drag stops at 7 degrees.
  gun.mount(m).train = Angle10(m == 0 ? 1650 : 3450);
  h.drag(gun_station, x, y, -10, 10);
  CHECK(gun.mount(m).train.tenths() == (m == 0 ? 1730 : 3530));
  CHECK(!gun.following());

  // Dragged down, the gun rises half a tenth a pixel, up to its limit.
  const std::int32_t offset = fx::angle_diff(gun.mount(m).elevation_offset, Angle10(0));
  h.mouse(gun_station, x, 100, 0);
  h.mouse(gun_station, x, 100, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    h.mouse(gun_station, x, 100 + 10 * i, ui::kButtonLeft);
  }
  h.mouse(gun_station, x, 140, 0);
  const std::int32_t elevation = fx::angle_diff(gun.mount(m).elevation, Angle10(0));
  CHECK(fx::angle_diff(gun.mount(m).elevation_offset, Angle10(0)) == std::min(offset + 20, 0x1c2 - elevation));
  gun_station.close();
}

TEST_CASE("controls: the deck gun drags from anywhere in the sight's rows, its controls there kept",
          "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  // The sight's rows 0x4f..0x116; beside it, the breech and the elevation
  // areas.
  const int y = 0x4f + 100;
  for (const bool drag : {true, false}) {
    CAPTURE(drag);
    Harness h(install, drag ? with_drag() : Features{});
    DeckGunStation gun_station(h.services);
    gun_station.open();
    h.frame(gun_station);
    DeckGun& gun = h.mission->player_combat().deck_gun;
    gun.set_manual(true);
    h.frame(gun_station);
    const std::int32_t m = gun.selected();
    const std::int32_t start = m == 0 ? 300 : 2000;

    // Left of the sight, right of it, and on a wide window's side fill
    // (the command bar's band across the window, as the mission screen
    // sets it): 100 px left trains 125 tenths right, 100 px right as much
    // left. Off, none of them drags.
    gun.mount(m).train = Angle10(start);
    h.drag(gun_station, 100, y, -10, 10);
    CHECK(gun.mount(m).train.tenths() == (drag ? start + 125 : start));
    gun.mount(m).train = Angle10(start);
    h.drag(gun_station, 570, y, -10, 10);
    CHECK(gun.mount(m).train.tenths() == (drag ? start + 125 : start));
    h.gui.mouse().set_band(ClipRect{-107, 0x1cb, 746, kScreenHeight - 1});
    gun.mount(m).train = Angle10(start);
    h.drag(gun_station, -80, y, 10, 10);
    CHECK(gun.mount(m).train.tenths() == (drag ? start - 125 : start));
    // Below the rows, the breech's panel does not drag.
    h.drag(gun_station, 100, 0x4f + 200, -10, 10);
    CHECK(gun.mount(m).train.tenths() == (drag ? start - 125 : start));

    // The zoom knob in the rows: pressed, it zooms once; moved on it, it
    // does not drag.
    const Fixed16 zoom = h.services.zoom;
    h.drag(gun_station, 0x104, 0xc0, 10, 3);
    CHECK(h.services.zoom.raw() == next_magnification(zoom).raw());
    CHECK(gun.mount(m).train.tenths() == (drag ? start - 125 : start));

    // The elevation area in the rows: held, it raises the gun.
    gun.mount(m).elevation_offset = Angle10(0);
    h.mouse(gun_station, 0x260, 0x80, 0);
    h.mouse(gun_station, 0x260, 0x80, ui::kButtonLeft);
    for (int i = 0; i < 4; ++i) {
      h.frame(gun_station);
    }
    h.mouse(gun_station, 0x260, 0x80, 0);
    CHECK(fx::angle_diff(gun.mount(m).elevation_offset, Angle10(0)) > 0);
    gun_station.close();
    // Closed, the band no longer reaches the sight's rows.
    h.gui.mouse().update(-80, y, 0, 0);
    CHECK(h.gui.mouse().x() == 0);
  }
}

TEST_CASE("controls: the TDC dials take a dragged value, only in manual", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install, with_dials());
  Tdc& tdc = h.mission->player_combat().tdc;
  TdcStation station(h.services);
  station.open();
  h.frame(station);
  REQUIRE(tdc.mode() == Tdc::Mode::kAuto);
  // Auto: the range counter (320,19)-(385,37) is disabled, its drag too.
  h.drag(station, 350, 25, 10, 5);
  CHECK(tdc.range_yd() == 0);
  h.mouse(station, 490, 410, ui::kButtonLeft);
  h.mouse(station, 490, 410, 0);
  REQUIRE(tdc.mode() == Tdc::Mode::kManual);

  // 50 px right on the range: 10 yd a pixel, nothing before the release.
  h.mouse(station, 350, 25, 0);
  h.mouse(station, 350, 25, ui::kButtonLeft);
  for (int i = 1; i <= 5; ++i) {
    h.mouse(station, 350 + 10 * i, 25, ui::kButtonLeft);
  }
  CHECK(tdc.range_yd() == 0);
  CHECK(station.capturing());
  save_screenshot("controls_tdc_range_pending", h.screen, h.palette);
  h.mouse(station, 400, 25, 0);
  CHECK(tdc.range_yd() == 500);
  CHECK(!station.capturing());

  // A tap on the range is the original count-up of its digit.
  h.mouse(station, 340, 25, ui::kButtonLeft);
  h.mouse(station, 340, 25, 0);
  CHECK(tdc.range_yd() == 1500);

  // The bearing knob (212, 83): 40 px right is 10 degrees right.
  h.mouse(station, 212, 60, 0);
  h.mouse(station, 212, 60, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    h.mouse(station, 212 + 10 * i, 60, ui::kButtonLeft);
  }
  save_screenshot("controls_tdc_bearing_pending", h.screen, h.palette);
  h.mouse(station, 252, 60, 0);
  CHECK(tdc.bearing().tenths() == 100);
  // The target follows the dialled bearing and range (`place_target`).
  const fx::Vec3 own = h.mission->player().position();
  const std::uint32_t placed = fx::approx_distance(tdc.target_position().x - own.x, tdc.target_position().z - own.z);
  CHECK(std::abs(static_cast<std::int32_t>(placed / kUnitsPerYard) - 1500) <= 20);

  // The right button cancels.
  h.mouse(station, 212, 60, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    h.mouse(station, 212 + 10 * i, 60, ui::kButtonLeft);
  }
  h.mouse(station, 252, 60, ui::kButtonLeft | ui::kButtonRight);
  h.mouse(station, 252, 60, 0);
  CHECK(tdc.bearing().tenths() == 100);
  station.close();
}

TEST_CASE("controls: off, the TDC dials are the original clicks", "[controls][stations2]") {
  SH_REQUIRE_INSTALL(install);
  // A press, a drag and a release on the bearing knob sets the angle of
  // the press, as a plain click does.
  Harness dragged(install, Features::classic());
  Harness clicked(install, Features::classic());
  TdcStation a(dragged.services);
  TdcStation b(clicked.services);
  for (auto [h, s] : {std::pair{&dragged, &a}, std::pair{&clicked, &b}}) {
    s->open();
    h->frame(*s);
    h->mouse(*s, 490, 410, ui::kButtonLeft);
    h->mouse(*s, 490, 410, 0);
  }
  dragged.drag(a, 252, 84, 10, 4);
  clicked.mouse(b, 252, 84, 0);
  clicked.mouse(b, 252, 84, ui::kButtonLeft);
  clicked.mouse(b, 252, 84, 0);
  for (int i = 0; i < 4; ++i) {
    clicked.frame(b);
  }
  const Tdc& da = dragged.mission->player_combat().tdc;
  const Tdc& cb = clicked.mission->player_combat().tdc;
  CHECK(da.bearing().tenths() == 900);
  CHECK(da.bearing().tenths() == cb.bearing().tenths());
  CHECK(std::equal(dragged.screen.pixels().begin(), dragged.screen.pixels().end(), clicked.screen.pixels().begin()));
}

namespace {

// The gauges station of a control rig.
GaugesStation& open_gauges(test::ControlRig& rig) {
  rig.screen->install(StationId::kGauges,
                      std::make_unique<GaugesStation>(rig.screen->env(), rig.screen->dive_recorder()));
  rig.frames(1);
  rig.settle();
  rig.key(0x4100);  // F7
  auto* gauges = dynamic_cast<GaugesStation*>(rig.screen->loop().open_station());
  REQUIRE(gauges != nullptr);
  rig.settle();
  rig.frames(2);
  return *gauges;
}

void hold(test::ControlRig& rig, int x, int y, std::uint8_t buttons) {
  rig.gui.mouse().update(x, y, buttons, 0);
  rig.frames(1);
}

}  // namespace

TEST_CASE("controls: the gauges' depth dial takes a dragged order", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  test::ControlRig rig(install);
  rig.screen->env().ui.features.precise_dials = true;
  GaugesStation& gauges = open_gauges(rig);
  PlayerSub& player = rig.mission().player();
  const NavTemplate& t = gauges.panel()->layout();
  // A plain click orders ~300 ft, as in the original.
  rig.click(t.depth.cx, t.depth.cy - 40);
  rig.frames(2);
  const std::int32_t ordered = player.dive().ordered_depth_ft;
  CHECK(ordered >= 280);
  CHECK(ordered <= 320);

  // 40 px right: 20 ft deeper, given on the release only.
  const Image8 before = rig.screen->screen();
  hold(rig, t.depth.cx, t.depth.cy + 30, 0);
  hold(rig, t.depth.cx, t.depth.cy + 30, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    hold(rig, t.depth.cx + 10 * i, t.depth.cy + 30, ui::kButtonLeft);
  }
  rig.frames(1);
  CHECK(player.dive().ordered_depth_ft == ordered);
  CHECK(rig.screen->loop().open_station()->capturing());
  // The ghost and its readout are drawn over the dial.
  CHECK(!std::equal(before.pixels().begin(), before.pixels().end(), rig.screen->screen().pixels().begin()));
  rig.save("controls_depth_pending");
  hold(rig, t.depth.cx + 40, t.depth.cy + 30, 0);
  rig.frames(2);
  CHECK(player.dive().ordered_depth_ft == ordered + 20);

  // The right button cancels, and the command bar does not take it (no
  // END MISSION menu).
  hold(rig, t.depth.cx, t.depth.cy + 30, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    hold(rig, t.depth.cx + 10 * i, t.depth.cy + 30, ui::kButtonLeft);
  }
  hold(rig, t.depth.cx + 40, t.depth.cy + 30, ui::kButtonLeft | ui::kButtonRight);
  hold(rig, t.depth.cx + 40, t.depth.cy + 30, ui::kButtonLeft);
  hold(rig, t.depth.cx + 40, t.depth.cy + 30, 0);
  rig.frames(3);
  CHECK(player.dive().ordered_depth_ft == ordered + 20);
  CHECK(!rig.screen->env().modals.modal_active());

  // The compass: 40 px left is 10 degrees to port of the order.
  rig.click(t.compass.cx + 40, t.compass.cy - 1);
  const std::int32_t course = player.motion().ordered_heading.tenths();
  hold(rig, t.compass.cx, t.compass.cy + 30, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    hold(rig, t.compass.cx - 10 * i, t.compass.cy + 30, ui::kButtonLeft);
  }
  rig.save("controls_course_pending");
  hold(rig, t.compass.cx - 40, t.compass.cy + 30, 0);
  CHECK(player.motion().ordered_heading.tenths() == (course + 5) / 10 * 10 - 100);
  CHECK(player.motion().steering);
}

TEST_CASE("controls: off, a drag on the depth dial is the original click", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  test::ControlRig dragged(install);
  test::ControlRig clicked(install);
  const NavTemplate& t = open_gauges(dragged).panel()->layout();
  open_gauges(clicked);
  hold(dragged, t.depth.cx, t.depth.cy - 40, 0);
  hold(dragged, t.depth.cx, t.depth.cy - 40, ui::kButtonLeft);
  for (int i = 1; i <= 4; ++i) {
    hold(dragged, t.depth.cx + 10 * i, t.depth.cy - 40, ui::kButtonLeft);
  }
  hold(dragged, t.depth.cx + 40, t.depth.cy - 40, 0);
  dragged.frames(2);
  clicked.click(t.depth.cx, t.depth.cy - 40);
  clicked.frames(6);
  CHECK(dragged.mission().player().dive().ordered_depth_ft == clicked.mission().player().dive().ordered_depth_ft);
  CHECK(dragged.mission().sim().random().state() == clicked.mission().sim().random().state());
  CHECK(std::equal(dragged.screen->screen().pixels().begin(), dragged.screen->screen().pixels().end(),
                   clicked.screen->screen().pixels().begin()));
}

}  // namespace sh
