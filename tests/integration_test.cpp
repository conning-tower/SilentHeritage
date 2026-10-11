// End-to-end tests of a mission in play with every station (MissionSession):
// HIST01 driven through the GUI by the station hotkeys, a torpedo fired
// from the periscope at the first ship with its sounds, the fire control's
// view of the targets (Mission::sense), the option files, and a generated
// convoy mission (start time and boat placement).

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/scenario.h"
#include "audio/mel_layer.h"
#include "audio/sample_table.h"
#include "core/fixed_math.h"
#include "game/frontend_results.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/mission_session.h"
#include "game/saves_options.h"
#include "game/scengen.h"
#include "game/shpath.h"
#include "game/stations/chart.h"
#include "game_data.h"
#include "sim/aircraft_strike.h"
#include "sim/enemy_sub.h"
#include "sim/geometry.h"
#include "sim/torpedo.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh {
namespace {

// A scratch save directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_integration_test_" + std::to_string(stamp));
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// Returns HIST01 with the boat moved 1200 yd abeam of the first ship,
// facing it.
Scenario hist01_abeam(const Install& install) {
  Scenario scenario = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
  const ShipRecord& ship = scenario.ships.front();
  PlayerSubRecord& player = *scenario.player_sub;
  const double h = ship.heading * std::numbers::pi / 180;
  const double d = 1200.0 * kUnitsPerYard;
  player.x = ship.x + static_cast<std::int32_t>(d * std::cos(h));
  player.y = ship.y - static_cast<std::int32_t>(d * std::sin(h));
  player.heading = fx::heading_from_delta(ship.x - player.x, ship.y - player.y).tenths() / 10;
  return scenario;
}

// Returns HIST01 with HIST57's Kaidai submarine added, stopped on the
// surface 1500 yd east of the boat.
Scenario hist01_with_surfaced_sub(const Install& install) {
  Scenario scenario = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
  const Scenario hist57 = parse_scenario(install.read("SCEN/HIST57.SCN"), "HIST57");
  EnemySubRecord sub = hist57.enemy_subs.front();
  const PlayerSubRecord& player = *scenario.player_sub;
  sub.x = player.x + 1500 * kUnitsPerYard;
  sub.y = player.y;
  sub.depth = 0;
  sub.speed = 0;
  sub.orders = ShipOrders::kNone;
  sub.waypoints = {};
  scenario.enemy_subs.push_back(sub);
  return scenario;
}

// A sound lock counting how often the game thread takes it.
class CountingLock final : public SoundLock {
 public:
  void lock() override {
    mutex_.lock();
    ++count;
  }
  void unlock() override { mutex_.unlock(); }

  int count = 0;

 private:
  std::mutex mutex_;
};

// HIST01 (or `scenario`) in a session with all the stations, a headless
// sound layer behind a lock, an injected clock.
struct SessionRig {
  explicit SessionRig(const Install& game, std::optional<Scenario> scenario = std::nullopt)
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        tables(GameTables::load(game)),
        resources(ui::GuiResources::load(game)),
        saves(dir.path(), &game, paths.dir(GameDir::kSave)) {
    session = std::make_unique<MissionSession>(
        MissionScreenSetup{install, paths, exe, tables, gui, &layer, options, detail, false, &lock},
        scenario ? Mission::create(install, tables, std::move(*scenario), {})
                 : Mission::load(install, tables, "SCEN/HIST01.SCN", {}),
        saves);
  }

  MissionScreen& screen() { return session->screen(); }
  Mission& mission() { return screen().mission(); }

