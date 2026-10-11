// Mission saves: MISSn.SAV (header, files), the state lists and their
// round trip (a restored mission continues as the saved one), the career
// saved on patrol.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <deque>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "game/mission.h"
#include "assets/exe_image.h"
#include "game/career_save.h"
#include "game/frontend.h"
#include "game/mission_session.h"
#include "game/stations/cabin.h"
#include "game/shpath.h"
#include "game/mission_save.h"
#include "game/patrol.h"
#include "game/saves_options.h"
#include "game/sim_state.h"
#include "game/scengen.h"
#include "core/byte_reader.h"
#include "game/vehicle_state.h"
#include "game_data.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"
#include "sim/ship.h"

namespace sh {
namespace {

// Runs `frames` frames of 10 ticks.
void run(Mission& m, int frames) {
  for (int i = 0; i < frames && m.end_code() == 0; ++i) {
    m.simulate_frame(10, false);
  }
}

// Returns the bytes of a capture of `m` (which sets the vehicles' ids).
std::vector<std::uint8_t> state_bytes(Mission& m) { return capture_sim_state(m, {}, {}).serialize(); }

// Returns the index of the first differing byte, or -1.
std::ptrdiff_t first_difference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return static_cast<std::ptrdiff_t>(i);
    }
  }
  return a.size() == b.size() ? -1 : static_cast<std::ptrdiff_t>(n);
}

// Returns where two state lists differ first ("" when equal).
std::string state_difference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  ByteReader ra(a, "a");
  ByteReader rb(b, "b");
  const SimStateFile fa = SimStateFile::parse(ra, kSimStateVersion);
  const SimStateFile fb = SimStateFile::parse(rb, kSimStateVersion);
  const auto image = [](const StateImage& x, const StateImage& y) -> std::ptrdiff_t {
    const auto dx = x.bytes(0, x.size());
    const auto dy = y.bytes(0, y.size());
    return first_difference(std::vector<std::uint8_t>(dx.begin(), dx.end()),
                            std::vector<std::uint8_t>(dy.begin(), dy.end()));
  };
  if (const std::ptrdiff_t d = image(fa.player, fb.player); d >= 0) {
    return "player +" + std::to_string(d);
  }
  if (fa.ui != fb.ui) {
    return "ui";
  }
  if (fa.patrol_log.size() != fb.patrol_log.size()) {
    return "patrol log";
  }
  if (const std::ptrdiff_t d = image(*fa.tdc, *fb.tdc); d >= 0) {
    return "tdc +" + std::to_string(d);
  }
  if (const std::ptrdiff_t d = image(*fa.deck_gun, *fb.deck_gun); d >= 0) {
    return "deck gun +" + std::to_string(d);
  }
  if (fa.routes.size() != fb.routes.size()) {
    return "routes";
  }
  for (std::size_t i = 0; i < fa.routes.size(); ++i) {
    if (const std::ptrdiff_t d = image(fa.routes[i], fb.routes[i]); d >= 0) {
      return "route " + std::to_string(i) + " +" + std::to_string(d);
    }
  }
  if (fa.groups.size() != fb.groups.size()) {
    return "groups";
  }
  for (std::size_t i = 0; i < fa.groups.size(); ++i) {
    if (const std::ptrdiff_t d = image(fa.groups[i].image, fb.groups[i].image); d >= 0) {
      return "group " + std::to_string(i) + " +" + std::to_string(d);
    }
  }
  if (fa.vehicles.size() != fb.vehicles.size()) {
    return "vehicle count";
  }
  for (std::size_t i = 0; i < fa.vehicles.size(); ++i) {
    if (const std::ptrdiff_t d = image(fa.vehicles[i].image, fb.vehicles[i].image); d >= 0) {
      return "vehicle " + std::to_string(i) + " class " + std::to_string(static_cast<int>(fa.vehicles[i].cls)) +
             " +" + std::to_string(d);
    }
  }
  if (const std::ptrdiff_t d = first_difference(fa.sim_extension, fb.sim_extension); d >= 0) {
    return "sim extension +" + std::to_string(d);
  }
  return {};
}

