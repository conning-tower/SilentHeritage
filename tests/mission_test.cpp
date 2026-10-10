// Mission integration: class adapters, scenario instantiation, the headless
// runner and the mission loop's frame order.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "assets/dat_tables.h"
#include "core/random.h"
#include "game/class_params.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/mission_loop.h"
#include "game/station.h"
#include "game_data.h"
#include "sim/contacts.h"
#include "sim/geometry.h"
#include "sim/sensors.h"
#include "sim/terrain.h"
#include "sim_classes.h"

namespace sh {

namespace {

// Knots in world units per second (16.16 of the motion code: 27.008).
constexpr double kUnitsPerKnotSecond = 27.008;

double distance(fx::Vec3 a, fx::Vec3 b) { return static_cast<double>(fx::approx_distance(b.x - a.x, b.z - a.z)); }

}  // namespace

TEST_CASE("The class adapters pick the motion fields of the DAT records", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);

  const ShipMotionParams fuso = ship_motion_params(*tables.find_ship("FUSO"));
  const ShipMotionParams expected = test::fuso();
  CHECK(fuso.max_power == expected.max_power);
  CHECK(fuso.hull.length_ft == expected.hull.length_ft);
  CHECK(fuso.hull.beam_ft == expected.hull.beam_ft);
  CHECK(fuso.hull.draft_ft == expected.hull.draft_ft);
  CHECK(fuso.hull.displacement == expected.hull.displacement);
  CHECK(fuso.hull.drag_cubic == expected.hull.drag_cubic);
  CHECK(fuso.hull.drag_linear == expected.hull.drag_linear);
  CHECK(fuso.hull.lateral_drag == expected.hull.lateral_drag);
  CHECK(fuso.hull.astern_drag_factor == expected.hull.astern_drag_factor);
  CHECK(fuso.hull.turn_factor == expected.hull.turn_factor);

  const SubClassParams gato = sub_class_params(*tables.find_sub("GATO"));
  const SubClassParams g = test::gato();
  CHECK(gato.length_ft == g.length_ft);
  CHECK(gato.draft_ft == g.draft_ft);
  CHECK(gato.test_depth_ft == g.test_depth_ft);
  CHECK(gato.periscope_depth_ft == g.periscope_depth_ft);
  CHECK(gato.radar_depth_ft == g.radar_depth_ft);
  CHECK(gato.periscope_height_ft == g.periscope_height_ft);
  CHECK(gato.bridge_height_ft == g.bridge_height_ft);
  CHECK(gato.awash_depth_ft == g.awash_depth_ft);
  CHECK(gato.noise_factor == g.noise_factor);
  CHECK(gato.displacement_surfaced == g.displacement_surfaced);
  CHECK(gato.ballast_neutral == g.ballast_neutral);
  CHECK(gato.ballast_max == g.ballast_max);
  CHECK(gato.diesel_hp == g.diesel_hp);
  CHECK(gato.motor_hp == g.motor_hp);
  CHECK(gato.drag_cubic_surfaced == g.drag_cubic_surfaced);
  CHECK(gato.drag_cubic_submerged == g.drag_cubic_submerged);
  CHECK(gato.drag_linear_surfaced == g.drag_linear_surfaced);
  CHECK(gato.drag_linear_submerged == g.drag_linear_submerged);
  CHECK(gato.lateral_drag == g.lateral_drag);
  CHECK(gato.astern_drag_factor == g.astern_drag_factor);
  CHECK(gato.turn_factor == g.turn_factor);
  CHECK(gato.pitch_factor == g.pitch_factor);
  CHECK(gato.diesel_count == g.diesel_count);
  CHECK(gato.motor_count == g.motor_count);
  CHECK(gato.fuel_capacity == g.fuel_capacity);
  CHECK(gato.fuel_use == g.fuel_use);
  CHECK(gato.power_surfaced == g.power_surfaced);
  CHECK(gato.power_submerged == g.power_submerged);
}

