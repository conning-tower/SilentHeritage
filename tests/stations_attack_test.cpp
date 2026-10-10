// The outside and attack stations: the 3D view adapter, bridge, periscope,
// TBT, deck gun, TDC and radar on the HIST01 mission.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/pcx.h"
#include "core/fixed_math.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/mission_loop.h"
#include "game/stations/attack_services.h"
#include "game/stations/bridge.h"
#include "game/stations/scope.h"
#include "game/stations/tbt.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/ship.h"
#include "ui/default_skin.h"
#include "ui/skin.h"

namespace sh {

namespace {

// Saves `image` with `palette` as `name`.bmp into the directory named by
// SH_SCREENSHOT_DIR, when set (for checking the views by eye).
void save_screenshot(const std::string& name, const Image8& image, const VgaPalette& palette) {
  const char* dir = std::getenv("SH_SCREENSHOT_DIR");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / (name + ".bmp"), image, to_palette(palette));
}

// Returns the number of pixels of the ship colours (16..63, the hull and
// superstructure of 3DSHIPS.CAT) in columns x0..x1 of rows y0..y1 of `image`.
int ship_pixels(const Image8& image, int x0, int x1, int y0, int y1) {
  int n = 0;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const std::uint8_t c = image.at(x, y);
      n += c >= 16 && c <= 63;
    }
  }
  return n;
}

// The first ship of the mission (HIST01: the cruiser Agano).
const Ship& first_ship(const Mission& mission) { return *mission.ships().front(); }

// Returns the relative bearing (tenths) from the player to `ship`.
std::int32_t relative_bearing(const Mission& mission, const Ship& ship) {
  const fx::Vec3 p = mission.player().position();
  const fx::Vec3 s = ship.motion().position;
  const Angle10 to = fx::heading_from_delta(s.x - p.x, s.z - p.z);
  return fx::angle_sub(to, mission.player().heading()).tenths();
}

// A host that records what the stations ask for.
class FakeHost final : public AttackStationHost {
 public:
  void open_command_bar(const CommandBarMenu& m) override { menus.push_back(m); }
  void close_command_bar() override { ++bar_closes; }
  void fade_out() override { ++fade_outs; }
  void fade_in() override { ++fade_ins; }
  std::unique_ptr<StationPanel> make_nav_panel(PanelSite) override { return nullptr; }
  std::unique_ptr<StationPanel> make_id_book(PanelSite) override { return nullptr; }
  bool rig_for_red() const override { return red; }
  void play_sfx(std::int32_t sfx) override { sfx_played.push_back(sfx); }

  std::vector<CommandBarMenu> menus;
  int bar_closes = 0;
  int fade_outs = 0;
  int fade_ins = 0;
  bool red = false;
  std::vector<std::int32_t> sfx_played;
};

// The loop's collaborators: no sound, the GUI's event queue, no bar.
class LoopHooks final : public MissionSound, public MissionInput, public CommandBar, public GameOverScreen {
 public:
  explicit LoopHooks(ui::Gui& gui) : gui_(gui) {}
  bool sfx_playing(std::int32_t) const override { return false; }
  void play_sfx_looped(std::int32_t) override {}
  void stop_sfx(std::int32_t) override {}
  void service_speech() override {}
  std::optional<ui::Event> get() override { return gui_.events().get(); }
  bool pending() const override { return gui_.events().size() != 0; }
  void event(const ui::Event&) override {}
  void process() override {}
  void update() override {}
  void close() override {}
  void show(GameOverReason) override {}

 private:
  ui::Gui& gui_;
};

// HIST01 played through the mission loop with the attack stations.
struct Rig {
  explicit Rig(const Install& i)
      : install(i),
        tables(GameTables::load(i)),
        mission(Mission::load(i, tables, "SCEN/HIST01.SCN", {})),
        resources(ui::GuiResources::load(i)),
        skin(resources),
        gui(
            screen, [this] { return tick; }, skin),
        cursors(Shp::load(i, "ART/CURSORS.SHP")),
        world(i, *mission),
        services(i, *mission, gui, palette, stations, host, world, cursors),
        hooks(gui),
        loop(*mission, stations, {hooks, hooks, hooks, hooks}) {}

  // Runs `n` frames of 5 ticks (20 a second).
  void frames(int n) {
    for (int k = 0; k < n; ++k) {
      tick += 5;
      loop.frame(5);
    }
  }
  // Presses and releases the left button at (x, y) over the next frames.
  void click(int x, int y) {
    gui.mouse().update(x, y, ui::kButtonLeft, 0);
    frames(1);
    gui.mouse().update(x, y, 0, 0);
    frames(1);
  }
  void key(std::uint16_t word) {
    gui.mouse().post_key(word, 0);
    frames(2);
  }