// Saves `live`, restores the file into a new mission and checks that it
// captures back to the same state, then that both continue alike for
// `frames` frames (with the time compression `compression`).
void check_round_trip(const Install& install, const GameTables& tables, Mission& live, int frames) {
  const MissionSaveFile saved = capture_mission_save(install, live, 1, "Round trip", {});
  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(saved.serialize());
  REQUIRE(parsed);
  MissionSetup setup;
  setup.random = WatcomRandom(1);
  std::unique_ptr<Mission> restored = restore_mission(install, tables, *parsed, setup);
  REQUIRE(restored->sim().vehicles().size() == live.sim().vehicles().size());
  for (std::size_t i = 0; i < live.sim().vehicles().size(); ++i) {
    CHECK(restored->sim().vehicles()[i]->class_id() == live.sim().vehicles()[i]->class_id());
    CHECK(restored->sim().vehicles()[i]->position() == live.sim().vehicles()[i]->position());
  }
  CHECK(first_difference(state_bytes(live), state_bytes(*restored)) == -1);
  for (int i = 0; i < frames; ++i) {
    live.simulate_frame(10, false);
    restored->simulate_frame(10, false);
    REQUIRE(restored->sim().random().state() == live.sim().random().state());
  }
  CHECK(restored->end_code() == live.end_code());
  CHECK(first_difference(state_bytes(live), state_bytes(*restored)) == -1);
}

TEST_CASE("the MISSn.SAV header keeps its fields at the original offsets", "[save]") {
  MissionSaveHeader h;
  h.number = 3;
  h.name = "Midway";
  h.boat_name = "(SS-212) Gato";
  h.class_name = "GATO";
  h.class_long_name = "GATO";
  h.zone_name = "Central Pacific";
  h.weather = 2;
  h.month = 5;
  h.day = 4;
  h.year = 12;
  h.hour = 13;
  h.minute = 37;
  h.second = 12;
  h.latitude = {28, 12, 'N'};
  h.longitude = {177, 22, 'W'};
  h.heading = 2705;
  h.depth_ft = 64;
  h.speed = 0x48000;
  h.seed = 0x12345678;
  // Realism bits 2-3 = 2, bit 7, bit 13, the SJ (16-17) = 2, the SD (18).
  h.realism[0] = 2;
  h.realism[3] = 1;
  h.realism[9] = 1;
  h.realism[12] = 2;
  h.realism[13] = 1;
  const std::array<std::uint8_t, kMissionHeaderSize> bytes = h.serialize();
  CHECK(bytes[0x004] == 'M');
  CHECK(bytes[0x022] == '(');
  CHECK(read_u32le(bytes, 0x81f) == 5);
  CHECK(read_u32le(bytes, 0x827) == 12);
  CHECK(read_u32le(bytes, 0x847) == 28);
  CHECK(bytes[0x84f] == 'N');
  CHECK(read_u32le(bytes, 0x854) == 177);
  CHECK(bytes[0x85c] == 'W');
  CHECK(read_u32le(bytes, 0x861) == 2705);
  CHECK(read_u32le(bytes, 0x86d) == 0x12345678);
  CHECK(read_u32le(bytes, 0x871) == 2);
  CHECK(read_u32le(bytes, 0x8a1) == 2);
  const MissionSaveHeader back = MissionSaveHeader::parse(bytes);
  CHECK(back.serialize() == bytes);
  CHECK(back.boat_name == "(SS-212) Gato");
  // continue_mission_loop's options word: the realism and equipment bits
  // and the saved-mission mode.
  CHECK(back.options() == (0x8u | 0x80u | 0x2000u | 0x20000u | 0x40000u | option::kModeSavedMission));
  // The SD to bit 0, the SJ-1 (value 2) to bit 2.
  CHECK(back.equipment() == 0x5u);
}