TEST_CASE("The world sea floor samples the grid", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::string path = "DATA/WORLD-8.WLD";
  const World world = parse_world(install.read(path), path);
  const WorldSeaFloor floor(world);
  const std::int32_t cell = world.cell_size();
  const std::int32_t top = world.grid.height * cell;
  // Truncating divisions: the first cell outside starts one cell out.
  CHECK(floor.height_at(-cell, 100) == WorldSeaFloor::kOutsideHeight);
  CHECK(floor.height_at(-1, 100) != WorldSeaFloor::kOutsideHeight);
  CHECK(floor.height_at(100, top + cell) == WorldSeaFloor::kOutsideHeight);
  CHECK(floor.height_at(world.grid.width * cell, 100) == WorldSeaFloor::kOutsideHeight);
  // At a grid point the height is that of its level, whatever the
  // neighbours; inside a cell it lies between the corner heights.
  std::int32_t checked = 0;
  for (std::int32_t row = 1; row + 1 < world.grid.height; row += 7) {
    for (std::int32_t column = 1; column + 1 < world.grid.width; column += 5) {
      const std::int32_t level = world.grid.at(column, row) - kCoastCell;
      const std::int32_t h = terrain_level_to_height(level, world.scale_shift);
      CHECK(floor.height_at(column * cell, top - row * cell) == h);
      const std::int32_t right = terrain_level_to_height(world.grid.at(column + 1, row) - kCoastCell, 0);
      const std::int32_t mid = floor.height_at(column * cell + cell / 2, top - row * cell);
      CHECK(mid >= std::min(h, right) - 1);
      CHECK(mid <= std::max(h, right) + 1);
      ++checked;
    }
  }
  CHECK(checked > 100);
}

TEST_CASE("Every shipped scenario instantiates", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::vector<std::string> files = install.list("SCEN", ".SCN");
  for (const std::string& f : install.list("CUSTOM", ".SCN")) {
    files.push_back(f);
  }
  REQUIRE(files.size() == 61);
  for (const std::string& file : files) {
    INFO(file);
    const std::unique_ptr<Mission> mission = Mission::load(install, tables, file, {});
    const Scenario& s = mission->scenario();
    CHECK(mission->skipped_ships() == 0);
    CHECK(mission->ships().size() == s.ships.size());
    CHECK(mission->enemy_subs().size() == s.enemy_subs.size());
    CHECK(mission->aircraft().size() == s.aircraft.size());
    CHECK(mission->sim().vehicles().size() == 1 + s.ships.size() + s.enemy_subs.size() + s.aircraft.size());
    CHECK(mission->difficulty_rating() <= 1000);
  }
}