  const Install& install;
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
  LoopHooks hooks;
  MissionLoop loop;
};

}  // namespace

TEST_CASE("The station eyes follow the boat's heights, bearing and zoom", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  const PlayerSub& sub = mission->player();
  const fx::Vec3 p = sub.position();
  // BALAO: bridge 40 ft, periscope 68 ft (SUBS.DAT +176, +172).
  REQUIRE(sub.sub().bridge_height_ft == 40);
  REQUIRE(sub.sub().periscope_height_ft == 68);
  const std::int32_t heading = sub.heading().tenths();

  const ViewEye bridge = bridge_eye(sub, Angle10(900));
  CHECK(bridge.heading.tenths() == fx::angle_add(Angle10(heading), Angle10(900)).tenths());
  CHECK(bridge.x == p.x);
  CHECK(bridge.y == p.y + 40 * 16);
  CHECK(bridge.z == p.z);
  CHECK(bridge.zoom == Fixed16::from_int(1));
  CHECK(bridge.screen_width == 640);

  const ViewEye scope = scope_eye(sub, Angle10(-100), Fixed16::from_int(4));
  CHECK(scope.heading.tenths() == fx::angle_sub(Angle10(heading), Angle10(100)).tenths());
  CHECK(scope.y == p.y + 68 * 16);
  CHECK(scope.zoom == Fixed16::from_int(4));
  CHECK(scope.screen_width == 201);

  const ViewEye tbt = tbt_eye(sub, Angle10(0), Fixed16::from_int(2));
  CHECK(tbt.y == bridge.y);
  CHECK(tbt.screen_width == 291);

  DeckGun::Mount mount;
  mount.train = Angle10(450);
  mount.x = p.x + 100;
  mount.z = p.z - 50;
  const ViewEye gun = deck_gun_eye(sub, mount, Fixed16::from_int(1));
  CHECK(gun.heading.tenths() == fx::angle_add(Angle10(heading), Angle10(450)).tenths());
  CHECK(gun.x == mount.x);
  CHECK(gun.y == 0xa0);
  CHECK(gun.z == mount.z);
  CHECK(gun.screen_width == 201);
}

TEST_CASE("A HIST01 ship on the bridge's bearing appears at the view's centre", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  HeadlessRunner runner(*mission);
  runner.run(5);
  WorldViewAdapter view(install, *mission);
  view.update_sky(*mission);
  const Ship& agano = first_ship(*mission);
  const std::int32_t bearing = relative_bearing(*mission, agano);

  // Looking straight at it: after the boat's own wake, the ship is the
  // scene's first object, projected on the centre column.
  const ViewEye eye = bridge_eye(mission->player(), Angle10(bearing));
  WatcomRandom random;
  const RenderList list = view.render(*mission, eye, random);
  REQUIRE(view.scene().objects.size() >= 2);
  CHECK(view.scene().objects[0].type == RenderType::kShipWake);
  const RenderObject& ship = view.scene().objects[1];
  CHECK(ship.type == RenderType::kShip);
  REQUIRE(ship.sprite != nullptr);
  CHECK(ship.x == agano.motion().position.x);
  const RenderEntry& e = list.entries()[1];
  CHECK(e.owner == &ship);
  CHECK(std::abs(e.screen_x - 320) <= 2);
  CHECK(view.project_x(eye, agano.motion().position.x, agano.motion().position.z) == e.screen_x);

  // 20 degrees to its right the ship moves left by 920 x tan(20 deg) px.
  const ViewEye right = bridge_eye(mission->player(), fx::angle_add(Angle10(bearing), Angle10(200)));
  view.render(*mission, right, random);
  const auto x = view.project_x(right, agano.motion().position.x, agano.motion().position.z);
  REQUIRE(x.has_value());
  CHECK(std::abs(*x - (320 - 335)) <= 3);

  VgaPalette palette = load_pcx(install, "ART/BRIDGE.PCX").palette;
  view.shade_palette(palette);
  view.render(*mission, eye, random);
  save_screenshot("adapter_bridge", view.frame(), palette);
}