TEST_CASE("a restored historical mission continues as the saved one", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.options = RealismFile::kDefaultBits;
  setup.random = WatcomRandom(7);
  std::unique_ptr<Mission> live = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
  run(*live, 300);
  REQUIRE(live->end_code() == 0);

  const MissionSaveFile saved = capture_mission_save(install, *live, 0, "Test", {});
  CHECK(saved.header.boat_name == live->player_name());
  CHECK(saved.header.world_file(3) == live->scenario().header.world);
  CHECK(saved.header.world_file(1) == live->environment_file());
  CHECK(saved.state.vehicles.size() + 1 == live->sim().vehicles().size());
  const std::vector<std::uint8_t> file = saved.serialize();
  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(file);
  REQUIRE(parsed);
  CHECK(parsed->serialize() == file);

  MissionSetup restore_setup;
  restore_setup.random = WatcomRandom(1);
  std::unique_ptr<Mission> restored = restore_mission(install, tables, *parsed, restore_setup);
  CHECK(restored->sim().vehicles().size() == live->sim().vehicles().size());
  CHECK((restored->sim().conditions().options & option::kModeSavedMission) != 0);

  // The restored state saves back to the same lists.
  const std::vector<std::uint8_t> a = state_bytes(*live);
  const std::vector<std::uint8_t> b = state_bytes(*restored);
  CHECK(first_difference(a, b) == -1);

  // And both continue the same.
  run(*live, 600);
  run(*restored, 600);
  CHECK(restored->sim().random().state() == live->sim().random().state());
  CHECK(first_difference(state_bytes(*live), state_bytes(*restored)) == -1);
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

TEST_CASE("a restored encounter continues as the saved one with weapons in flight", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  GeneratorParams params;
  params.day = 15;
  params.month = 5;  // June 1943
  params.year = 13;
  params.time = 1;
  params.escort = 3;
  params.crew = 2;
  params.air_cover = 2;
  const std::vector<const SubClass*> subs = sub_classes_in_service(tables, params.month, params.year);
  REQUIRE_FALSE(subs.empty());
  params.sub_class = subs.back()->index_name;
  WatcomRandom random(4321);
  const MissionStartDraws draws = MissionStartDraws::draw(random);
  GeneratedMission generated =
      generate_single_mission(install, tables, MissionType::kConvoy, params, RealismFile::kDefaultBits, random);
  MissionSetup setup;
  setup.options = RealismFile::kDefaultBits;
  setup.random = random;
  std::unique_ptr<Mission> live = Mission::create(install, tables, std::move(generated), draws, setup);

  // Close to the first escort, surfaced: the escorts open fire.
  const std::vector<Ship*> ships = live->ships();
  REQUIRE_FALSE(ships.empty());
  Ship* escort = ships.front();
  for (Ship* s : ships) {
    if (s->is_escort()) {
      escort = s;
      break;
    }
  }
  const fx::Vec3 e = escort->position();
  live->place_player_at(e.x + 1500 * kUnitsPerYard, e.z, Angle10(2700));
  // A torpedo at it.
  live->player_combat().tdc.set_target(escort);
  SimContext ctx = live->sim().context();
  live->player_combat().fire_tube(ctx, live->player(), 0);

  // Until shells or charges fly.
  bool weapons = false;
  for (int i = 0; i < 3000 && !weapons && live->end_code() == 0; ++i) {
    live->simulate_frame(10, false);
    for (const auto& v : live->sim().vehicles()) {
      weapons = weapons || v->class_id() == VehicleClass::kShell || v->class_id() == VehicleClass::kDepthCharge;
    }
  }
  REQUIRE(live->end_code() == 0);
  CHECK(weapons);
  check_round_trip(install, tables, *live, 1500);
}

TEST_CASE("a restored historical mission continues at a high time compression", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.options = RealismFile::kDefaultBits;
  setup.random = WatcomRandom(11);
  std::unique_ptr<Mission> live = Mission::load(install, tables, "SCEN/HIST05.SCN", setup);
  for (int i = 0; i < 6; ++i) {
    live->sim().clock().double_compression();
  }
  run(*live, 500);
  REQUIRE(live->end_code() == 0);
  check_round_trip(install, tables, *live, 2000);
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

// A career in port in June 1943 with a Balao (as career_patrol_test).
Career patrol_career() {
  Career c;
  CareerRecord& r = c.record;
  r.name = "Tester";
  r.month = 5;
  r.day = 1;
  r.year = 13;
  r.status = CareerStatus::kInPort;
  r.boat = "(SS-337) Carbonero";
  r.class_name = "BALAO";
  r.rank = 1;
  r.equipment = 0x6f;
  r.home_port = "Darwin";
  return c;
}

TEST_CASE("a career saved on patrol resumes the saved patrol", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  Career career = patrol_career();
  WatcomRandom random(99);
  Patrol patrol(install, tables, career, build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random));
  MissionSetup setup;
  setup.options = option::kModeCareer | RealismFile::kDefaultBits;
  setup.random = random;
  std::unique_ptr<Mission> live = patrol.create_mission(std::move(setup));
  for (int i = 0; i < 6; ++i) {
    live->sim().clock().double_compression();
  }
  run(*live, 1500);
  REQUIRE(live->end_code() == 0);
  REQUIRE(patrol.groups() != nullptr);
  REQUIRE_FALSE(patrol.groups()->groups().empty());

  // SAVE CAREER: the roster with the state lists, read back.
  capture_patrol_state(*live, patrol, {});
  REQUIRE(career.record.on_patrol != 0);
  const RosterFile roster = parse_roster(serialize_roster(career));
  CHECK(roster.career.patrol_state == career.patrol_state);
  Career resumed = roster.career;

  // career_base_loop on patrol: the patrol's scenario of the same zone,
  // then the mission, which restores the saved one.
  WatcomRandom other(5);
  Patrol patrol2(install, tables, resumed, build_patrol_scenario(resumed, zones.at(9), 9, 20000, 100, tables, other));
  MissionSetup setup2;
  setup2.options = option::kModeCareer | RealismFile::kDefaultBits;
  setup2.random = other;
  std::unique_ptr<Mission> restored = patrol2.create_mission(std::move(setup2));
  REQUIRE(patrol2.resumed_state() != nullptr);
  REQUIRE(restored->sim().vehicles().size() == live->sim().vehicles().size());
  CHECK(patrol2.groups()->groups().size() == patrol.groups()->groups().size());
  CHECK(restored->environment_file() == live->environment_file());

  {
    const MissionConditions& a = live->sim().conditions();
    const MissionConditions& b = restored->sim().conditions();
    CHECK(a.sea_state == b.sea_state);
    CHECK(a.options == b.options);
    CHECK(a.weather == b.weather);
    CHECK(a.fog == b.fog);
    CHECK(a.thermal_layer_ft == b.thermal_layer_ft);
    CHECK(a.scenario_roll == b.scenario_roll);
    CHECK(live->sim().clock().minute() == restored->sim().clock().minute());
    CHECK(live->sim().clock().hundredths() == restored->sim().clock().hundredths());
    CHECK(live->sim().clock().effective_compression() == restored->sim().clock().effective_compression());
  }
  const auto bytes = [](Mission& m, Patrol& p) { return capture_sim_state(m, {.patrol = &p}, {}).serialize(); };
  CHECK(state_difference(bytes(*live, patrol), bytes(*restored, patrol2)) == "");
  for (int i = 0; i < 1500; ++i) {
    live->simulate_frame(10, false);
    restored->simulate_frame(10, false);
    REQUIRE(restored->sim().random().state() == live->sim().random().state());
  }
  CHECK(state_difference(bytes(*live, patrol), bytes(*restored, patrol2)) == "");
  CHECK(resumed.record.day == career.record.day);
  CHECK(resumed.record.minute == career.record.minute);
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

TEST_CASE("a save without the extension restores what the original restores", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.options = RealismFile::kDefaultBits;
  setup.random = WatcomRandom(3);
  std::unique_ptr<Mission> live = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
  run(*live, 700);
  MissionSaveFile saved = capture_mission_save(install, *live, 4, "Original", {});
  saved.state.sim_extension.clear();
  saved.state.ui_extension.clear();
  const std::vector<std::uint8_t> file = saved.serialize();
  // The original's reader stops after the vehicles: nothing follows.
  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(file);
  REQUIRE(parsed);
  CHECK(parsed->state.sim_extension.empty());
  CHECK(parsed->serialize() == file);

  MissionSetup restore_setup;
  restore_setup.random = WatcomRandom(1);
  std::unique_ptr<Mission> restored = restore_mission(install, tables, *parsed, restore_setup);
  const auto& a = live->sim().vehicles();
  const auto& b = restored->sim().vehicles();
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) {
    CHECK(b[i]->class_id() == a[i]->class_id());
    CHECK(b[i]->position() == a[i]->position());
    CHECK(b[i]->heading() == a[i]->heading());
  }
  // init_restore_saved_mission: srand(the header's seed), the engines set
  // up again; clock_init at the saved second.
  CHECK(restored->sim().random().state() == saved.header.seed);
  const GameClock& c = restored->sim().clock();
  CHECK(c.hour() == live->sim().clock().hour());
  CHECK(c.minute() == live->sim().clock().minute());
  CHECK(c.second() == live->sim().clock().second());
  CHECK(restored->fog_pct() == live->fog_pct());
  CHECK(restored->player().fuel() == live->player().fuel());
  CHECK(restored->player().telegraph() == live->player().telegraph());
  CHECK(restored->player_combat().tubes.content(0) == live->player_combat().tubes.content(0));
  run(*restored, 300);
  CHECK(restored->end_code() == 0);
}

TEST_CASE("the save directory lists, numbers and deletes the mission saves", "[save]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_mission_save_test";
  std::filesystem::remove_all(dir);
  const SaveDir saves(dir);
  CHECK(find_free_mission_slot(saves) == 0);
  for (std::int32_t n = 0; n < 3; ++n) {
    MissionSaveFile f;
    f.header.number = n;
    f.header.name = "Save " + std::to_string(n);
    REQUIRE(save_mission(saves, f));
  }
  // Not a mission save, and another version.
  REQUIRE(saves.write("MISSX.SAV", std::vector<std::uint8_t>(0x920, 7)));
  REQUIRE(saves.write("MISS9.SAV", std::vector<std::uint8_t>(0x920, 6)));
  std::vector<MissionSaveHeader> list = list_mission_saves(saves);
  REQUIRE(list.size() == 3);
  CHECK(list[2].name == "Save 2");
  CHECK(find_free_mission_slot(saves) == 3);
  // mission_delete_save renumbers the saves above.
  delete_mission_save(saves, 0);
  list = list_mission_saves(saves);
  REQUIRE(list.size() == 2);
  CHECK(list[0].number == 0);
  CHECK(list[0].name == "Save 1");
  CHECK(list[1].number == 1);
  CHECK(list[1].name == "Save 2");
  CHECK(load_mission_save(saves, 1)->header.name == "Save 2");
  CHECK_FALSE(load_mission_save(saves, 2));
  std::filesystem::remove_all(dir);
}

TEST_CASE("the original install has no mission saved in play", "[save][content]") {
  SH_REQUIRE_GAME_DATA(install);
  // SAVE holds no MISSn.SAV, and ROSTER0.SAV was saved in port: no state
  // lists follow its log.
  const SaveDir saves(std::filesystem::temp_directory_path() / "sh_mission_save_none", &install, "SAVE\\");
  CHECK(list_mission_saves(saves).empty());
  const std::optional<RosterFile> roster = load_roster(saves, 0);
  REQUIRE(roster);
  CHECK(roster->career.record.on_patrol == 0);
  CHECK(roster->career.patrol_state.empty());
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

// A host replaying clicks and keys (one every 40 frames) and recording the
// missions it is asked to run (as frontend_test.cpp's).
class ScriptedHost final : public FrontendHost, public MissionRunner {
 public:
  struct Step {
    std::uint16_t key = 0;
    int x = 0;
    int y = 0;
  };
  std::deque<Step> script;
  std::vector<MissionRequest> requests;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8&, const VgaPalette&) override {
    tick_ += 2;
    if (++frames_ > 50'000) {
      throw std::runtime_error("the script did not end the front end");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 40 || script.empty()) {
      return;
    }
    idle_ = 0;
    const Step s = script.front();
    script.pop_front();
    if (s.key != 0) {
      gui.mouse().post_key(s.key, 0);
    } else {
      gui.mouse().update(s.x, s.y, ui::kButtonLeft, 0);
      gui.mouse().update(s.x, s.y, 0, 0);
    }
  }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view) override {}
  std::int32_t run(const MissionRequest& request) override {
    requests.push_back(request);
    return 1;  // aborted
  }
  MissionOutcome outcome() const override { return {}; }

 private:
  std::uint32_t tick_ = 0;
  int frames_ = 0;
  int idle_ = 0;
};