TEST_CASE("HIST01 instantiates the scenario's units", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});

  // 1944-02-16T16:00 (the header of SCEN/HIST01.SCN).
  const GameClock& clock = mission->sim().clock();
  CHECK(clock.year() == 14);
  CHECK(clock.month() == 1);
  CHECK(clock.day() == 16);
  CHECK(clock.hour() == 16);
  CHECK(clock.minute() == 0);
  CHECK(clock.second() == 0);
  CHECK(clock.weekday() == 2);  // A Wednesday.

  const PlayerSub& player = mission->player();
  CHECK(mission->player_class().index_name == "BALAO");
  CHECK(mission->player_name() == "(SS-305) Skate");
  CHECK(mission->player_crew() == 1);
  CHECK(player.motion().position.x == 6291456);
  CHECK(player.motion().position.z == 19562496);
  // Depth 15 ft less the spawner's 1 ft, then up to the surfaced keel depth.
  REQUIRE(mission->player_class().draft_ft >= 14);
  CHECK(player.motion().position.y == -static_cast<std::int32_t>(mission->player_class().draft_ft) * 16);
  CHECK(std::abs(fx::angle_diff(player.motion().heading, Angle10(2170))) <= 1);
  CHECK(player.telegraph() == 4);

  struct Expected {
    const char* ship_class;
    std::int32_t x, z, heading, speed;
    ShipOrder orders;
    std::int32_t route_count;
  };
  const std::vector<Expected> expected = {
      {"AGANO", 6029312, 18755584, 3150, 12, ShipOrder::kConvoyLeader, 3},
      {"MUTSUKI", 5984256, 18737152, 2380, 24, ShipOrder::kEscort, 4},
      {"MUTSUKI", 6086656, 18788352, 2430, 24, ShipOrder::kEscort, 4},
  };
  const std::vector<Ship*> ships = mission->ships();
  REQUIRE(ships.size() == expected.size());
  for (std::size_t i = 0; i < ships.size(); ++i) {
    INFO(i);
    const Ship& ship = *ships[i];
    const Expected& e = expected[i];
    CHECK(ship.setup().ship_class->index_name == e.ship_class);
    CHECK(ship.motion().position == fx::Vec3{e.x, 0, e.z});
    CHECK(ship.motion().heading == Angle10(e.heading));
    CHECK(ship.motion().ordered_heading == Angle10(e.heading));
    CHECK(ship.motion().speed.to_double() == Catch::Approx(e.speed).margin(0.2));
    CHECK(ship.orders() == e.orders);
    CHECK(ship.setup().crew == 1);
    CHECK(ship.setup().convoy_group == 1);
    CHECK(ship.setup().route.count == e.route_count);
    CHECK(ship.setup().route_index == 1);
    // The first point is the start position.
    CHECK(ship.setup().route.x[0] == e.x);
    CHECK(ship.setup().route.z[0] == e.z);
    CHECK(ship.setup().hit_points == static_cast<std::int32_t>(ship.setup().ship_class->hull_strength));
    CHECK(ship.alert().level == 0);
  }
  CHECK(ships[0]->setup().route.x[2] == 3743744);
  CHECK(ships[0]->setup().route.z[2] == 25657344);

  // Sea 0, fog 0, no gradient: 30; player crew 1: 80; AGANO (warship):
  // 8534 / 500 + 2 x 40; each MUTSUKI (escort): 2 x 40.
  CHECK(mission->difficulty_rating() == 30 + 80 + 17 + 80 + 80 + 80);

  // Ten Mark 14s in the tubes; reloads: 10 forward, 4 aft.
  const TorpedoLoad& t = mission->torpedoes();
  for (std::int32_t id : t.tubes) {
    CHECK(id == 2);
  }
  const std::uint32_t bow_reloads = mission->player_class().bow_reloads;
  REQUIRE(bow_reloads >= 10);
  for (std::size_t i = 0; i < TorpedoLoad::kRackCapacity; ++i) {
    CHECK(t.bow_racks[i] == (i < 10 ? 2 : 0));
    CHECK(t.stern_racks[i] == (i < 4 ? 2 : 0));
  }
  // February 1944: the Mark 18-1 is in service, the Mark 18-2 (August 1944)
  // is not.
  const TorpedoTypes& types = mission->torpedo_types();
  CHECK(types.types[2].name == "MARK18-1");
  CHECK(types.types[0].available);
  CHECK(types.types[1].available);
  CHECK(types.types[2].available);
  CHECK(types.selected == 1);
  CHECK_FALSE(types.single_type);

  // The boat's tubes and TDC type table get the load.
  const PlayerCombat& combat = mission->player_combat();
  for (std::size_t i = 0; i < static_cast<std::size_t>(combat.tubes.count()); ++i) {
    CHECK(combat.tubes.content(i) == 2);
  }
  CHECK(combat.torpedo_types[2].name == "MARK18-1");
  CHECK(combat.torpedo_types[2].weapon_id == 3);
  CHECK(combat.crew == 1);
  // Equipment 45 (SD, SJ-1, PPI, bathythermograph) lands on option bits
  // 18, 17, 19 and 21.
  CHECK((mission->sim().conditions().options & option::kEquipmentMask) == 0x2e0000);
  CHECK(mission->sim().sensors().environment().grid != nullptr);
  CHECK(mission->sim().sensors().environment().crew == 1);

  CHECK(mission->environment_file() == "env-12.env");
}

TEST_CASE("HIST01 draws from the generator in the original order", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.random = WatcomRandom(12345);
  const std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);

  WatcomRandom r(12345);
  CHECK(mission->restore_seed() == static_cast<std::uint32_t>(r.rand()));
  CHECK(mission->fog_day_pct() == 60 + r.random_int(30));
  r.random_int(100);  // The scenario loader's roll, overwritten below.
  // No draw for the environment (tropical, as no map was loaded before)
  // nor the sea picture (low latitude) nor the thermal layer (no gradient).
  CHECK(mission->sea_picture() == 0);
  CHECK(mission->sim().conditions().scenario_roll == r.random_int(100));
  CHECK(mission->player().air() == 1000 + r.random_int(333));
  // Then ship_init's first draws for the first ship (the AI init follows).
  const Ship& first = *mission->ships().front();
  CHECK(first.reaction_rolls().flee_angle == (r.random_int(90) + 135) * 10);
  CHECK(first.reaction_rolls().breakaways_left == r.random_int(3) + 1);
  CHECK(first.reaction_rolls().preserved_timer_1dc == Fixed16::from_int((r.random_int(10) + 2) * 60));
}

