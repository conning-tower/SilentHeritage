// Game regressions: a career resumed on patrol keeps its
// state, two-point convoy routes move, the roster after a deletion, damaged
// saves and state lists, the contact groups' forgotten vehicles and the
// chart view kept in the state lists.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/patrol_zones.h"
#include "assets/world.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "core/install.h"
#include "game/career_menus.h"
#include "game/career_save.h"
#include "game/career_ui.h"
#include "game/frontend.h"
#include "game/mission.h"
#include "game/mission_save.h"
#include "game/mission_session.h"
#include "game/patrol.h"
#include "game/patrol_contacts.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game/sim_state.h"
#include "game/stations/chart.h"
#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/geometry.h"
#include "sim/sensors.h"
#include "sim/state_blocks.h"
#include "sim/state_io.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh {
namespace {

// A scratch directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_game_review_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A vehicle standing for a spawned unit.
class FakeVehicle final : public Vehicle {
 public:
  VehicleClass class_id() const override { return VehicleClass::kShip; }
  void init(SimContext& /*ctx*/) override {}
  std::int32_t update_pass() const override { return 0; }
  void update(SimContext& /*ctx*/, Fixed16 /*dt*/) override {}
  fx::Vec3 position() const override { return {}; }
};

// Keeps one fake vehicle per unit spawned.
class FakeSpawner final : public ContactSpawner {
 public:
  std::int32_t free_convoy_group() const override { return 1; }
  std::vector<Vehicle*> spawn(const Scenario& units) override {
    std::vector<Vehicle*> out;
    const std::size_t n = units.ships.size() + units.enemy_subs.size() + units.aircraft.size();
    for (std::size_t i = 0; i < n; ++i) {
      live.push_back(std::make_unique<FakeVehicle>());
      out.push_back(live.back().get());
    }
    return out;
  }
  void remove(Vehicle& vehicle) override {
    std::erase_if(live, [&](const std::unique_ptr<FakeVehicle>& v) { return v.get() == &vehicle; });
  }
  bool contains(const Vehicle* vehicle) const override {
    return std::ranges::any_of(live, [&](const std::unique_ptr<FakeVehicle>& v) { return v.get() == vehicle; });
  }
  void limit_compression(std::int32_t /*max*/) override {}

  std::vector<std::unique_ptr<FakeVehicle>> live;
};

GameClock clock_on(std::int32_t year, std::int32_t month, std::int32_t day) {
  GameClock c;
  c.init(year, month, day, 12, 0);
  return c;
}

// A view from far off the map: nothing materializes.
ContactView far_view() {
  ContactView v;
  v.player_x = -0x40000000;
  v.player_z = -0x40000000;
  v.frame_hundredths = 100;
  return v;
}

// The shipped career (ROSTER0.SAV) with its log, in slot `slot`.
Career shipped_career(const SaveDir& saves, std::int32_t slot) {
  const std::optional<RosterFile> roster = load_roster(saves, 0);
  REQUIRE(roster);
  Career career = roster->career;
  career.record.roster_slot = slot;
  return career;
}

// The bytes of a version 7 state with one patrol log entry (`tonnage`),
// cut `cut` bytes after the end of the patrol log.
std::vector<std::uint8_t> state_cut_after_log(std::int32_t tonnage, std::size_t cut) {
  SimStateFile f;
  f.ui = SavedStationUi{};
  StateImage entry(kPatrolLogRecordSize);
  entry.set_i32(0x47, tonnage);
  f.patrol_log.push_back(std::move(entry));
  std::vector<std::uint8_t> bytes = f.serialize();
  ByteReader r(bytes, "state");
  SimStateFile::parse(r, kSimStateVersion, true);
  bytes.resize(bytes.size() - r.remaining() + cut);
  return bytes;
}

}  // namespace

