// Tests of the features ported late: the photo-recon indicators of the
// periscope (`player_draw_recon_target_marker`), the jettisoned debris
// (class 12) with its save layout, the fuel dial's second needle on
// patrol, and the screen shake (`screen_shake_update`).
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "game/mission.h"
#include "game/mission_save.h"
#include "game/mission_session.h"
#include "game/screen_shake.h"
#include "game/shpath.h"
#include "game/stations/gauges.h"
#include "game/stations/nav_panel.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "sim/aircraft_strike.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "stations_control_fixture.h"
#include "ui/default_skin.h"

namespace sh {
namespace {

// Saves `image` with `dac` as `name`.bmp in the directory of SH_SCREENS,
// when that variable is set.
void save_screen(const std::string& name, const Image8& image, const VgaPalette& dac) {
  const char* dir = std::getenv("SH_SCREENS");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / (name + ".bmp"), image, to_palette(dac));
}

// A scratch save directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_gaps_test_" + std::to_string(stamp));
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// HIST01 in a session with every station, no sound, an injected clock.
struct SessionRig {
  explicit SessionRig(const Install& game)
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        tables(GameTables::load(game)),
        resources(ui::GuiResources::load(game)),
        saves(dir.path(), &game, paths.dir(GameDir::kSave)) {
    session = std::make_unique<MissionSession>(
        MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail, false},
        Mission::load(install, tables, "SCEN/HIST01.SCN", {}), saves);
  }

  MissionScreen& screen() { return session->screen(); }
  Mission& mission() { return screen().mission(); }
  bool frames(int n) {
    for (int i = 0; i < n; ++i) {
      tick += 5;
      if (!screen().frame(5)) {
        return false;
      }
    }
    return true;
  }
  void key(std::uint16_t word, int n = 2) {
    gui.mouse().post_key(word, 0);
    frames(n);
  }

  const Install& install;
  ShPaths paths;
  ExeImage exe;
  GameTables tables;
  ui::GuiResources resources;
  ui::DefaultSkin skin{resources};
  Image8 screen_image{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen_image, [this] { return tick; }, skin};
  TempDir dir;
  SaveDir saves;
  GameOptions options;
  DetailOptions detail;
  std::unique_ptr<MissionSession> session;
};

// Returns the number of pixels of colour `c` in the box (x0, y0)-(x1, y1).
int count_colour(const Image8& image, int x0, int y0, int x1, int y1, std::uint8_t c) {
  int n = 0;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      n += image.contains(x, y) && image.at(x, y) == c;
    }
  }
  return n;
}

}  // namespace

TEST_CASE("The screen shake steps the display start every 6 ticks, then stops", "[gaps][content]") {
  SH_REQUIRE_GAME_EXE();
  ScreenShake shake;
  std::int32_t timer = 500;
  shake.update(timer);
  CHECK_FALSE(shake.active());
  CHECK(timer == 500);

  shake.trigger();
  // The first frame resets the timer and moves nothing.
  shake.update(timer);
  CHECK(timer == 0);
  CHECK(shake.display_start() == 0);
  std::vector<std::int32_t> rows;
  for (int frame = 0; frame < 100 && shake.active(); ++frame) {
    timer += 2;
    const std::int32_t before = shake.display_start();
    shake.update(timer);
    if (timer % 6 != 0) {
      CHECK(shake.display_start() == before);
    } else {
      rows.push_back(shake.display_start());
    }
    // A hit during the shake does not restart it.
    if (frame == 4) {
      shake.trigger();
    }
  }
  CHECK(rows == std::vector<std::int32_t>{20, 0, 16, 0, 12, 0, 8, 0, 4, 0});
  CHECK(timer == 60);
  CHECK_FALSE(shake.active());

  // The next one starts over.
  shake.trigger();
  shake.update(timer);
  CHECK(timer == 0);
  timer = 6;
  shake.update(timer);
  CHECK(shake.display_start() == 20);
  shake.finish();
  CHECK_FALSE(shake.active());
  CHECK(shake.display_start() == 0);
}

TEST_CASE("The displayed frame starts at the display start row", "[gaps]") {
  Image8 frame(4, 6);
  for (int y = 0; y < 6; ++y) {
    for (int x = 0; x < 4; ++x) {
      frame.set(x, y, static_cast<std::uint8_t>(10 + y));
    }
  }
  const Image8 shown = displayed_frame(frame, 2);
  CHECK(shown.at(0, 0) == 12);
  CHECK(shown.at(3, 3) == 15);
  CHECK(shown.at(1, 4) == 0);
  CHECK(shown.at(2, 5) == 0);
  CHECK(displayed_frame(frame, 0).at(1, 1) == 11);
}

