// The TDC station and its panels on the HIST01 mission: the dials feeding
// the solution, the tube buttons firing.
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "assets/dat_tables.h"
#include "assets/pcx.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/stations/attack_services.h"
#include "game/stations/tdc.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "sim/geometry.h"
#include "sim/torpedo.h"
#include "ui/default_skin.h"

namespace sh {

namespace {

// A host recording the calls the station makes.
class FakeHost final : public AttackStationHost {
 public:
  void open_command_bar(const CommandBarMenu& m) override { menu = m; }
  void close_command_bar() override {}
  void fade_out() override { ++fade_outs; }
  void fade_in() override { ++fade_ins; }
  std::unique_ptr<StationPanel> make_nav_panel(PanelSite) override { return nullptr; }
  std::unique_ptr<StationPanel> make_id_book(PanelSite) override { return nullptr; }
  bool rig_for_red() const override { return false; }
  void play_sfx(std::int32_t sfx) override { sfx_played.push_back(sfx); }

  CommandBarMenu menu;
  int fade_outs = 0;
  int fade_ins = 0;
  std::vector<std::int32_t> sfx_played;
};

// HIST01 run for a few seconds with the services of the attack stations.
struct Harness {
  explicit Harness(const Install& install)
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
  }

  // Clicks at (x, y) and runs the station's event and process slots.
  void click(Station& station, int x, int y) {
    gui.mouse().update(x, y, ui::kButtonLeft, 0);
    pump(station);
    gui.mouse().update(x, y, 0, 0);
    pump(station);
  }
  void pump(Station& station) {
    while (auto e = gui.events().get()) {
      station.event(*e);
      station.process();
    }
    station.process();
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

void save_screenshot(const std::string& name, const Image8& image, const VgaPalette& palette) {
  const char* dir = std::getenv("SH_SCREENSHOT_DIR");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / (name + ".bmp"), image, to_palette(palette));
}

std::size_t torpedo_count(const Mission& mission) {
  std::size_t n = 0;
  for (const auto& v : mission.sim().vehicles()) {
    n += v->class_id() == VehicleClass::kTorpedo ? 1 : 0;
  }
  return n;
}

}  // namespace

TEST_CASE("TDC dials feed the manual solution and the tube gyros", "[stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install);
  Tdc& tdc = h.mission->player_combat().tdc;
  TdcStation station(h.services);
  station.open();
  station.update();
  h.pump(station);
  CHECK(h.host.fade_outs == 1);
  REQUIRE(h.host.menu.items.size() == 1);
  CHECK(h.host.menu.items[0].frame == 2);
  REQUIRE(tdc.mode() == Tdc::Mode::kAuto);

  // In auto mode the manual dials are disabled: a click on the range does
  // nothing.
  h.click(station, 330, 25);
  CHECK(tdc.range_yd() == 0);

  // The mode knob (463,380)-(521,437) turns auto to manual, with a click.
  h.click(station, 490, 410);
  REQUIRE(tdc.mode() == Tdc::Mode::kManual);
  CHECK(h.host.sfx_played == std::vector<std::int32_t>{kButtonClickSfx});

  // The range counter (320,19)-(385,37), 5 digits of 13 px: a click on the
  // first counts 10000 yd, on the second 1000 yd.
  h.click(station, 325, 25);
  h.click(station, 340, 25);
  h.click(station, 340, 25);
  CHECK(tdc.range_yd() == 12000);
  // The bearing knob centred on (212, 83): a click straight right of the
  // centre sets 90 deg, and the target is placed on it.
  h.click(station, 212 + 1 + 40, 83 + 1);
  CHECK(tdc.bearing().tenths() == 900);
  const fx::Vec3 own = h.mission->player().position();
  const std::uint32_t placed = fx::approx_distance(tdc.target_position().x - own.x, tdc.target_position().z - own.z);
  CHECK(std::abs(static_cast<std::int32_t>(placed / kUnitsPerYard) - 12000) <= 50);
  // The speed counter (334,75)-(373,93): 3 digits of 13 px, tenths of a knot.
  h.click(station, 347 + 2, 80);
  CHECK(tdc.speed_tenths() == 10);
  CHECK_FALSE(tdc.tracking());

  // The track button starts the solution: the next frames compute the tube
  // gyro angles and run times from the dialled target.
  h.click(station, 140, 160);
  CHECK(tdc.tracking());
  HeadlessRunner(*h.mission).run(1);
  CHECK(tdc.run_time(Tdc::kBow) > 0);
  CHECK(tdc.gyro(Tdc::kBow).tenths() != 0);

  // The spread knob (353, 252) to the right: 90 deg of the dial is 10 deg.
  h.click(station, 353 + 1 + 40, 252 + 1);
  station.update();
  CHECK(tdc.spread() == 100);

  station.update();
  h.pump(station);
  save_screenshot("tdc_station", h.screen, h.palette);
  station.close();
}

TEST_CASE("The TDC tube buttons fire the torpedoes", "[stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install);
  PlayerCombat& pc = h.mission->player_combat();
  REQUIRE(pc.tubes.content(0) > 0);
  TdcStation station(h.services);
  station.open();
  station.update();
  h.pump(station);
  const std::size_t before = torpedo_count(*h.mission);
  const std::uint8_t lit = h.screen.at(613, 46);

  // Tube 1's button (598,31)-(628,61); its lamp goes dark as it is released.
  h.click(station, 610, 45);
  CHECK(torpedo_count(*h.mission) == before + 1);
  CHECK(pc.tubes.content(0) <= 0);
  CHECK(h.screen.at(613, 46) != lit);
  // A second press finds it empty or loading: nothing fired.
  h.click(station, 610, 45);
  CHECK(torpedo_count(*h.mission) == before + 1);
  station.close();
}

TEST_CASE("The scope and TBT show the TDC sub-panels", "[stations2]") {
  SH_REQUIRE_INSTALL(install);
  Harness h(install);
  for (const auto& [site, file, name] : {std::tuple{TdcPanelSite::kScope, "ART/SCOPE.PCX", "tdc_scope"},
                                         std::tuple{TdcPanelSite::kTbt, "ART/TBT.PCX", "tdc_tbt"}}) {
    for (const TdcSubPanel sub : {TdcSubPanel::kSolution, TdcSubPanel::kGyro, TdcSubPanel::kTorpedo}) {
      const PcxImage pcx = load_pcx(install, file);
      h.screen.blit(pcx.image, 0, 0);
      h.palette = pcx.palette;
      h.services.tdc_panel = sub;
      TdcPanels panels(h.services, site);
      panels.open();
      panels.update();
      panels.process();
      // Tube 1's lamp is drawn lit (TBTTDC / SCOPETDC frame 0).
      const int x = site == TdcPanelSite::kScope ? 537 : 449;
      const int y = site == TdcPanelSite::kScope ? 285 : 257 + 145;
      CHECK(h.screen.at(x, y) != pcx.image.at(x, y));
      save_screenshot(std::string(name) + std::to_string(static_cast<int>(sub)), h.screen, h.palette);
      panels.close();
    }
  }
}

}  // namespace sh