TEST_CASE("A career selected on patrol from the roster gets its patrol state", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  TempDir dir;
  const SaveDir saves(dir.path(), &install, "SAVE/");
  Career career = shipped_career(saves, 1);
  career.record.on_patrol = 1;
  career.patrol_state = {1, 2, 3, 4, 5};
  REQUIRE(save_career(saves, career));
  // The file as an older version 6 roster (same layout).
  std::vector<std::uint8_t> data = *saves.read("ROSTER1.SAV");
  data[0] = 6;
  REQUIRE(saves.write("ROSTER1.SAV", data));

  const std::vector<RosterEntry> entries = list_rosters(saves);
  const auto entry = std::ranges::find(entries, std::string("ROSTER1.SAV"), &RosterEntry::file);
  REQUIRE(entry != entries.end());
  Career selected = career_from_roster_entry(*entry, tables);
  CHECK(selected.patrol_state.empty());
  load_roster_patrol_state(saves, selected);
  CHECK(selected.patrol_state == std::vector<std::uint8_t>{1, 2, 3, 4, 5});
  CHECK(selected.patrol_state_version == 6);

  // In port nothing is read.
  Career in_port = career_from_roster_entry(*entry, tables);
  in_port.record.on_patrol = 0;
  load_roster_patrol_state(saves, in_port);
  CHECK(in_port.patrol_state.empty());
}

TEST_CASE("A two-point route group heads for its second point and moves", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  const auto palau = std::ranges::find(zones, std::string("PALAU.CR"), &PatrolZone::routes);
  REQUIRE(palau != zones.end());
  ZoneData zone = ZoneData::load(install, *palau);
  REQUIRE(zone.routes.size() == 9);
  REQUIRE(zone.routes[8].waypoints.size() == 2);
  zone.bases.clear();
  zone.owners.clear();
  zone.zone.sampan_density = 0;
  const World world = parse_world(install.read("DATA/" + palau->world), palau->world);
  FakeSpawner spawner;
  ContactGroups groups(tables, zone, world, spawner);
  WatcomRandom random(77);
  const GameClock clock = clock_on(12, 6, 1);  // 1 July 1942
  groups.init(clock, random);
  const auto route8 = [&groups]() -> const ContactGroup* {
    for (const auto& g : groups.groups()) {
      if (g->kind == ContactGroupKind::kRoute && g->route == 8 && !g->finished) {
        return g.get();
      }
    }
    return nullptr;
  };
  for (int i = 0; i < 3 * 24 * 3600 && route8() == nullptr; ++i) {
    groups.update(clock, far_view(), random);
  }
  const ContactGroup* g = route8();
  REQUIRE(g != nullptr);
  REQUIRE(g->waypoints.size() == 2);
  CHECK(g->waypoint_index == 1);
  CHECK(g->target.x == g->waypoints[1].x);
  CHECK(g->target.z == g->waypoints[1].z);
  CHECK(g->speed > 0);
  const GroupPoint start = g->position;
  for (int i = 0; i < 600; ++i) {
    groups.update(clock, far_view(), random);
  }
  REQUIRE(route8() == g);
  CHECK((g->position.x != start.x || g->position.z != start.z));
  // It comes closer to its second point.
  const auto distance = [g](const GroupPoint& p) {
    return fx::approx_distance(g->target.x - p.x, g->target.z - p.z);
  };
  CHECK(distance(g->position) < distance(start));
}

TEST_CASE("The roster starts on its last row after the last one was deleted", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  // A front end over the install whose host clicks VIEW once.
  class Host final : public FrontendHost, public MissionRunner {
   public:
    std::uint32_t ticks() override { return tick_; }
    void present(const Image8&, const VgaPalette&) override {
      tick_ += 2;
      if (++frames_ > 50'000) {
        throw std::runtime_error("the screen did not end");
      }
    }
    void poll_input(ui::Gui& gui) override {
      if (++idle_ == 40) {
        // VIEW, at (0xb + 0xb1 + 0x79 / 2, 0xe + 400 + 17).
        gui.mouse().update(0xb + 0xb1 + 0x79 / 2, 0xe + 400 + 17, ui::kButtonLeft, 0);
        gui.mouse().update(0xb + 0xb1 + 0x79 / 2, 0xe + 400 + 17, 0, 0);
      }
    }
    VideoSound* video_sound() override { return nullptr; }
    void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
    void apply_sound_options(const GameOptions&) override {}
    void report(std::string_view) override {}
    std::int32_t run(const MissionRequest&) override { return 1; }
    MissionOutcome outcome() const override { return {}; }

   private:
    std::uint32_t tick_ = 0;
    int idle_ = 0;
    int frames_ = 0;
  };
  TempDir dir;
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir.path(), &install, paths.dir(GameDir::kSave));
  Host host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  CareerUi ui(front_end);
  const std::optional<RosterFile> roster = load_roster(saves, 0);
  REQUIRE(roster);
  const std::vector<CareerRecord> careers = {roster->career.record};
  // Row 1 was deleted: one career left.
  int selection = 1;
  CHECK(roster_select_screen(ui, careers, selection, true) == career_code::kRosterView);
  CHECK(selection == 0);
}