TEST_CASE("HIST57 instantiates its enemy submarine and aircraft", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup a;
  a.random = WatcomRandom(1);
  MissionSetup b;
  b.random = WatcomRandom(777);
  const std::unique_ptr<Mission> m1 = Mission::load(install, tables, "SCEN/HIST57.SCN", a);
  const std::unique_ptr<Mission> m2 = Mission::load(install, tables, "SCEN/HIST57.SCN", b);

  CHECK(m1->ships().size() == 8);
  REQUIRE(m1->enemy_subs().size() == 1);
  CHECK(m1->enemy_subs()[0]->setup().ship_class->index_name == "KAIDAI TYPE");
  CHECK(m1->enemy_subs()[0]->orders() == ShipOrder::kFollowPath);
  REQUIRE(m1->aircraft().size() == 1);
  CHECK(m1->aircraft()[0]->setup().type->index_name == "VAL");
  CHECK(m1->aircraft()[0]->setup().sorties == 10000);
  CHECK(m1->aircraft()[0]->setup().escort_group == 1);
  CHECK(m1->difficulty_rating() == 1000);
  // A patrolling ship starts alerted.
  CHECK(m1->ships()[4]->orders() == ShipOrder::kPatrol);
  CHECK(m1->ships()[4]->alert().level == -1);
  CHECK(m1->ships()[1]->alert().level == 1);
  CHECK(m1->ships()[1]->alert().timer == Fixed16::from_int(3 * 1800));

  // The scenario seeds the generator with its save time (`randomize` 0):
  // everything after the loader is the same whatever the generator was.
  CHECK(m1->restore_seed() != m2->restore_seed());
  CHECK(m1->sim().conditions().scenario_roll == m2->sim().conditions().scenario_roll);
  CHECK(m1->player().air() == m2->player().air());
  for (std::size_t i = 0; i < m1->ships().size(); ++i) {
    CHECK(m1->ships()[i]->reaction_rolls().flee_angle == m2->ships()[i]->reaction_rolls().flee_angle);
  }
}

TEST_CASE("A headless minute moves the ships at their speeds", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, "SCEN/HIST01.SCN");
  std::vector<fx::Vec3> start;
  std::vector<double> speed;
  for (const Ship* ship : mission->ships()) {
    start.push_back(ship->position());
    speed.push_back(ship->motion().speed.to_double());
  }
  const fx::Vec3 player_start = mission->player().position();
  const double player_speed = mission->player().motion().speed.to_double();
  REQUIRE(player_speed > 5);

  HeadlessRunner runner(*mission);
  CHECK(runner.run(60) == 6000);
  CHECK(mission->sim().clock().hour() == 16);
  CHECK(mission->sim().clock().minute() == 1);
  CHECK(mission->end_code() == 0);

  // The ships run at about their speed while the AI turns them toward
  // their next route point.
  const std::vector<Ship*> ships = mission->ships();
  for (std::size_t i = 0; i < ships.size(); ++i) {
    INFO(i);
    const Ship& ship = *ships[i];
    const double expected = speed[i] * kUnitsPerKnotSecond * 60;
    const double run = distance(start[i], ship.position());
    CHECK(run <= expected * 1.03);
    CHECK(run >= expected * 0.75);
    const Route& route = ship.setup().route;
    const auto next = static_cast<std::size_t>(ship.setup().route_index);
    REQUIRE(next < static_cast<std::size_t>(route.count));
    const Angle10 to_next =
        geom::bearing_to({ship.position().x, ship.position().z}, {route.x[next], route.z[next]});
    CHECK(std::abs(fx::angle_diff(ship.motion().ordered_heading, to_next)) <= 20);
  }
  // The boat holds its course of 217 degrees.
  const PlayerSub& player = mission->player();
  CHECK(std::abs(fx::angle_diff(player.motion().heading, Angle10(2170))) <= 1);
  CHECK(distance(player_start, player.position()) == Catch::Approx(player_speed * kUnitsPerKnotSecond * 60).epsilon(0.05));
  const Angle10 track = geom::bearing_to({player_start.x, player_start.z}, {player.position().x, player.position().z});
  CHECK(track.tenths() == Catch::Approx(2170).margin(5));

  // The lookouts see the three ships (about 9 nm off on a clear afternoon)
  // and call the sighting with its relative bearing.
  const std::vector<PlayerContact>& contacts = mission->sim().sensors().reports().contacts();
  REQUIRE(contacts.size() == 3);
  for (const PlayerContact& c : contacts) {
    CHECK(c.kind == ContactKind::kShip);
  }
  REQUIRE_FALSE(runner.crew_messages().empty());
  CHECK(runner.crew_messages().front().id == 56);
  CHECK(runner.crew_messages().front().arg == 337);
}

