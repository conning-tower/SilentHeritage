// The deck gun station on the HIST01 mission: training, firing and the
// sight's view.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/pcx.h"
#include "core/fixed_math.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/stations/deckgun.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "ui/default_skin.h"

namespace sh {

namespace {

// A host recording the station's calls.
struct FakeHost : AttackStationHost {
  std::vector<CommandBarMenu> menus;
  int closes = 0;
  int fades_in = 0;
  int fades_out = 0;
  std::vector<std::int32_t> sfx;

  void open_command_bar(const CommandBarMenu& menu) override { menus.push_back(menu); }
  void close_command_bar() override { ++closes; }
  void fade_out() override { ++fades_out; }
  void fade_in() override { ++fades_in; }
  std::unique_ptr<StationPanel> make_nav_panel(PanelSite) override { return nullptr; }
  std::unique_ptr<StationPanel> make_id_book(PanelSite) override { return nullptr; }
  bool rig_for_red() const override { return false; }
  void play_sfx(std::int32_t id) override { sfx.push_back(id); }
};

// The HIST01 mission a few seconds in, with a deck gun station.
struct Fixture {
  explicit Fixture(const Install& install)
      : tables(GameTables::load(install)),
        mission(Mission::load(install, tables, "SCEN/HIST01.SCN", {})),
        cursors(Shp::load(install, "ART/CURSORS.SHP")) {
    HeadlessRunner(*mission).run(5);
    world = std::make_unique<WorldViewAdapter>(install, *mission);
    services = std::make_unique<AttackServices>(install, *mission, gui, palette, stations, host, *world, cursors);
    station = std::make_unique<DeckGunStation>(*services);
  }

  // Posts the BIOS key `word` and runs the station's event and process.
  void key(std::uint16_t word) {
    gui.mouse().post_key(word, 0);
    while (const auto e = gui.events().get()) {
      station->event(*e);
      station->process();
    }
    station->process();
  }

  DeckGun& gun() { return mission->player_combat().deck_gun; }

  GameTables tables;
  std::unique_ptr<Mission> mission;
  Shp cursors;
  Image8 screen{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::GuiResources resources{};
  ui::DefaultSkin skin{resources};
  ui::Gui gui{screen, [this] { return tick; }, skin};
  VgaPalette palette{};
  StationRegistry stations;
  FakeHost host;
  std::unique_ptr<WorldViewAdapter> world;
  std::unique_ptr<AttackServices> services;
  std::unique_ptr<DeckGunStation> station;
};

std::size_t count_shells(const Mission& mission) {
  std::size_t n = 0;
  for (const auto& v : mission.sim().vehicles()) {
    n += v->class_id() == VehicleClass::kShell ? 1 : 0;
  }
  return n;
}

void save_screenshot(const std::string& name, const Image8& image, const VgaPalette& palette) {
  const char* dir = std::getenv("SH_SCREENSHOT_DIR");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / (name + ".bmp"), image, to_palette(palette));
}

}  // namespace

TEST_CASE("The deck gun station opens with its menu and trains out of the tower's dead zone", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  REQUIRE(f.mission->player_class().deck_gun_count == 1);
  f.station->open();
  CHECK(f.host.fades_out == 1);
  // BALAO has one gun: the menu holds the bridge only (0x11c1e0).
  REQUIRE(f.host.menus.size() == 1);
  REQUIRE(f.host.menus[0].items.size() == 1);
  CHECK(f.host.menus[0].items[0].frame == 14);
  f.host.menus[0].items[0].action();
  CHECK(f.stations.current() == StationId::kBridge);

  const std::int32_t m = f.gun().selected();
  f.gun().set_manual(true);
  f.station->update();  // enables the manual controls
  // ',' trains left by 5 tenths (the key's step).
  f.gun().mount(m).train = Angle10(300);
  f.key(0x332c);
  CHECK(f.gun().mount(m).train.tenths() == 295);
  // A mount cannot train within 7 deg of the tower: dead astern for the
  // forward gun, dead ahead for the aft one.
  if (m == 0) {
    f.gun().mount(m).train = Angle10(1725);
    f.key(0x342e);
    CHECK(f.gun().mount(m).train.tenths() == 1730);
  } else {
    f.gun().mount(m).train = Angle10(3525);
    f.key(0x342e);
    CHECK(f.gun().mount(m).train.tenths() == 3530);
  }
  f.station->close();
}

TEST_CASE("The deck gun's fire button fires a shell and counts the round", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  f.station->open();
  f.gun().set_manual(true);
  f.station->update();
  const std::int32_t m = f.gun().selected();
  REQUIRE(f.gun().mount(m).ready);
  const std::int32_t ammo = f.gun().ammo();
  const std::size_t shells = count_shells(*f.mission);
  f.key(0x2166);  // 'f'
  CHECK(count_shells(*f.mission) == shells + 1);
  CHECK(f.gun().ammo() == ammo - 1);
  CHECK(f.gun().mount(m).reload >= 200);
  // The click: sfx 2 at volume 30 (`sound_play_sfx(2, 0x1e, -1)`).
  CHECK(f.host.sfx == std::vector<std::int32_t>{kButtonClickSfx});
  // Crew control disables the fire button.
  f.station->toggle_manual();
  f.station->update();
  f.gun().mount(m).ready = true;
  f.key(0x2166);
  CHECK(count_shells(*f.mission) == shells + 1);
  f.station->close();
}

TEST_CASE("The deck gun's sight shows a HIST01 ship on its train", "[stations2]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  const std::int32_t m = f.gun().selected();
  const DeckGun::Mount& mount = f.gun().mount(m);
  const Ship& agano = *f.mission->ships().front();
  const fx::Vec3 s = agano.motion().position;
  const std::int32_t bearing = fx::heading_from_delta(s.x - mount.x, s.z - mount.z).tenths();
  f.gun().mount(m).train = fx::angle_sub(Angle10(bearing), f.mission->player().heading());
  f.station->open();
  f.station->update();
  // The ship is on the sight's centre column, the target on its train.
  const ViewEye eye = deck_gun_eye(f.mission->player(), mount, f.services->zoom);
  const auto x = f.world->project_x(eye, s.x, s.z);
  REQUIRE(x.has_value());
  CHECK(std::abs(*x - 100) <= 2);
  CHECK(f.gun().target() == &agano);
  // The view is copied under the sight at (0x15e, 0x4f): the sky above the
  // horizon is no PCX colour there.
  bool changed = false;
  const Image8 pcx = load_pcx(install, "ART/DECKGUN.PCX").image;
  for (int y = 0x4f + 10; y < 0x4f + 60; ++y) {
    changed = changed || f.screen.at(0x15e + 100, y) != pcx.at(0x15e + 100, y);
  }
  CHECK(changed);
  f.station->process();
  save_screenshot("deckgun", f.screen, f.palette);
  f.station->close();
}

}  // namespace sh