TEST_CASE("A roster that cannot be written keeps its last save", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  TempDir dir;
  const SaveDir saves(dir.path(), &install, "SAVE/");
  Career career = shipped_career(saves, 1);
  REQUIRE(save_career(saves, career));
  const std::vector<std::uint8_t> before = *saves.read("ROSTER1.SAV");
  // The atomic write's temporary file cannot be created.
  std::filesystem::create_directory(dir.path() / "ROSTER1.SAV.tmp");
  career.record.name = "Other";
  CHECK_FALSE(save_career(saves, career));
  CHECK(saves.read("ROSTER1.SAV") == before);
}

TEST_CASE("Damaged saves and rosters read as none", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  TempDir dir;
  const SaveDir saves(dir.path(), &install, "SAVE/");

  // MISS0.SAV: version 7 and a header, its state lists cut short.
  std::vector<std::uint8_t> miss(1 + kMissionHeaderSize + 100, 0);
  miss[0] = 7;
  REQUIRE(saves.write("MISS0.SAV", miss));
  CHECK_FALSE(load_mission_save(saves, 0).has_value());

  // A career on patrol whose state is garbage: VIEW CAREER shows its log.
  Career career = shipped_career(saves, 1);
  career.record.on_patrol = 1;
  career.patrol_state = {1, 2, 3};
  REQUIRE(save_career(saves, career));
  const std::optional<RosterFile> saved = load_roster(saves, 1);
  REQUIRE(saved);
  CareerView view = career_view(saves, saved->career.record);
  CHECK(view.events.size() == career.log.size());
  CHECK(view.record.tonnage == saved->career.record.tonnage);
}

TEST_CASE("VIEW CAREER reads a patrol state up to its patrol log", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  TempDir dir;
  const SaveDir saves(dir.path(), &install, "SAVE/");
  // The patrol log is whole, the TDC block after it is cut.
  const std::vector<std::uint8_t> state = state_cut_after_log(4321, 10);
  {
    ByteReader r(state, "state");
    CHECK_THROWS_AS(SimStateFile::parse(r, kSimStateVersion), DataError);
  }
  Career career = shipped_career(saves, 1);
  career.record.on_patrol = 1;
  career.patrol_state = state;
  REQUIRE(save_career(saves, career));
  const std::optional<RosterFile> saved = load_roster(saves, 1);
  REQUIRE(saved);
  const CareerView view = career_view(saves, saved->career.record);
  REQUIRE(view.events.size() == career.log.size() + 1);
  CHECK(view.events.back().log.tonnage == 4321);
  CHECK(view.record.tonnage == saved->career.record.tonnage + 4321);
}

TEST_CASE("State lists before version 3 have no radio block", "[gamereview]") {
  // The player block without the radio page, then the patrol log, routes
  // and groups counts (no vehicles before version 4).
  constexpr std::size_t kPlayerWithoutRadio = 0x144 + 0x24 + 0xdc + 0x40 + 0x200 + 0x488;
  std::vector<std::uint8_t> v2(kPlayerWithoutRadio + 12, 0);
  ByteReader r(v2, "v2 state");
  const SimStateFile f = SimStateFile::parse(r, 2);
  CHECK(r.at_end());
  CHECK(f.radio_waiting.empty());
  CHECK(f.radio_log.empty());
  CHECK_FALSE(f.ui.has_value());

  // Version 3: the radio page, the buffer, then counts whose sum overflows.
  ByteWriter w;
  w.zeros(kPlayerWithoutRadio + 4 + kRadioBufferSize);
  w.i32(std::numeric_limits<std::int32_t>::max());
  w.i32(1);
  const std::vector<std::uint8_t> v3 = w.take();
  ByteReader r3(v3, "v3 state");
  CHECK_THROWS_AS(SimStateFile::parse(r3, 3), DataError);
}