namespace {

// Records the calls the loop makes, in order.
struct Recorder {
  std::vector<std::string> calls;
};

class FakeStation : public Station {
 public:
  FakeStation(Recorder& log, std::string name, bool outdoors = false, bool expensive = false)
      : log_(log), name_(std::move(name)), outdoors_(outdoors), expensive_(expensive) {}

  void open() override { log_.calls.push_back(name_ + ".open"); }
  void event(const ui::Event& e) override { log_.calls.push_back(name_ + ".event " + std::to_string(e.key)); }
  void process() override { log_.calls.push_back(name_ + ".process"); }
  void update() override {
    log_.calls.push_back(name_ + (first_frame() ? ".update first" : ".update"));
  }
  void close() override { log_.calls.push_back(name_ + ".close"); }
  bool outdoors() const override { return outdoors_; }
  bool expensive() const override { return expensive_; }

 private:
  Recorder& log_;
  std::string name_;
  bool outdoors_;
  bool expensive_;
};

class FakeSound : public MissionSound {
 public:
  explicit FakeSound(Recorder& log) : log_(log) {}
  bool sfx_playing(std::int32_t sfx) const override { return sfx == kSfxOutdoorAmbient && ambient_; }
  void play_sfx_looped(std::int32_t sfx) override {
    log_.calls.push_back("play " + std::to_string(sfx));
    ambient_ = true;
  }
  void stop_sfx(std::int32_t sfx) override {
    log_.calls.push_back("stop " + std::to_string(sfx));
    ambient_ = false;
  }
  void service_speech() override {}

 private:
  Recorder& log_;
  bool ambient_ = false;
};

class FakeInput : public MissionInput {
 public:
  std::optional<ui::Event> get() override {
    if (queue.empty()) {
      return std::nullopt;
    }
    ui::Event e = queue.front();
    queue.pop_front();
    return e;
  }
  bool pending() const override { return !queue.empty(); }
  std::deque<ui::Event> queue;
};

class FakeCommandBar : public CommandBar {
 public:
  explicit FakeCommandBar(Recorder& log) : log_(log) {}
  void event(const ui::Event& e) override { log_.calls.push_back("bar.event " + std::to_string(e.key)); }
  void process() override { log_.calls.push_back("bar.process"); }
  void update() override { log_.calls.push_back("bar.update"); }
  void close() override { log_.calls.push_back("bar.close"); }

 private:
  Recorder& log_;
};

class FakeGameOver : public GameOverScreen {
 public:
  void show(GameOverReason r) override { shown = r; }
  std::optional<GameOverReason> shown;
};

ui::Event key(std::uint16_t k) { return ui::Event{.flags = ui::kEventKey, .key = k}; }
ui::Event move(std::uint16_t tag) { return ui::Event{.flags = ui::kEventMoved, .key = tag}; }

}  // namespace