TEST_CASE("The bridge deck overlay follows the look bearing", "[stations2]") {
  // The half field of view is atan(160 / 460) = 191 tenths in the
  // original's table, so 320 / 191 px per tenth. Dead ahead: the foredeck
  // (drawn mirrored, as for every bearing right of the axis) with its axis
  // one pixel short of the centre (the truncations).
  const auto ahead = bridge_deck_view(Angle10(0));
  REQUIRE(ahead.has_value());
  CHECK(ahead->frame == 0);
  CHECK(ahead->x == 319);
  CHECK(ahead->mirror);
  // 20 degrees right: one frame further round, the axis 9 tenths left of
  // the view's left edge.
  const auto right = bridge_deck_view(Angle10(200));
  REQUIRE(right.has_value());
  CHECK(right->frame == 1);
  CHECK(right->mirror);
  CHECK(right->x == -15);
  // 20 degrees left: not mirrored.
  CHECK_FALSE(bridge_deck_view(Angle10(3400))->mirror);
  // Abeam: no deck; astern: the afterdeck frames from 9.
  CHECK_FALSE(bridge_deck_view(Angle10(900)).has_value());
  const auto astern = bridge_deck_view(Angle10(1800));
  REQUIRE(astern.has_value());
  CHECK(astern->frame == 9);
  CHECK(bridge_deck_view(Angle10(1900))->frame == 9);
  CHECK(bridge_deck_view(Angle10(2240))->frame == 9 + 2);
}

TEST_CASE("The bridge station shows HIST01's ships and trains with the keys", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  Rig rig(install);
  rig.stations.install(StationId::kBridge, std::make_unique<BridgeStation>(rig.services));
  rig.stations.select(StationId::kBridge);
  rig.frames(1);
  // The menu: conn, TBT, deck gun; the palette went black, then faded in.
  REQUIRE(rig.host.menus.size() == 1);
  CHECK(rig.host.menus[0].items.size() == 3);
  CHECK(rig.host.menus[0].items[1].frame == 0xc);
  CHECK(rig.host.fade_outs == 1);
  CHECK(rig.host.fade_ins == 1);

  // ',' trains left 5 x 2.5 = 12.5 -> 12 tenths per key repeat step 5.
  PlayerCombat& combat = rig.mission->player_combat();
  combat.view_bearing = Angle10(0);
  rig.key(0x332c);
  CHECK(combat.view_bearing.tenths() == 3600 - 12);
  rig.key(0x342e);
  rig.key(0x342e);
  CHECK(combat.view_bearing.tenths() == 12);

  // Looking at the Agano: the ship is drawn in the view's centre column
  // above the horizon of the window (rows bob..bob+199 of the frame).
  const Ship& agano = first_ship(*rig.mission);
  combat.view_bearing = Angle10(relative_bearing(*rig.mission, agano));
  rig.frames(8);
  const ViewEye eye = bridge_eye(rig.mission->player(), combat.view_bearing);
  const auto x = rig.world.project_x(eye, agano.motion().position.x, agano.motion().position.z);
  REQUIRE(x.has_value());
  CHECK(std::abs(*x - 320) <= 2);
  save_screenshot("bridge_agano", rig.screen, rig.palette);

  // Astern, the afterdeck covers the bottom of the view.
  combat.view_bearing = Angle10(1800);
  rig.frames(8);
  save_screenshot("bridge_astern", rig.screen, rig.palette);

  // Submerged, the bridge sends the player to the conn.
  MotionState& m = rig.mission->player().motion();
  m.position.y = -100 * 16;
  m.pos_y = std::int64_t{m.position.y} << 16;
  rig.frames(1);
  CHECK(rig.stations.current() == StationId::kConn);
}

