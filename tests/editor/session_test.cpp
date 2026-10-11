// Whole editor sessions driven through the GUI like a player.
// One builds a scenario with every kind of object (the player, a convoy
// with its leader, a member and an escort, an enemy sub on a course,
// aircraft on patrol, escort and path missions, an airstrike, a photo
// recon) and every setting (environment, equipment, torpedoes, briefing,
// password), saves it, loads it back, runs it and plays it in the game for
// a few minutes. The other loads every historical scenario through Load
// Historical, saves it as a custom scenario and compares the bytes.
// SH_SCREENSHOT_DIR receives the screens along the way.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/scenario.h"
#include "assets/scn_file.h"
#include "core/fixed_math.h"
#include "editor/class_sheets.h"
#include "editor/file_commands.h"
#include "editor_rig.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game_data.h"
#include "password_key.h"
#include "sim/aircraft.h"
#include "sim/enemy_sub.h"
#include "sim/ship.h"

namespace sh::editor {
namespace {

using test::EditorRig;
using test::kEditX;
using test::kFilesX;
using test::kKeyYes;

using Point = std::array<int, 2>;

// Toolbox controls (dialog coordinates).
constexpr Point kAddShip = {82 + 20, 71 + 10};
constexpr Point kAddAircraft = {127 + 20, 71 + 10};
constexpr Point kAirstrike = {172 + 20, 71 + 10};
constexpr Point kPhotoRecon = {217 + 20, 71 + 10};
constexpr Point kZoomIn = {14 + 10, 44 + 10};
constexpr Point kOrders = {108 + 50, 173 + 10};
constexpr Point kDialEast = {60 + 30, 197 + 1};
constexpr Point kEngine = {108 + 50, 173 + 10};
// A ship's or enemy sub's speed digits.
constexpr Point kSpeedTens = {125 + 8, 239 + 10};
constexpr Point kSpeedUnits = {143 + 8, 239 + 10};
// PLOT COURSE's DONE and SELECT A CITY's SELECT.
constexpr Point kDone = {215 + 32, 45 + 10};
constexpr Point kCitySelect = {110 + 40, 45 + 10};

// The selection dialogs (screen): the first list row and SELECT.
constexpr Point kShipRow0 = {137 + 15 + 100, 10 + 292 + 9};
constexpr Point kShipSelect = {137 + 252 + 50, 10 + 400 + 12};
constexpr Point kAirRow0 = {137 + 15 + 100, 47 + 235 + 9};
constexpr Point kAirSelect = {137 + 252 + 50, 47 + 340 + 12};
// Edit dialogs (screen).
constexpr Point kEnvDayUnits = {126 + 256 + 8, 1 + 57 + 10};
constexpr Point kEnvWeather = {126 + 185 + 76, 1 + 143 + 10};
constexpr Point kEnvOk = {126 + 255 + 50, 1 + 417 + 12};
constexpr Point kEquipOk = {157 + 200 + 50, 109 + 219 + 12};
constexpr Point kTorpBowTube0 = {100 + 228 + 84, 80 + 74 + 10};
constexpr Point kTorpOk = {100 + 302 + 50, 80 + 311 + 12};
constexpr Point kBriefingOk = {53 + 322 + 50, 80 + 265 + 12};
constexpr Point kPasswordAccept = {163 + 177 + 50, 166 + 110 + 12};
// The Load / Delete dialog's first row and its LOAD.
constexpr Point kListRow0 = {134 + 14 + 60, 50 + 54 + 1 + 8};
constexpr Point kListLoad = {134 + 234 + 50, 50 + 325 + 12};

// Menu items of Files when started by the game.
constexpr int kFileRun = 0;
constexpr int kFileNew = 1;
constexpr int kFileLoadHistorical = 2;
constexpr int kFileLoadScenario = 3;
constexpr int kFileSave = 4;
constexpr int kFileExit = 6;
// Edit's items.
constexpr int kEditPassword = 0;
constexpr int kEditBriefing = 1;
constexpr int kEditEnvironment = 3;
constexpr int kEditTorpedoes = 4;
constexpr int kEditEquipment = 5;

// A session script with the helpers of the editor's screens.
struct Session {
  explicit Session(const Install& game) : install(game), rig(game, true) { rig.host.frame_limit = 200'000; }

  Editor& editor() { return rig.editor; }
  Scenario& scenario() { return rig.editor.scenario(); }

  // Clicks at a fixed screen point.
  void click(Point p) { rig.click(p[0], p[1]); }
  void click_panel(Point p) { rig.click_panel(p[0], p[1]); }
  // Clicks the map at `base` + (dx, dy), `base` being found when the script
  // runs.
  void click_map(int dx, int dy) {
    rig.click_at([this, dx, dy] { return Point{base[0] + dx, base[1] + dy}; });
  }
  // Clicks where the object `which()` is on the map.
  void click_object(std::function<EditorObject*()> which) {
    rig.click_at([this, which = std::move(which)] {
      EditorObject* o = which();
      REQUIRE(o != nullptr);
      const auto s = editor().chart().world_to_screen(o->position(scenario().object_env()));
      REQUIRE(s);
      return Point{(*s)[0], (*s)[1]};
    });
  }
  // Moves the selection of the focused list or button grid down `n` rows,
  // then Enter.
  void down_enter(int n) {
    for (int i = 0; i < n; ++i) {
      rig.key(ui::kKeyDown);
    }
    rig.key(ui::kKeyEnter);
  }
  // ADD SHIP with the SHIPS.DAT class chosen by `pick` in SELECT SHIP TYPE
  // (the first row clicked, then Down to it), placed at `base` + (dx, dy).
  void add_vessel(std::function<bool(const ShipClass&)> pick, int dx, int dy) {
    click_panel(kAddShip);
    click(kShipRow0);
    auto rows = std::make_shared<int>(0);
    rig.then([this, rows, pick = std::move(pick)] {
      const Scenario& s = scenario();
      const ClassList list = ship_type_list(s.data(), editor().options(), editor().ui().resources().small,
                                            s.header.month, s.header.year, -1);
      const auto it = std::ranges::find_if(list.entries, [&](std::size_t c) { return pick(s.data().tables.ships[c]); });
      REQUIRE(it != list.entries.end());
      *rows = static_cast<int>(it - list.entries.begin());
      for (int i = 0; i < *rows; ++i) {
        rig.host.script.push_front([](ui::Gui& gui) { gui.mouse().post_key(ui::kKeyDown, 0); });
      }
    });
    click(kShipSelect);
    click_map(dx, dy);
  }
  // ADD AIRCRAFT: the first type, then the mission `mission` of Patrol /
  // Escort / Follow Path.
  void add_aircraft(int mission) {
    click_panel(kAddAircraft);
    click(kAirRow0);
    click(kAirSelect);
    down_enter(mission);
  }
  // Returns the last object of `type` in the list.
  EditorObject* last_of(ObjectType type) {
    EditorObject* found = nullptr;
    for (const auto& o : scenario().objects()) {
      if (o->type() == type) {
        found = o.get();
      }
    }
    return found;
  }
  // Returns the first vessel with `orders`.
  Vessel* vessel_with(ShipOrders orders) {
    for (const auto& o : scenario().objects()) {
      if (auto* v = dynamic_cast<Vessel*>(o.get()); v != nullptr && v->ship.orders == orders) {
        return v;
      }
    }
    return nullptr;
  }

  // Finds `base`: a view pixel right of the panels whose surroundings
  // (-70..+110, -50..+70) are all deeper than 300 ft.
  void find_base() {
    const ChartScreen& chart = editor().chart();
    const auto deep = [&](int x, int y) {
      const WorldPoint w = chart.screen_to_world(x, y);
      return -scenario().world().floor().height_at(w.x, w.y) / 16 > 300;
    };
    for (int y = 80; y < 380; y += 4) {
      for (int x = 400; x < 520; x += 4) {
        bool ok = true;
        for (int dy = -50; dy <= 70 && ok; dy += 5) {
          for (int dx = -70; dx <= 110 && ok; dx += 5) {
            ok = deep(x + dx, y + dy);
          }
        }
        if (ok) {
          base = {x, y};
          return;
        }
      }
    }
    FAIL("no open water on the start screen");
  }

  const Install& install;
  EditorRig rig;
  Point base{};
};

// World units run in a second at one knot.
constexpr double kUnitsPerKnotSecond = 27.008;

double distance(fx::Vec3 a, fx::Vec3 b) { return static_cast<double>(fx::approx_distance(b.x - a.x, b.z - a.z)); }

bool is_merchant(const ShipClass& c) { return c.role == ShipRole::kMerchant && c.ship_type != ShipType::kSubmarine; }
bool is_escort(const ShipClass& c) { return c.role == ShipRole::kEscort && c.ship_type != ShipType::kSubmarine; }
bool is_submarine(const ShipClass& c) { return c.ship_type == ShipType::kSubmarine; }

}  // namespace

TEST_CASE("A scenario with every object built, saved, loaded, run and played", "[editor][session]") {
  SH_REQUIRE_INSTALL(install);
  Session s(install);
  EditorRig& rig = s.rig;
  rig.screenshot("qa_start");

  // The settings: 8 Dec 1941, partly cloudy; the bow tube 1 changed; a
  // briefing and a password.
  rig.menu(kEditX, kEditEnvironment);
  s.click(kEnvDayUnits);
  s.click(kEnvWeather);
  rig.screenshot("qa_environment");
  s.click(kEnvOk);
  rig.menu(kEditX, kEditEquipment);
  s.click(kEquipOk);
  rig.menu(kEditX, kEditTorpedoes);
  s.click(kTorpBowTube0);
  rig.screenshot("qa_torpedoes");
  s.click(kTorpOk);
  rig.menu(kEditX, kEditBriefing);
  rig.type("QA patrol off Shikoku");
  s.click(kBriefingOk);
  rig.menu(kEditX, kEditPassword);
  rig.type("Qa");
  rig.key(ui::kKeyEnter);
  s.click(kPasswordAccept);
  rig.key(0x1579);  // y
  rig.then([&] {
    const ScnHeader& h = s.scenario().header;
    CHECK(h.day == 8);
    CHECK(h.weather == Weather::kPartlyCloudy);
    CHECK(scn_text(h.briefing) == "QA patrol off Shikoku");
    CHECK(h.password == encode_scn_password("Qa", sh::test::se_password_key(install)));
    CHECK(s.scenario().modified);
  });

  // The player: heading 90, engine ahead.
  rig.then([&] { s.find_base(); });
  s.click_panel(kDialEast);
  s.click_panel(kEngine);
  rig.then([&] {
    CHECK(s.scenario().player().rec.heading == 90);
    CHECK(s.scenario().player().rec.engine_order == 1);
  });

  // A convoy: a merchant leading it on a course, another one following, an
  // escort on a looped course.
  s.add_vessel(is_merchant, 0, 0);
  s.click_panel(kOrders);
  rig.screenshot("qa_orders");
  s.down_enter(1);  // Convoy Leader
  s.click_map(60, 40);
  s.click_map(100, 60);
  rig.screenshot("qa_plot_course");
  s.click_panel(kDone);
  s.click_panel(kSpeedTens);  // 10 knots
  s.add_vessel(is_merchant, 20, 20);
  s.click_panel(kOrders);
  s.down_enter(2);  // Convoy Member
  rig.screenshot("qa_pick_convoy");
  s.click_object([&] { return s.vessel_with(ShipOrders::kConvoyLeader); });
  s.add_vessel(is_escort, -20, 20);
  s.click_panel(kOrders);
  s.down_enter(1);  // Escort
  s.click_map(-40, 0);
  s.click_map(-40, 50);
  s.click_map(10, 50);
  s.click_panel(kDone);
  s.click_object([&] { return s.vessel_with(ShipOrders::kConvoyLeader); });
  rig.then([&] {
    Vessel* leader = s.vessel_with(ShipOrders::kConvoyLeader);
    Vessel* member = s.vessel_with(ShipOrders::kConvoyMember);
    Vessel* escort = s.vessel_with(ShipOrders::kEscort);
    REQUIRE(leader != nullptr);
    REQUIRE(member != nullptr);
    REQUIRE(escort != nullptr);
    CHECK(leader->ship.convoy_group == 1);
    CHECK(leader->ship.speed == 10);
    CHECK(member->ship.speed == 10);
    CHECK(escort->ship.speed > 10);
    CHECK(leader->ship.waypoints.count == 3);
    CHECK(leader->ship.waypoints.x[3] == 0);  // open
    CHECK(member->ship.convoy_group == 1);
    CHECK(member->ship.heading == leader->ship.heading);
    CHECK(escort->ship.convoy_group == 1);
    CHECK(escort->ship.waypoints.count == 4);  // the start and 3 points
    CHECK(escort->ship.waypoints.x[4] == -1);  // looped
    CHECK(s.editor().chart().panel() == EditorPanel::kToolbox);
  });
  rig.screenshot("qa_toolbox_ship");

  // An enemy sub on patrol along an open course.
  s.add_vessel(is_submarine, 60, -30);
  rig.screenshot("qa_toolbox_enemy_sub");
  s.click_panel(kOrders);
  s.down_enter(1);  // Patrol
  s.click_map(100, -40);
  s.click_map(100, 0);
  s.click_panel(kDone);
  s.click_panel(kSpeedUnits);
  s.click_panel(kSpeedUnits);  // 2 knots
  rig.then([&] {
    auto* sub = dynamic_cast<Vessel*>(s.last_of(ObjectType::kSubmarine));
    REQUIRE(sub != nullptr);
    CHECK(sub->sub.orders == ShipOrders::kFollowPath);
    CHECK(sub->sub.waypoints.count == 3);
    CHECK(sub->sub.speed == 2);
  });

  // Aircraft: on patrol, escorting the convoy, along a path.
  s.add_aircraft(0);
  s.click_map(40, 60);
  rig.screenshot("qa_select_radius");
  s.click_map(80, 60);
  rig.screenshot("qa_toolbox_aircraft");
  s.add_aircraft(1);
  s.click_map(0, 30);
  s.click_map(30, 30);
  s.click_object([&] { return s.vessel_with(ShipOrders::kConvoyLeader); });
  s.add_aircraft(2);
  s.click_map(-60, -40);
  s.click_map(-20, -40);
  s.click_map(-20, -10);
  s.click_panel(kDone);
  rig.then([&] {
    std::vector<const Aircraft*> air;
    for (const auto& o : s.scenario().objects()) {
      if (const auto* a = dynamic_cast<const Aircraft*>(o.get())) {
        air.push_back(a);
      }
    }
    REQUIRE(air.size() == 3);
    CHECK(air[0]->rec.mission == AircraftMission::kSearchOrbit2);
    CHECK(air[0]->rec.orbit_radius > 0);
    CHECK(air[1]->rec.mission == AircraftMission::kSearchOrbit);
    CHECK(air[1]->rec.escort_group == 1);
    CHECK(air[2]->rec.mission == AircraftMission::kFollowPath);
    CHECK(air[2]->rec.waypoints.count == 3);
  });

  // An airstrike and a photo recon on the first cities of their lists.
  s.click_panel(kAirstrike);
  rig.screenshot("qa_select_city");
  s.click_panel(kCitySelect);
  rig.screenshot("qa_toolbox_airstrike");
  s.click_panel(kPhotoRecon);
  s.click_panel(kCitySelect);
  rig.screenshot("qa_toolbox_photo_recon");
  rig.then([&] {
    std::vector<const SpecialMission*> missions;
    for (const auto& o : s.scenario().objects()) {
      if (const auto* m = dynamic_cast<const SpecialMission*>(o.get())) {
        missions.push_back(m);
      }
    }
    REQUIRE(missions.size() == 2);
    CHECK(missions[0]->base() >= 0);
    CHECK(missions[1]->base() >= 0);
    CHECK(s.scenario().objects().size() == 10);
  });
  // The zoom box, applied on the convoy.
  s.click_panel(kZoomIn);  // the box opens (2x) and the panel closes
  rig.key(0x2c5a);         // Z: 4x
  s.click_map(0, 0);
  rig.screenshot("qa_zoom4");
  rig.then([&] { CHECK(s.editor().chart().view().zoom_int() == 4); });

  // Save, New, Load with the password, compare.
  auto saved = std::make_shared<std::vector<std::uint8_t>>();
  rig.menu(kFilesX, kFileSave);
  rig.type("qatest");
  rig.key(ui::kKeyEnter);
  rig.then([&, saved] {
    const std::filesystem::path file = rig.dir.path() / "CUSTOM" / "QATEST.SCN";
    REQUIRE(std::filesystem::exists(file));
    *saved = read_file(file);
    CHECK(*saved == write_scn_file(s.scenario().save()));
    CHECK_FALSE(s.scenario().modified);
  });
  rig.menu(kFilesX, kFileNew);
  rig.then([&] { CHECK(s.scenario().objects().size() == 1); });
  rig.menu(kFilesX, kFileLoadScenario);
  s.click(kListRow0);
  s.click(kListLoad);
  rig.type("QA");
  rig.key(ui::kKeyEnter);
  rig.then([&, saved] {
    CHECK(s.scenario().objects().size() == 10);
    CHECK(write_scn_file(s.scenario().save()) == *saved);
    CHECK(s.editor().files().name == "QATEST");
  });
  rig.screenshot("qa_loaded");
  rig.menu(kFilesX, kFileRun);
  CHECK(s.editor().run() == EditorExit::kRun);
  CHECK(rig.host.script.empty());
  const std::filesystem::path run_file = rig.dir.path() / "CUSTOM" / "AEONSCEN.TMP";
  REQUIRE(std::filesystem::exists(run_file));
  const std::vector<std::uint8_t> bytes = read_file(run_file);
  CHECK(bytes == *saved);

  // The game plays it: every unit is there, and after three minutes the
  // convoy has moved along its course, the sub along its path and the
  // aircraft are flying.
  const GameTables tables = GameTables::load(install);
  sh::Scenario played = parse_scenario(bytes, "CUSTOM/AEONSCEN.TMP");
  const std::unique_ptr<Mission> mission = Mission::create(install, tables, std::move(played), {});
  REQUIRE(mission->ships().size() == 3);
  REQUIRE(mission->enemy_subs().size() == 1);
  CHECK(mission->aircraft().size() == 3);
  CHECK(mission->bomber_groups().size() == 1);
  CHECK(mission->photo_recons().size() == 1);
  std::vector<fx::Vec3> start;
  for (const Ship* ship : mission->ships()) {
    start.push_back(ship->position());
  }
  std::vector<fx::Vec3> air_start;
  for (const sh::Aircraft* a : mission->aircraft()) {
    air_start.push_back(a->position());
  }
  const fx::Vec3 sub_start = mission->enemy_subs()[0]->position();
  HeadlessRunner runner(*mission);
  runner.run(180);
  CHECK(mission->end_code() == 0);
  // The convoy at 10 knots (the escort faster), the sub at 2.
  for (std::size_t i = 0; i < start.size(); ++i) {
    INFO(i);
    const double run = distance(start[i], mission->ships()[i]->position());
    CHECK(run >= 10 * kUnitsPerKnotSecond * 180 * 0.7);
  }
  const double sub_run = distance(sub_start, mission->enemy_subs()[0]->position());
  CHECK(sub_run >= 2 * kUnitsPerKnotSecond * 180 * 0.5);
  CHECK(sub_run <= 2 * kUnitsPerKnotSecond * 180 * 1.5);
  for (std::size_t i = 0; i < air_start.size(); ++i) {
    INFO(i);
    CHECK(distance(air_start[i], mission->aircraft()[i]->position()) > 0);
  }
}

TEST_CASE("Every historical scenario loads and saves as a custom one", "[editor][session][content]") {
  // Load Historical, row N, then Save Scenario as HNN: version 7 files come
  // back byte for byte; older ones as the editor's own load and save of the
  // same files in the same order give them (records regrouped, the reload
  // table rebuilt, required_memory one byte low, the stale header bytes of
  // the previous load). Each saved file then plays in the game.
  SH_REQUIRE_GAME_DATA(install);
  Session s(install);
  EditorRig& rig = s.rig;
  const GameTables tables = GameTables::load(install);
  EditorOptions ref_options;
  auto ref = std::make_shared<std::optional<Scenario>>();
  std::vector<std::string> files;
  for (const std::string& path : install.list("SCEN", ".SCN")) {
    const std::string file = path.substr(path.rfind('/') + 1);
    if (file.starts_with("HIST")) {
      files.push_back(file);
    }
  }
  std::ranges::sort(files);
  REQUIRE(files.size() == 60);
  rig.then([&, ref] { ref->emplace(install, s.scenario().data(), ref_options, [] { return 1000; }); });
  int older = 0;
  for (std::size_t i = 0; i < files.size(); ++i) {
    const std::string name = std::format("H{:02}", i + 1);
    rig.menu(kFilesX, kFileLoadHistorical);
    s.click(kListRow0);
    for (std::size_t k = 0; k < i; ++k) {
      rig.key(ui::kKeyDown);
    }
    s.click(kListLoad);
    rig.menu(kFilesX, kFileSave);
    // The range and size checks, answered YES when they ask.
    rig.then([&] {
      const Scenario& sc = s.scenario();
      const bool in_range = sc.header.end_on_lost_contact == 0 ||
                            std::ranges::any_of(sc.objects(), [](const auto& o) {
                              return o->is_special_mission() || o->in_detection_range(false) ||
                                     o->in_detection_range(true);
                            });
      if (sc.too_large()) {
        rig.host.script.push_front([](ui::Gui& gui) { gui.mouse().post_key(kKeyYes, 0); });
      }
      if (!in_range) {
        rig.host.script.push_front([](ui::Gui& gui) { gui.mouse().post_key(kKeyYes, 0); });
      }
    });
    rig.type(name);
    rig.key(ui::kKeyEnter);
    rig.then([&, i, name, ref] {
      INFO(files[i]);
      const std::vector<std::uint8_t> original = install.read("SCEN/" + files[i]);
      const std::filesystem::path saved_path = rig.dir.path() / "CUSTOM" / (name + ".SCN");
      REQUIRE(std::filesystem::exists(saved_path));
      const std::vector<std::uint8_t> saved = read_file(saved_path);
      const ScnFile file = read_scn_file(original, files[i]);
      REQUIRE((*ref)->load(file));
      if (file.version == 7) {
        CHECK(saved == original);
      } else {
        ++older;
        CHECK(saved == write_scn_file((*ref)->save()));
      }
      // The game reads the same units from both.
      const sh::Scenario a = parse_scenario(original, files[i]);
      const sh::Scenario b = parse_scenario(saved, name);
      CHECK(a.player_sub.has_value() == b.player_sub.has_value());
      CHECK(a.ships.size() == b.ships.size());
      CHECK(a.enemy_subs.size() == b.enemy_subs.size());
      CHECK(a.aircraft.size() == b.aircraft.size());
      const std::unique_ptr<Mission> mission = Mission::create(install, tables, b, {});
      HeadlessRunner runner(*mission);
      runner.run(10);
      CHECK(mission->ships().size() + static_cast<std::size_t>(mission->skipped_ships()) == b.ships.size());
    });
  }
  rig.menu(kFilesX, kFileExit);
  CHECK(s.editor().run() == EditorExit::kExit);
  CHECK(older == 30);
}

}  // namespace sh::editor