TEST_CASE("Extension counts past the end and bad routes are rejected", "[gamereview]") {
  VehicleLinks links;
  {
    const std::vector<std::uint8_t> ext = {0xff, 0xff, 0xff, 0x7f, 0, 0, 0, 0};
    ByteReader reader(ext, "extension");
    StateImage none;
    StateIn in(none, &reader, links);
    std::vector<std::int32_t> items;
    CHECK_THROWS_AS(bind_x_vector(in, items, [](StateIn& x, std::int32_t& v) { x.x_i32(v); }), DataError);
  }
  // A route of x[25], z[25], count, then its index.
  StateImage image(25 * 8 + 8);
  constexpr std::uint32_t kCount = 25 * 8;
  constexpr std::uint32_t kIndex = kCount + 4;
  StateIn in(image, nullptr, links);
  Route route;
  std::int32_t index = 0;
  image.set_i32(kCount, 26);
  CHECK_THROWS_AS(bind_route(in, 0, 100, kCount, route), DataError);
  image.set_i32(kCount, -1);
  CHECK_THROWS_AS(bind_route(in, 0, 100, kCount, route), DataError);
  image.set_i32(kCount, 3);
  bind_route(in, 0, 100, kCount, route);
  CHECK(route.count == 3);
  // An index past the count is a finished route; a negative one is damage.
  image.set_i32(kIndex, 7);
  bind_route_index(in, kIndex, index);
  CHECK(index == 7);
  image.set_i32(kIndex, -2);
  CHECK_THROWS_AS(bind_route_index(in, kIndex, index), DataError);
}

TEST_CASE("The contact groups forget a removed vehicle at once", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  ZoneData zone = ZoneData::load(install, zones.at(0));
  const World world = parse_world(install.read("DATA/WORLD-1.WLD"), "WORLD-1.WLD");
  zone.routes.clear();
  zone.zone.sampan_density = 0;
  // Hiroshima alone: its anchored ships materialize with the boat nearby.
  zone.bases.resize(1);
  zone.owners.resize(1);
  const BaseRecord& base = zone.bases[0];
  FakeSpawner spawner;
  ContactGroups groups(tables, zone, world, spawner);
  WatcomRandom random(42);
  const GameClock clock = clock_on(13, 2, 15);
  groups.init(clock, random);
  groups.update_bases(clock, random, false);
  groups.update(clock, far_view(), random);
  // The boat 9500 yd from the harbour, away from the patrol point, with a
  // 10000 yd radar.
  const Angle10 away = fx::heading_from_delta(base.harbor.x - base.patrol.x, base.harbor.z - base.patrol.z);
  const fx::SinCos r = fx::angle_sincos(away);
  ContactView view;
  view.player_x = base.harbor.x + fx::mul(9500 * 0x30, r.sin);
  view.player_z = base.harbor.z + fx::mul(9500 * 0x30, r.cos);
  view.sj_range_yd = 10000;
  groups.update(clock, view, random);
  REQUIRE(spawner.live.size() >= 2);
  Vehicle& gone = *spawner.live[0];
  ContactGroup* g = groups.group_of(gone);
  REQUIRE(g != nullptr);
  const std::size_t members = g->vehicles.size();

  // The simulation's forget reaches the groups through the sensors.
  Sensors sensors;
  sensors.environment().contact_groups = &groups;
  sensors.forget(gone);
  CHECK(groups.group_of(gone) == nullptr);
  CHECK(g->vehicles.size() == members - 1);
  CHECK(std::ranges::find(g->vehicles, &gone) == g->vehicles.end());
  CHECK(groups.group_of(*spawner.live[1]) == g);
}