  // Runs `n` frames of 5 ticks, the sound rendered as the audio thread
  // would; returns false once the mission is over.
  bool frames(int n) {
    for (int i = 0; i < n; ++i) {
      tick += 5;
      if (!screen().frame(5)) {
        return false;
      }
      const std::lock_guard<SoundLock> guard(lock);
      layer.render(audio);
    }
    return true;
  }
  // Posts the BIOS key word `word`, then runs `n` frames.
  void key(std::uint16_t word, int n = 30) {
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
  SampleTable samples{&install, paths.dir(GameDir::kSfx)};
  MelLayer layer{samples, [] { return 0; }};
  CountingLock lock;
  // 50 ms of stereo output.
  std::vector<std::int16_t> audio = std::vector<std::int16_t>(MelLayer::kDefaultOutputRate / 20 * 2);
  GameOptions options;
  DetailOptions detail;
  std::unique_ptr<MissionSession> session;
};

// Posts the BIOS key word `word` and runs the two frames a hotkey takes.
void gui_key_and_frame(SessionRig& rig, std::uint16_t word) {
  rig.gui.mouse().post_key(word, 0);
  rig.frames(2);
}

std::size_t torpedo_count(const Mission& mission) {
  std::size_t n = 0;
  for (const auto& v : mission.sim().vehicles()) {
    n += v->class_id() == VehicleClass::kTorpedo ? 1 : 0;
  }
  return n;
}

// Returns the bearing of `target` relative to the boat's heading (tenths).
Angle10 relative_bearing(const Mission& mission, const Vehicle& target) {
  const fx::Vec3 p = mission.player().position();
  const fx::Vec3 s = target.position();
  const Angle10 to = fx::heading_from_delta(s.x - p.x, s.z - p.z);
  return fx::angle_sub(to, mission.player().heading());
}

TEST_CASE("The station hotkeys open every station of a mission", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install);
  REQUIRE(rig.frames(60));
  CHECK(rig.screen().stations().current() == StationId::kConn);
  struct Switch {
    std::uint16_t key;
    StationId station;
  };
  // F2 .. F10, then back to F1.
  for (const Switch s : {Switch{0x3c00, StationId::kScope}, Switch{0x3d00, StationId::kTbt},
                         Switch{0x3e00, StationId::kBridge}, Switch{0x3f00, StationId::kChart},
                         Switch{0x4000, StationId::kTdc}, Switch{0x4100, StationId::kGauges},
                         Switch{0x4200, StationId::kStatus}, Switch{0x4300, StationId::kRadar},
                         Switch{0x4400, StationId::kCabin}, Switch{0x3b00, StationId::kConn}}) {
    rig.key(s.key);
    CHECK(rig.screen().stations().current() == s.station);
  }
  CHECK(rig.mission().end_code() == 0);
}

TEST_CASE("The command bar's option changes are saved to the save directory", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install);
  CHECK(rig.session->world().options().ship_wakes);
  rig.detail.ship_wakes = kOptionOff;
  rig.screen().bar().on_detail_changed();
  CHECK_FALSE(rig.session->world().options().ship_wakes);
  CHECK(DetailOptions::load(rig.saves).ship_wakes == kOptionOff);
  CHECK(std::filesystem::exists(rig.dir.path() / "GDETAIL.SAV"));

  rig.options.speech_volume = 33;
  rig.screen().bar().on_options_changed();
  CHECK(GameOptions::load(rig.saves).speech_volume == 33);
}

TEST_CASE("A torpedo fired from the periscope at a marked ship runs to it", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install, hist01_abeam(install));
  REQUIRE(rig.frames(60));
  Mission& mission = rig.mission();
  Ship& target = *mission.ships().front();
  PlayerCombat& combat = mission.player_combat();
  // The TDC starts on the torpedo type the load selected.
  CHECK(combat.tdc.type_index() == mission.torpedo_types().selected);

  rig.key(0x3c00);  // F2: the periscope
  REQUIRE(rig.screen().stations().current() == StationId::kScope);
  combat.view_bearing = relative_bearing(mission, target);
  rig.key(0x3920);  // space: mark the target on the bearing
  REQUIRE(combat.tdc.target() == &target);
  rig.frames(300);  // the crew tracks it
  const std::size_t before = torpedo_count(mission);
  const int locks = rig.lock.count;
  gui_key_and_frame(rig, 0x7800);  // Alt-1: fire tube 1
  REQUIRE(torpedo_count(mission) == before + 1);
  // The launch sound and the order are heard, through the lock.
  CHECK(rig.layer.sfx_is_playing(PlayerCombat::kSfxFire));
  CHECK(rig.layer.speech_is_playing());
  CHECK(rig.lock.count > locks);

  // With the lookouts' range the solution is perfect (no realism): the
  // torpedo runs into the ship in about a minute and damages it.
  CHECK(combat.tdc.quality() == 100);
  const std::int32_t hull = target.setup().hit_points;
  int frames = 0;
  while (frames < 2000 && target.setup().hit_points == hull && rig.frames(1)) {
    ++frames;
  }
  CHECK(target.setup().hit_points < hull);
  CHECK(frames < 2000);
}