TEST_CASE("The station update is throttled at high compression", "[mission]") {
  CHECK(station_update_due(false, true, 3, 16));
  CHECK_FALSE(station_update_due(false, true, 3, 32));
  CHECK(station_update_due(false, true, 4, 32));
  CHECK_FALSE(station_update_due(false, true, 4, 128));
  CHECK(station_update_due(false, true, 0, 128));
  CHECK(station_update_due(true, true, 5, 128));
  CHECK(station_update_due(false, false, 5, 128));
}

TEST_CASE("The mission loop switches stations and pumps events in order", "[mission]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> mission = Mission::load(install, tables, "SCEN/HIST01.SCN", {});
  Recorder log;
  StationRegistry stations;
  stations.install(StationId::kConn, std::make_unique<FakeStation>(log, "conn"));
  stations.install(StationId::kBridge, std::make_unique<FakeStation>(log, "bridge", true, true));
  FakeSound sound(log);
  FakeInput input;
  FakeCommandBar bar(log);
  FakeGameOver game_over;
  MissionLoop loop(*mission, stations, {sound, input, bar, game_over});

  CHECK(station_address(StationId::kConn) == 0x11a7e4);
  CHECK(station_address(StationId::kTdc) == 0x11a844);
  CHECK(stations.current() == StationId::kConn);

  // The first frame opens the conn and updates it as its first frame;
  // queued mouse moves are drained in the same frame.
  input.queue = {move(1), move(2), key(0x3e00)};
  REQUIRE(loop.frame(5));
  CHECK(log.calls == std::vector<std::string>{"conn.open", "bar.event 1", "conn.event 1", "bar.process",
                                              "conn.process", "bar.event 2", "conn.event 2", "bar.process",
                                              "conn.process", "bar.event 15872", "conn.event 15872",
                                              "bar.process", "conn.process", "bar.update", "conn.update first"});
  CHECK(mission->sim().clock().frame_ticks() == 5);
  CHECK(loop.frame_counter() == 1);

  // A key stops the draining: the move behind it waits for the next frame.
  log.calls.clear();
  input.queue = {key(0x11b), move(3)};
  REQUIRE(loop.frame(5));
  CHECK(log.calls == std::vector<std::string>{"bar.event 283", "conn.event 283", "bar.process", "conn.process",
                                              "bar.update", "conn.update"});
  CHECK(input.queue.size() == 1);

  // Selecting the bridge closes the conn (after the command bar) and starts
  // the outdoor ambient sound.
  log.calls.clear();
  stations.select(StationId::kBridge);
  REQUIRE(loop.frame(5));
  CHECK(log.calls == std::vector<std::string>{"bar.close", "conn.close", "bridge.open", "play 9", "bar.event 3",
                                              "bridge.event 3", "bar.process", "bridge.process", "bar.update",
                                              "bridge.update first"});

  // A reopen request closes and reopens the same station.
  log.calls.clear();
  stations.request_reopen();
  REQUIRE(loop.frame(5));
  CHECK(log.calls == std::vector<std::string>{"bar.close", "bridge.close", "bridge.open", "bar.process",
                                              "bridge.process", "bar.update", "bridge.update first"});

  // Back inside: the ambient sound stops.
  log.calls.clear();
  stations.select(StationId::kConn);
  REQUIRE(loop.frame(5));
  CHECK(log.calls == std::vector<std::string>{"bar.close", "bridge.close", "conn.open", "stop 9", "bar.process",
                                              "conn.process", "bar.update", "conn.update first"});

  // While paused the clock stands still.
  const std::int32_t second = mission->sim().clock().second();
  const std::int32_t hundredths = mission->sim().clock().hundredths();
  mission->set_paused(true);
  REQUIRE(loop.frame(5));
  CHECK(mission->sim().clock().second() == second);
  CHECK(mission->sim().clock().hundredths() == hundredths);
  mission->set_paused(false);

  // A game over ends the mission with code 10 + reason; the next frame
  // closes the station and stops.
  mission->trigger_game_over(GameOverReason::kOxygen);
  REQUIRE(loop.frame(5));
  CHECK(game_over.shown == GameOverReason::kOxygen);
  CHECK(mission->end_code() == 12);
  log.calls.clear();
  CHECK_FALSE(loop.frame(5));
  CHECK(log.calls == std::vector<std::string>{"bar.close", "conn.close"});
  CHECK(mission->finish() == 12);
}

}  // namespace sh