constexpr ScriptedHost::Step kSingleMission{0, 20 + 35 + 90, 43 + 130 + 17};
constexpr ScriptedHost::Step kExitToDos{0, 20 + 35 + 90, 43 + 330 + 17};
constexpr ScriptedHost::Step kMainMenu{0, 69 + 38 + 90, 59 + 299 + 17};
constexpr ScriptedHost::Step kContinue{0, 69 + 261 + 90, 59 + 205 + 17};
// The CONTINUE MISSION dialog (0x45, 0x13): DELETE and BEGIN.
constexpr ScriptedHost::Step kDelete{0, 0x45 + 0xb9 + 60, 0x13 + 0x18b + 17};
constexpr ScriptedHost::Step kBeginSave{0, 0x45 + 0x14f + 60, 0x13 + 0x18b + 17};
constexpr ScriptedHost::Step kYes{0x1579, 0, 0};
constexpr ScriptedHost::Step kEnter{0x1c0d, 0, 0};

TEST_CASE("CONTINUE MISSION deletes a save and begins another", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_continue_test";
  std::filesystem::remove_all(dir);
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir, &install, paths.dir(GameDir::kSave));
  for (std::int32_t n = 0; n < 2; ++n) {
    MissionSaveFile f;
    f.header.number = n;
    f.header.name = n == 0 ? "First" : "Second";
    f.header.realism[0] = 2;  // bits 2-3
    f.header.realism[9] = 1;  // bit 13
    REQUIRE(save_mission(saves, f));
  }
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  // DELETE the first save (YES), then BEGIN the other, renumbered 0.
  host.script = {kSingleMission, kContinue, kDelete, kYes, kBeginSave, kMainMenu, kExitToDos};
  front_end.run();

  CHECK(host.script.empty());
  REQUIRE(host.requests.size() == 1);
  const MissionRequest& r = host.requests.front();
  CHECK(r.saved_mission == 0);
  // The save's realism in saved-mission mode.
  CHECK((r.options & 0x3u) == option::kModeSavedMission);
  CHECK((r.options & 0xcu) == 0x8u);
  CHECK((r.options & 0x2000u) != 0);
  const std::vector<MissionSaveHeader> left = list_mission_saves(saves);
  REQUIRE(left.size() == 1);
  CHECK(left[0].name == "Second");
  std::filesystem::remove_all(dir);
}