TEST_CASE("A close blast shakes the mission's picture", "[gaps][content]") {
  SH_REQUIRE_GAME_DATA(install);
  test::ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  CHECK(rig.screen->display_start() == 0);
  SimContext ctx = rig.mission().sim().context();
  ctx.events.screen_shake = true;
  // The blast's frame, the reset frame, then 6 ticks to the first step.
  rig.frames(1);
  rig.frames(1);
  rig.frames(2, 3);
  CHECK(rig.screen->display_start() == 20);
  rig.frames(2, 3);
  CHECK(rig.screen->display_start() == 0);
  rig.frames(2, 3);
  CHECK(rig.screen->display_start() == 16);
  const Image8 shown = displayed_frame(rig.screen->screen(), 16);
  CHECK(shown.at(100, 0) == rig.screen->screen().at(100, 16));
  save_screen("gaps_shake", shown, rig.screen->dac());
  rig.frames(30, 3);
  CHECK(rig.screen->display_start() == 0);
}

TEST_CASE("Debris lies on the surface for 1500 s, seen but silent", "[gaps]") {
  const FlatSeaFloor floor(-3000 * kUnitsPerFoot);
  Simulation sim(MissionConditions{}, floor, 1);
  SimContext ctx = sim.context();
  Debris& debris = spawn_debris(ctx, fx::Vec3{1000, -900, 2000});
  CHECK(debris.position().x == 1000);
  CHECK(debris.position().y == 0);
  CHECK(debris.position().z == 2000);
  CHECK(debris.class_id() == VehicleClass::kDebris);
  CHECK(debris.is_debris());
  CHECK(debris.is_contact());
  CHECK(debris.noise_level() == 0);
  CHECK(debris.visual_height() == 0x40);
  CHECK(debris.update_pass() == 10);
  CHECK(debris.lifetime() == 1500 << 16);
  debris.update(ctx, Fixed16::from_int(1499));
  CHECK_FALSE(debris.removed());
  debris.update(ctx, Fixed16::from_int(1));
  CHECK(debris.removed());
}

TEST_CASE("The 1MC jettisons debris at the submerged boat, and saves keep it", "[gaps][content]") {
  SH_REQUIRE_GAME_DATA(install);
  test::ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  PlayerSub& player = rig.mission().player();
  MotionState& m = player.motion();
  m.position.y = -100 * 16;
  m.pos_y = static_cast<std::int64_t>(m.position.y) << 16;
  player.dive().depth_ft = 100;
  player.dive().ordered_depth_ft = 100;
  rig.key(0x326d);  // 'm': 1MC
  rig.frames(2);
  // From Cancel, Up skips the pilot to "Jettison Debris".
  rig.key(0x4800);
  rig.key(0x1c0d);
  rig.frames(2);
  const Debris* debris = nullptr;
  for (const auto& v : rig.mission().sim().vehicles()) {
    if (v->class_id() == VehicleClass::kDebris) {
      debris = static_cast<const Debris*>(v.get());
    }
  }
  REQUIRE(debris != nullptr);
  CHECK(rig.screen->bar().current_message() == "OOD: Jettisoning debris, sir!");
  CHECK(debris->position().y == 0);
  CHECK(std::abs(debris->position().x - player.position().x) < 200);
  CHECK(debris->lifetime() < 1500 << 16);

  // debris_write_state: the position and the time left at +0xd0..+0xdf.
  const MissionSaveFile saved = capture_mission_save(install, rig.mission(), 0, "Test", {});
  const SimStateFile::Unit* unit = nullptr;
  for (const SimStateFile::Unit& u : saved.state.vehicles) {
    if (u.cls == VehicleClass::kDebris) {
      unit = &u;
    }
  }
  REQUIRE(unit != nullptr);
  CHECK(unit->image.i32(0xd0) == debris->position().x);
  CHECK(unit->image.i32(0xd8) == debris->position().z);
  CHECK(unit->image.i32(0xdc) == debris->lifetime());

  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(saved.serialize());
  REQUIRE(parsed);
  std::unique_ptr<Mission> restored = restore_mission(install, rig.tables, *parsed, {});
  const Debris* back = nullptr;
  for (const auto& v : restored->sim().vehicles()) {
    if (v->class_id() == VehicleClass::kDebris) {
      back = static_cast<const Debris*>(v.get());
    }
  }
  REQUIRE(back != nullptr);
  CHECK(back->lifetime() == debris->lifetime());
  CHECK(back->position().x == debris->position().x);
  CHECK(back->position().z == debris->position().z);
}