TEST_CASE("The periscope trains, magnifies, marks and goes down with the scope", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  Rig rig(install);
  rig.stations.install(StationId::kScope, std::make_unique<ScopeStation>(rig.services));
  SimContext ctx = rig.mission->sim().context();
  PlayerSub& sub = rig.mission->player();
  REQUIRE(sub.can_raise_periscope());
  sub.toggle_periscope(ctx);
  REQUIRE(sub.periscope_raised());
  rig.stations.select(StationId::kScope);
  rig.frames(1);
  // The nav panel's menu: conn, recognition manual, TDC.
  REQUIRE(rig.host.menus.size() == 1);
  REQUIRE(rig.host.menus[0].items.size() == 3);
  CHECK(rig.host.menus[0].items[0].frame == 2);
  CHECK(rig.host.menus[0].items[1].frame == 0x18);
  CHECK(rig.host.menus[0].items[2].frame == 0x10);

  PlayerCombat& combat = rig.mission->player_combat();
  combat.view_bearing = Angle10(100);
  rig.key(0x342e);  // '.'
  CHECK(combat.view_bearing.tenths() == 112);

  // Trained on the Agano at 8x, she is in the middle of the 201-column
  // window: hull colours around her column near the horizon (row 130 of
  // the frame), none at the window's edge.
  const Ship& agano = first_ship(*rig.mission);
  combat.view_bearing = Angle10(relative_bearing(*rig.mission, agano));
  for (int k = 0; k < 3; ++k) {
    rig.key(0x2c7a);  // 'z'
  }
  CHECK(rig.services.zoom == Fixed16::from_int(8));
  CHECK(rig.host.sfx_played.size() == 3);
  rig.frames(12);
  // The ship moved meanwhile: train on her again for this frame.
  combat.view_bearing = Angle10(relative_bearing(*rig.mission, agano));
  rig.frames(1);
  const ViewEye eye = scope_eye(sub, combat.view_bearing, rig.services.zoom);
  CHECK(eye.zoom == Fixed16::from_int(8));
  const auto x = rig.world.project_x(eye, agano.motion().position.x, agano.motion().position.z);
  REQUIRE(x.has_value());
  // A tenth of a degree (the bearing resolution) is 13 px at 8x.
  CHECK(std::abs(*x - 100) <= 16);
  CHECK(ship_pixels(rig.world.frame(), *x - 20, *x + 20, 110, 135) > 20);
  CHECK(ship_pixels(rig.world.frame(), 0, 30, 110, 135) == 0);
  save_screenshot("scope_agano_8x", rig.screen, rig.palette);

  // Space marks her: the TDC locks on the ship on the bearing.
  rig.key(0x3920);
  CHECK(combat.tdc.target() == &agano);
  CHECK(combat.tdc.locked());

  // 'T' shows the TDC panels: menu conn, manual, nav; 'N' goes back.
  rig.key(0x1474);
  CHECK(rig.services.lower_panel == LowerPanel::kTdc);
  REQUIRE(rig.host.menus.size() == 2);
  CHECK(rig.host.menus[1].items[2].frame == 0x16);
  rig.frames(4);
  save_screenshot("scope_tdc", rig.screen, rig.palette);
  rig.key(0x1474);
  CHECK(rig.services.tdc_panel == TdcSubPanel::kGyro);
  rig.key(0x316e);  // 'n'
  CHECK(rig.services.lower_panel == LowerPanel::kNav);

  // Down periscope: back to the conn.
  sub.toggle_periscope(ctx);
  rig.frames(1);
  CHECK(rig.stations.current() == StationId::kConn);
}

TEST_CASE("The TBT shows the target through the binoculars", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  Rig rig(install);
  rig.stations.install(StationId::kTbt, std::make_unique<TbtStation>(rig.services));
  rig.stations.select(StationId::kTbt);
  rig.frames(1);
  // The nav panel's menu: bridge, recognition manual, TDC.
  REQUIRE(rig.host.menus.size() == 1);
  CHECK(rig.host.menus[0].items[0].frame == 0xe);

  // The zoom plate slides out and back after the knob: active while it
  // moves, then still.
  rig.key(0x2c7a);
  CHECK(rig.services.zoom == Fixed16::from_int(2));
  rig.frames(40);
  CHECK(rig.host.sfx_played.size() == 1);

  PlayerCombat& combat = rig.mission->player_combat();
  const Ship& agano = first_ship(*rig.mission);
  combat.view_bearing = Angle10(relative_bearing(*rig.mission, agano));
  rig.key(0x2c7a);
  rig.key(0x2c7a);
  CHECK(rig.services.zoom == Fixed16::from_int(8));
  rig.frames(12);
  combat.view_bearing = Angle10(relative_bearing(*rig.mission, agano));
  rig.frames(1);
  // The TBT's 291 columns centre the ship on column 145.
  const ViewEye eye = tbt_eye(rig.mission->player(), combat.view_bearing, rig.services.zoom);
  const auto x = rig.world.project_x(eye, agano.motion().position.x, agano.motion().position.z);
  REQUIRE(x.has_value());
  CHECK(std::abs(*x - 145) <= 16);
  CHECK(ship_pixels(rig.world.frame(), *x - 20, *x + 20, 110, 135) > 20);
  // Marked with the space bar, the target is locked and its marker drawn.
  rig.key(0x3920);
  CHECK(combat.tdc.locked());
  rig.frames(8);
  save_screenshot("tbt_agano", rig.screen, rig.palette);
}

}  // namespace sh