TEST_CASE("CONTINUE MISSION without saves tells so", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_continue_none_test";
  std::filesystem::remove_all(dir);
  const ShPaths paths = ShPaths::load(install);
  const ExeImage exe = ExeImage::load(install);
  const SaveDir saves(dir, &install, paths.dir(GameDir::kSave));
  ScriptedHost host;
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves}, host, host, 1234);
  front_end.set_play_intro(false);
  host.script = {kSingleMission, kContinue, kEnter, kMainMenu, kExitToDos};
  front_end.run();
  CHECK(host.script.empty());
  CHECK(host.requests.empty());
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

// A mission session over HIST01 or a restored save, with its GUI, an
// injected clock and the save directory (as integration_test.cpp's rig).
struct SaveRig {
  SaveRig(const Install& game, const std::filesystem::path& dir)
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        tables(GameTables::load(game)),
        resources(ui::GuiResources::load(game)),
        saves(dir, &game, paths.dir(GameDir::kSave)) {}

  void start(std::unique_ptr<Mission> mission, const SimStateFile* restored = nullptr) {
    session = std::make_unique<MissionSession>(
        MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail, false, nullptr},
        std::move(mission), saves);
    if (restored != nullptr) {
      restore_session_state(*restored, {.screen = &session->screen(), .attack = &session->services()});
    }
    session->screen().bar().on_save = [this] { cmd_save_mission(*session, install, saves, slot); };
  }
  void frames(int n) {
    for (int i = 0; i < n; ++i) {
      tick += 5;
      session->screen().frame(5);
    }
  }
  void key(std::uint16_t word) {
    gui.mouse().post_key(word, 0);
    frames(4);
  }
  // Types `keys` from the GUI pump, as the text entries' editing loops
  // read the keyboard (the game's pump presents the screen and polls).
  void type(std::initializer_list<std::uint16_t> keys) {
    pending.insert(pending.end(), keys);
    gui.set_pump([this] {
      ++tick;
      if (!pending.empty()) {
        gui.mouse().post_key(pending.front(), 0);
        pending.pop_front();
      }
    });
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
  SaveDir saves;
  GameOptions options;
  DetailOptions detail;
  SavedMissionSlot slot;
  std::deque<std::uint16_t> pending;
  std::unique_ptr<MissionSession> session;
};