TEST_CASE("On patrol the fuel dial's second needle shows the fuel to get home", "[gaps]") {
  SH_REQUIRE_INSTALL(install);
  test::ControlRig rig(install);
  rig.screen->install(StationId::kGauges,
                      std::make_unique<GaugesStation>(rig.screen->env(), rig.screen->dive_recorder()));
  rig.frames(1);
  rig.settle();
  rig.key(0x4100);  // F7
  rig.settle();
  rig.frames(2);
  const NeedleDial dial = NeedleDial::read(rig.exe, 0x1087fc);
  const ui::Rect r = dial.rect;
  const auto dial_pixels = [&] {
    std::vector<std::uint8_t> out;
    for (int y = r.y0; y <= r.y1; ++y) {
      for (int x = r.x0; x <= r.x1; ++x) {
        out.push_back(rig.screen->screen().at(x, y));
      }
    }
    return out;
  };
  const std::vector<std::uint8_t> single = dial_pixels();

  // The patrol's transit fuel, a third of the tank, seen when the station
  // opens again.
  const PlayerSub& player = rig.mission().player();
  const auto reopen = [&] {
    rig.screen->stations().request_reopen();
    rig.frames(1);
    rig.settle();
    rig.frames(2);
  };
  rig.mission().sim().conditions().options |= option::kModeCareer;
  rig.mission().set_transit_fuel(player.sub().fuel_capacity / 3);
  reopen();
  const std::vector<std::uint8_t> patrol = dial_pixels();
  CHECK(patrol != single);
  rig.save("gaps_fuel_needle");

  // In a single mission there is no second needle.
  rig.mission().sim().conditions().options &= ~option::kModeCareer;
  reopen();
  CHECK(dial_pixels() == single);
}

TEST_CASE("The periscope marks the photo-recon target and lights the in-view lamp", "[gaps][content]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install);
  REQUIRE(rig.frames(20));
  Mission& mission = rig.mission();
  PlayerSub& player = mission.player();
  mission.sim().clock().init(1942, 5, 1, 12, 0);

  // A recon target 3000 yd off the boat's bow, on the surface.
  std::vector<BaseRecord> bases(1);
  const fx::Vec3 own = player.position();
  const fx::SinCos sc = fx::angle_sincos(player.heading());
  const std::int32_t d = 3000 * kUnitsPerYard;
  bases[0].station = {own.x, 0, own.z};
  bases[0].harbor = {own.x + fx::mul(d, sc.sin), 0, own.z + fx::mul(d, sc.cos)};
  AirTheatre theatre;
  theatre.bases = bases;
  IntelReport& report = mission.sim().add(std::make_unique<IntelReport>(theatre, IntelReport::Kind::kPhotoRecon, 0));

  SimContext ctx = mission.sim().context();
  if (!player.periscope_raised()) {
    player.toggle_periscope(ctx);
  }
  rig.key(0x3c00, 4);  // F2: the periscope
  REQUIRE(rig.screen().stations().current() == StationId::kScope);
  PlayerCombat& combat = mission.player_combat();
  const auto aim = [&] {
    const fx::Vec3 p = player.position();
    const fx::Vec3 t = report.position();
    combat.view_bearing = fx::angle_sub(fx::heading_from_delta(t.x - p.x, t.z - p.z), player.heading());
  };
  aim();
  rig.frames(20);
  aim();
  rig.frames(10);
  const ContactReports& reports = mission.sim().sensors().reports();
  REQUIRE(reports.recon_target() == &report);

  // The cross on the target (56 pixels, a few under the reticle), in the
  // marker colour 0xd4, around its column (the window's middle) in the
  // world frame the scope shows.
  const Image8& world = rig.session->world().frame();
  CHECK(count_colour(world, 80, 0, 120, world.height() - 1, 0xd4) >= 30);
  save_screen("gaps_recon_scope", rig.screen().screen(), rig.screen().dac());
  // In daylight and on the bearing, the lamp: an 8 x 9 block left of
  // (196, 100) of the window (4 pixels under the scope's rim), lit in the
  // TDC blink's visible half.
  REQUIRE(reports.recon_in_view() == &report);
  std::vector<int> lamp;
  for (int i = 0; i < 20; ++i) {
    aim();
    rig.frames(1);
    lamp.push_back(count_colour(world, 183, 0, 190, world.height() - 1, 0xd4));
  }
  CHECK(std::ranges::count(lamp, 68) > 0);
  CHECK(std::ranges::count(lamp, 68) + std::ranges::count(lamp, 0) == 20);
}

}  // namespace sh