TEST_CASE("The fire control senses targets from their sensor slots", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install);
  REQUIRE(rig.frames(60));
  Mission& mission = rig.mission();
  Ship& ship = *mission.ships().front();
  PlayerCombat& combat = mission.player_combat();

  const TargetSensing s = combat.sensing(ship);
  CHECK(s.spotted == ship.sighted());
  CHECK(s.visual_height == ship.visual_height());
  CHECK(s.visual_height > 0);
  CHECK(s.line_of_sight);
  CHECK(s.track_time.raw() == 0);

  // As the TDC target the ship gathers tracking time, which the sensing
  // reports without adding any.
  rig.key(0x3c00);
  combat.view_bearing = relative_bearing(mission, ship);
  rig.key(0x3920);
  REQUIRE(combat.tdc.target() == &ship);
  rig.frames(100);
  const std::int32_t tracked = ship.track_time();
  CHECK(tracked > 0);
  CHECK(combat.sensing(ship).track_time.raw() == tracked);
  CHECK(combat.sensing(ship).track_time.raw() == tracked);
}

TEST_CASE("A surfaced enemy submarine is marked from the periscope and the chart", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install, hist01_with_surfaced_sub(install));
  REQUIRE(rig.frames(60));
  Mission& mission = rig.mission();
  REQUIRE(mission.enemy_subs().size() == 1);
  EnemySub& sub = *mission.enemy_subs().front();
  PlayerCombat& combat = mission.player_combat();
  REQUIRE(sub.visual_height() > 0);
  REQUIRE(combat.sensing(sub).line_of_sight);

  // Space on its bearing locks the TDC on it (slot +0x54 of the enemy
  // submarine: surfaced), and the crew's tracking gives a solution.
  rig.key(0x3c00);  // F2: the periscope
  REQUIRE(rig.screen().stations().current() == StationId::kScope);
  combat.view_bearing = relative_bearing(mission, sub);
  rig.key(0x3920);
  CHECK(combat.tdc.target() == &sub);
  CHECK(combat.tdc.locked());
  rig.frames(300);
  CHECK(combat.tdc.target() == &sub);
  CHECK(sub.track_time() > 0);
  CHECK(combat.tdc.quality() > 0);
  rig.key(0x3920);  // unlocks
  REQUIRE_FALSE(combat.tdc.locked());
  combat.view_bearing = fx::angle_add(combat.view_bearing, Angle10(900));
  rig.frames(2);
  REQUIRE(combat.tdc.target() != &sub);

  // A click on it on the chart selects and locks it, as the original's
  // `tdc_select_target_at_chart_xy` does: the chart then draws its marker.
  rig.key(0x3f00);  // F5: the chart
  auto* chart = dynamic_cast<ChartStation*>(rig.screen().loop().open_station());
  REQUIRE(chart != nullptr);
  chart->view().set_zoom(64 << 16);
  chart->center_button();
  rig.frames(2);
  const ChartPoint p = chart->view().world_to_screen(sub.position().x, sub.position().z);
  chart->click(p.x, p.y);
  CHECK(combat.tdc.target() == &sub);
  CHECK(combat.tdc.locked());
}

TEST_CASE("The results list a damaged enemy submarine still afloat and score the player's share", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = Mission::create(install, tables, hist01_with_surfaced_sub(install), {});
  REQUIRE(mission->enemy_subs().size() == 1);
  EnemySub& sub = *mission->enemy_subs().front();
  REQUIRE(sub.setup().ship_class != nullptr);
  const ShipClass& cls = *sub.setup().ship_class;
  REQUIRE(sub.side() != mission->player().side());
  // Half its hull gone, all of it the player's doing.
  sub.setup().hit_points = static_cast<std::int32_t>(cls.hull_strength) / 2;
  sub.setup().damage = 0;

  // mission_compute_results: every hostile hull afloat (slot +0x2c), the
  // submarine's fields read through slot +0xa0.
  const MissionOutcome o = mission_outcome(*mission, 0x11, {});
  REQUIRE(o.afloat.size() == mission->ships().size() + 1);
  const auto it = std::find_if(o.afloat.begin(), o.afloat.end(),
                               [&](const AfloatVessel& a) { return a.class_name == cls.name; });
  REQUIRE(it != o.afloat.end());
  CHECK(it->type == static_cast<std::int32_t>(cls.ship_type));
  CHECK(it->tonnage == static_cast<std::int32_t>(cls.displacement_tons));
  CHECK(it->hit_points == sub.setup().hit_points);
  const MissionResults r = compute_mission_results(o, 0);
  const auto damaged = std::find_if(r.sunk.begin(), r.sunk.end(), [&](const SunkListEntry& e) {
    return e.log.type == static_cast<std::int32_t>(cls.ship_type) && e.status == SunkStatus::kDamaged;
  });
  REQUIRE(damaged != r.sunk.end());
  CHECK(damaged->damage_pct == 50);

  // Sinking, it is no longer listed.
  sub.set_sinking();
  CHECK(mission_outcome(*mission, 0x11, {}).afloat.size() == mission->ships().size());
}

