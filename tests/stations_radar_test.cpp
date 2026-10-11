// The radar station on the HIST01 mission with an SD, an improved SJ and
// its PPI scope fitted.
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "assets/dat_tables.h"
#include "assets/scenario.h"
#include "assets/shp.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/stations/radar.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh {

namespace {

void save_screenshot(const std::string& name, const Image8& image, const VgaPalette& palette) {
  const char* dir = std::getenv("SH_SCREENSHOT_DIR");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / (name + ".bmp"), image, to_palette(palette));
}

class FakeHost : public AttackStationHost {
 public:
  void open_command_bar(const CommandBarMenu& m) override { menu = m; }
  void close_command_bar() override {}
  void fade_out() override { ++fades_out; }
  void fade_in() override { ++fades_in; }
  std::unique_ptr<StationPanel> make_nav_panel(PanelSite) override { return nullptr; }
  std::unique_ptr<StationPanel> make_id_book(PanelSite) override { return nullptr; }
  bool rig_for_red() const override { return false; }
  void play_sfx(std::int32_t sfx) override { sfxs.push_back(sfx); }

  CommandBarMenu menu;
  int fades_out = 0;
  int fades_in = 0;
  std::vector<std::int32_t> sfxs;
};

// HIST01 with the SD, the improved SJ (SJ-1) and the PPI display.
std::unique_ptr<Mission> radar_mission(const Install& install, const GameTables& tables) {
  Scenario s = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
  s.header.equipment = static_cast<std::uint32_t>(Equipment::kSdAirRadar) |
                       static_cast<std::uint32_t>(Equipment::kSj1Radar) |
                       static_cast<std::uint32_t>(Equipment::kPpiDisplay);
  return Mission::create(install, tables, std::move(s), {});
}

struct Rig {
  explicit Rig(const Install& install, Mission& mission)
      : cursors(Shp::load(install, "ART/CURSORS.SHP")),
        world(install, mission),
        services(install, mission, gui, palette, stations, host, world, cursors) {}

  Image8 screen{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::GuiResources resources{};
  ui::DefaultSkin skin{resources};
  ui::Gui gui{screen, [this] { return tick; }, skin};
  VgaPalette palette{};
  StationRegistry stations;
  FakeHost host;
  Shp cursors;
  WorldViewAdapter world;
  AttackServices services;

  void click(Station& s, int x, int y) {
    for (const std::uint16_t f : {ui::kEventLeftDown, ui::kEventLeftUp}) {
      ui::Event e;
      e.flags = static_cast<std::uint16_t>(f | ui::kEventMoved);
      e.x = x;
      e.y = y;
      gui.mouse().update(x, y, f == ui::kEventLeftDown ? ui::kButtonLeft : 0, 0);
      s.event(e);
      s.process();
      ++tick;
    }
  }
};

bool in_range(std::uint8_t v, int lo, int hi) { return v >= lo && v <= hi; }

}  // namespace

TEST_CASE("The radar display plots a blip at its bearing and range", "[stations2][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = radar_mission(install, tables);
  REQUIRE((mission->sim().conditions().options & 0xf0000) == 0xa0000 + 0x40000);
  HeadlessRunner(*mission).run(3);
  Rig rig(install, *mission);
  RadarSets& radar = mission->sim().sensors().radar();
  RadarSets::Sj& sj = radar.sj();

  // One fresh echo 5000 yd off the starboard beam on the 10000 yd scale.
  RadarBlip blip;
  blip.bearing = Angle10(900);
  blip.range_yd = 5000;
  blip.strength = 0x18000;
  blip.age = 8;
  sj.blips = {blip};
  sj.scale = 2;
  sj.mode = SjMode::kPpi;
  sj.beam = 0;
  CHECK(ppi_blip_offset(blip, 100, radar_display_range(2)) == fx::Point2{50, 0});

  RadarStation station(rig.services);
  station.open();
  CHECK(rig.host.fades_out == 1);
  REQUIRE(rig.host.menu.items.size() == 1);
  CHECK(rig.host.menu.items[0].frame == 2);
  station.update();
  // The improved SJ draws in shades 0x18..0x1f on a 0x1f face; the new
  // echo's big blip is centred 50 px right of the scope centre.
  CHECK(rig.screen.at(kSjScopeCentre.x - 40, kSjScopeCentre.y + 20) == 0x1f);
  int lit = 0;
  for (int dy = -3; dy <= 3; ++dy) {
    for (int dx = -3; dx <= 3; ++dx) {
      lit += in_range(rig.screen.at(kSjScopeCentre.x + 50 + dx, kSjScopeCentre.y + dy), 0x18, 0x1e) ? 1 : 0;
    }
  }
  CHECK(lit > 10);
  // The beam points to the bow, its seven lines fanning 3 deg to the left
  // (the vertical one is under the face's cross-hair).
  int beam = 0;
  for (int dx = -6; dx < 0; ++dx) {
    beam += in_range(rig.screen.at(kSjScopeCentre.x + dx, kSjScopeCentre.y - 90), 0x19, 0x1e) ? 1 : 0;
  }
  CHECK(beam >= 3);
  rig.palette = rig.services.palette();
  save_screenshot("radar_ppi", rig.screen, rig.palette);

  // On the A-scope the spike rises at column 5000 x 180 / 10000 of the scope.
  sj.mode = SjMode::kAScope;
  station.update();
  const int left = kSjScopeCentre.x - 99;
  CHECK(ascope_blip_column(blip, radar_display_range(2)) == 90);
  CHECK(in_range(rig.screen.at(left + 90, kAScopeBaseline - 30), 0x19, 0x1a));
  CHECK(rig.screen.at(left + 40, kAScopeBaseline - 30) == 0x1f);
  save_screenshot("radar_ascope", rig.screen, rig.palette);
  station.close();
}

TEST_CASE("The radar's switch turns the SJ off, to the PPI and to the A-scope", "[stations2]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = radar_mission(install, tables);
  HeadlessRunner(*mission).run(3);
  Rig rig(install, *mission);
  RadarSets::Sj& sj = mission->sim().sensors().radar().sj();
  // radar_init switches the fitted SJ on, in PPI mode.
  REQUIRE(sj.mode == SjMode::kPpi);
  RadarStation station(rig.services);
  station.open();
  station.update();

  rig.click(station, 31, 308);  // OFF
  CHECK(sj.mode == SjMode::kOff);
  rig.click(station, 31, 327);  // A-SCOPE
  CHECK(sj.mode == SjMode::kAScope);
  rig.click(station, 31, 289);  // PPI
  CHECK(sj.mode == SjMode::kPpi);

  // The range knob steps the scale 4 -> 0 with a click.
  const std::int32_t scale = sj.scale;
  rig.click(station, 170, 336);
  CHECK(sj.scale == (scale + 1) % 5);
  CHECK(rig.host.sfxs.back() == kButtonClickSfx);

  // The SD power switch.
  const bool sd = mission->sim().sensors().radar().sd().on;
  rig.click(station, 598, 311);
  CHECK(mission->sim().sensors().radar().sd().on != sd);
  station.update();
  rig.palette = rig.services.palette();
  save_screenshot("radar_switches", rig.screen, rig.palette);
  station.close();
}

}  // namespace sh