TEST_CASE("SAVE MISSION in play writes MISS0.SAV, then overwrites it", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_save_command_test";
  std::filesystem::remove_all(dir);
  SaveRig rig(install, dir);
  rig.start(Mission::load(install, rig.tables, "SCEN/HIST01.SCN", {}));
  rig.frames(100);
  // The status station, then SAVE MISSION: the name dialog, "Test", Enter.
  rig.session->screen().stations().select(StationId::kStatus);
  rig.frames(10);
  rig.type({0x1454, 0x1265, 0x1f73, 0x1474, 0x1c0d});
  rig.session->screen().bar().on_save();
  rig.frames(20);
  std::optional<MissionSaveFile> saved = load_mission_save(rig.saves, 0);
  REQUIRE(saved);
  CHECK(saved->header.name == "Test");
  CHECK(rig.slot.number == 0);
  CHECK((rig.session->screen().mission().sim().conditions().options & option::kModeSavedMission) != 0);
  REQUIRE(saved->state.ui);
  CHECK(saved->state.ui->station == static_cast<std::int32_t>(StationId::kStatus));

  // Again: "Overwrite currently saved mission?", YES.
  rig.frames(50);
  rig.session->screen().bar().on_save();
  rig.frames(10);
  rig.key(0x1579);
  CHECK(list_mission_saves(rig.saves).size() == 1);
  saved = load_mission_save(rig.saves, 0);
  REQUIRE(saved);
  CHECK(saved->header.name == "Test");

  // Continued: the mission restored on a new screen, at the status station.
  SaveRig rig2(install, dir);
  MissionSetup setup;
  rig2.start(restore_mission(install, rig2.tables, *saved, setup), &saved->state);
  CHECK(rig2.session->screen().stations().current() == StationId::kStatus);
  CHECK(rig2.session->screen().mission().sim().vehicles().size() ==
        rig.session->screen().mission().sim().vehicles().size());
  rig2.frames(50);
  CHECK(rig2.session->screen().mission().end_code() == 0);
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

TEST_CASE("SAVE CAREER on patrol writes a roster that resumes the patrol", "[save]") {
  SH_REQUIRE_INSTALL(install);
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_save_career_test";
  std::filesystem::remove_all(dir);
  SaveRig rig(install, dir);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  Career career = patrol_career();
  career.record.roster_slot = 0;
  WatcomRandom random(17);
  Patrol patrol(install, rig.tables, career,
                build_patrol_scenario(career, zones.at(9), 9, 20000, 100, rig.tables, random));
  MissionSetup setup;
  setup.options = option::kModeCareer | RealismFile::kDefaultBits;
  setup.random = random;
  rig.start(patrol.create_mission(std::move(setup)));
  rig.session->screen().bar().on_save = [&] { cmd_save_career(*rig.session, patrol, rig.saves); };
  rig.frames(200);
  // A ship in the patrol log.
  auto* cabin = dynamic_cast<CabinStation*>(rig.session->screen().stations().get(StationId::kCabin));
  REQUIRE(cabin != nullptr);
  PatrolLogEntry sunk;
  sunk.type = 3;
  sunk.name = "NACHI";
  sunk.tonnage = 13000;
  sunk.latitude = {5, 30, 'N'};
  cabin->logs().patrol.push_back(sunk);
  // BACKUP CAREER: "Bak", Enter.
  rig.type({0x3042, 0x1e61, 0x256b, 0x1c0d});
  rig.session->screen().bar().on_save();
  rig.frames(20);
  // The install's ROSTER0.SAV and the backup.
  const std::vector<RosterEntry> rosters = list_rosters(rig.saves);
  const auto backup = std::ranges::find_if(rosters, [](const RosterEntry& e) { return e.record.name == "Bak"; });
  REQUIRE(backup != rosters.end());
  CHECK(backup->record.on_patrol != 0);
  const std::optional<RosterFile> roster = load_roster(rig.saves, backup->record.roster_slot);
  REQUIRE(roster);
  REQUIRE_FALSE(roster->career.patrol_state.empty());
  ByteReader r(roster->career.patrol_state, "patrol state");
  const SimStateFile state = SimStateFile::parse(r, kSimStateVersion);
  CHECK(state.vehicles.size() + 1 == rig.session->screen().mission().sim().vehicles().size());
  CHECK(state.groups.size() == patrol.groups()->groups().size());
  CHECK_FALSE(state.sim_extension.empty());
  CHECK(roster->career.record.patrol_log_count == 1);
  // VIEW CAREER shows the patrol log as sunk ships.
  const CareerView view = career_view(rig.saves, backup->record);
  REQUIRE(view.events.size() == 1);
  CHECK(view.events[0].log.name == "NACHI");
  CHECK(view.events[0].log.latitude.minutes == 30);
  CHECK(view.events[0].status == SunkStatus::kSunk);
  CHECK(view.record.tonnage == backup->record.tonnage + 13000);
  CHECK(view.record.ships_sunk == backup->record.ships_sunk + 1);
  std::filesystem::remove_all(dir);
}

}  // namespace
}  // namespace sh