TEST_CASE("A downed pilot's raft is a TDC target, and a deck-gun target on the other side", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  SessionRig rig(install);
  REQUIRE(rig.frames(60));
  Mission& mission = rig.mission();
  const fx::Vec3 own = mission.player().position();
  DownedPilot& pilot = mission.sim().add(std::make_unique<DownedPilot>(
      fx::Vec3{own.x + 1000 * kUnitsPerYard, 0, own.z}, Angle10(0), mission.air_theatre()));
  REQUIRE(rig.frames(60));
  PlayerCombat& combat = mission.player_combat();
  REQUIRE(pilot.visual_height() > 0);
  REQUIRE(combat.sensing(pilot).line_of_sight);

  // `pilot_is_surface_target` (slot +0x54): the periscope's pick on its
  // bearing locks the TDC on the raft, and its tracking time (slot +0x84)
  // reaches the solution.
  rig.key(0x3c00);  // F2: the periscope
  REQUIRE(rig.screen().stations().current() == StationId::kScope);
  combat.view_bearing = relative_bearing(mission, pilot);
  rig.key(0x3920);
  CHECK(combat.tdc.target() == &pilot);
  CHECK(combat.tdc.locked());
  rig.frames(300);
  CHECK(combat.tdc.target() == &pilot);
  CHECK(pilot.track_time() > 0);
  CHECK(combat.sensing(pilot).track_time.raw() == pilot.track_time());
  CHECK(combat.tdc.quality() > 0);
  // pilot_max_speed: 0, the solution's speed is nil.
  CHECK(target_state(pilot, combat.sensing(pilot)).max_speed_kts == 0);

  // `deckgun_pick_target_on_bearing` skips the rafts of the player's side
  // (slot +0x74), not the others'.
  DeckGun& gun = combat.deck_gun;
  const SimContext ctx = mission.sim().context();
  const DeckGun::Mount& mount = gun.mount(gun.selected());
  const fx::Vec3 p = pilot.position();
  gun.mount(gun.selected()).train = fx::angle_sub(fx::heading_from_delta(p.x - mount.x, p.z - mount.z),
                                                  mission.player().heading());
  gun.pick_target_on_bearing(combat.own_boat(mission.player(), ctx), mission.sim().vehicles(), combat.sensing);
  CHECK(gun.target() == nullptr);
  pilot.set_side(mission.player().side() + 1);
  gun.pick_target_on_bearing(combat.own_boat(mission.player(), ctx), mission.sim().vehicles(), combat.sensing);
  CHECK(gun.target() == &pilot);
}

TEST_CASE("A generated convoy mission starts at its time of day, moved out from the target", "[integ]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  GeneratorParams params;
  params.day = 15;
  params.month = 5;  // June 1943
  params.year = 13;
  params.time = 1;  // day
  const std::vector<const SubClass*> subs = sub_classes_in_service(tables, params.month, params.year);
  REQUIRE_FALSE(subs.empty());
  params.sub_class = subs.back()->index_name;
  WatcomRandom random(1234);
  const MissionStartDraws draws = MissionStartDraws::draw(random);
  GeneratedMission generated = generate_single_mission(install, tables, MissionType::kConvoy, params, 0, random);
  const std::int32_t bearing = generated.start_bearing;
  const PlayerSubRecord record = *generated.scenario.player_sub;
  REQUIRE(generated.scenario.header.hour == -1);
  MissionSetup setup;
  setup.random = random;
  const std::unique_ptr<Mission> mission = Mission::create(install, tables, std::move(generated), draws, setup);

  // A day start: between 3/8 and 5/8 of the daylight past sunrise.
  const GameClock& clock = mission->sim().clock();
  CHECK(clock.hour() >= 6);
  CHECK(clock.hour() <= 17);
  CHECK(mission->restore_seed() == draws.restore_seed);
  CHECK(mission->fog_day_pct() == draws.fog_density);

  // The convoy's intercept gave a start bearing: the boat moved out from
  // the target into water 3 drafts deep.
  REQUIRE(bearing != -1);
  const fx::Vec3 p = mission->player().position();
  CHECK((p.x != record.x || p.z != record.y));
  CHECK(mission->floor().height_at(p.x, p.z) < mission->player().sub().draft_ft * -48);
  // It runs.
  HeadlessRunner runner(*mission);
  runner.run(60);
  CHECK(mission->end_code() == 0);
}

}  // namespace
}  // namespace sh