TEST_CASE("A single mission's scenario group forgets its deleted vehicles", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  ContactGroup* g = mission->scenario_group();
  REQUIRE(g != nullptr);
  REQUIRE_FALSE(g->vehicles.empty());
  Vehicle* gone = g->vehicles.front();
  const std::size_t members = g->vehicles.size();
  gone->remove();
  mission->simulate_frame(10, false);
  CHECK(g->vehicles.size() == members - 1);
  CHECK(std::ranges::find(g->vehicles, gone) == g->vehicles.end());
}

TEST_CASE("The chart's view and panels are kept in the state lists", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const GameTables tables = GameTables::load(install);
  ui::GuiResources resources = ui::GuiResources::load(install);
  ui::DefaultSkin skin{resources};
  Image8 screen_image{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen_image, [&tick] { return tick; }, skin};
  GameOptions options;
  DetailOptions detail;
  const auto chart_of = [](MissionScreen& s) {
    auto* chart = dynamic_cast<ChartStation*>(s.stations().get(StationId::kChart));
    REQUIRE(chart != nullptr);
    return chart;
  };

  SimStateFile saved;
  ChartStation::SavedPanels panels;
  std::int32_t cx = 0;
  std::int32_t cy = 0;
  {
    MissionScreen screen(MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail},
                         Mission::load(install, tables, "SCEN/HIST01.SCN", {}));
    ChartStation* chart = chart_of(screen);
    chart->view().set_zoom(4 << 16);
    chart->view().set_center(chart->view().center_x() + (20 << 16), chart->view().center_y() - (10 << 16));
    chart->restore_panels({ChartPanel::kNavigation, {0x30, 0x120, 0x40, 0x50, 0x30, 0x120}});
    cx = chart->view().center_x();
    cy = chart->view().center_y();
    panels = chart->saved_panels();
    saved = capture_sim_state(screen.mission(), {}, {.screen = &screen});
  }
  REQUIRE(saved.ui);
  CHECK(saved.ui->chart_zoom == (4 << 16));
  CHECK(saved.ui->chart_x == cx);
  CHECK(saved.ui->chart_z == cy);
  CHECK(saved.ui->chart_panel == static_cast<std::int32_t>(ChartPanel::kNavigation));
  CHECK(saved.ui->chart_panel_xy == panels.xy);
  CHECK(panels.xy == std::array<std::int32_t, 6>{0x30, 0x120, 0x40, 0x50, 0x30, 0x120});

  MissionScreen screen(MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail},
                       Mission::load(install, tables, "SCEN/HIST01.SCN", {}));
  ChartStation* chart = chart_of(screen);
  CHECK(chart->view().zoom() == (1 << 16));
  restore_session_state(saved, {.screen = &screen});
  CHECK(chart->view().zoom() == (4 << 16));
  CHECK(chart->view().center_x() == cx);
  CHECK(chart->view().center_y() == cy);
  CHECK(chart->panel() == ChartPanel::kNavigation);
  CHECK(chart->saved_panels().xy == panels.xy);
}

}  // namespace sh

namespace sh {

TEST_CASE("The AA guns' state is the combat's and is kept in the state lists", "[gamereview]") {
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const GameTables tables = GameTables::load(install);
  ui::GuiResources resources = ui::GuiResources::load(install);
  ui::DefaultSkin skin{resources};
  Image8 screen_image{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen_image, [&tick] { return tick; }, skin};
  GameOptions options;
  DetailOptions detail;
  MissionScreen screen(MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail},
                       Mission::load(install, tables, "SCEN/HIST01.SCN", {}));
  AaGuns& guns = screen.mission().player_combat().aa_guns;
  CHECK_FALSE(screen.bar().aa_manned());
  guns.manned = true;
  guns.fire_timer = 0x12345;
  CHECK(screen.bar().aa_manned());
  const SimStateFile saved = capture_sim_state(screen.mission(), {}, {.screen = &screen});
  REQUIRE(saved.ui);
  CHECK(saved.ui->aa_manned != 0);

  std::unique_ptr<Mission> restored = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  restore_sim_state(*restored, saved, {});
  CHECK(restored->player_combat().aa_guns.manned);
  CHECK(restored->player_combat().aa_guns.fire_timer == 0x12345);
}

}  // namespace sh
