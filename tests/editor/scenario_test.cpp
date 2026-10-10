// The editor's object model and scenario document: the shipped scenarios
// loaded and saved, the memory estimate, File / New, the editing operations,
// the info texts.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "assets/scn_file.h"
#include "core/error.h"
#include "core/install.h"
#include "editor/data.h"
#include "editor/objects.h"
#include "editor/options.h"
#include "editor/scenario.h"
#include "game/stations/chart_view.h"
#include "game_data.h"
#include "password_key.h"

namespace sh::editor {
namespace {

constexpr std::int32_t kNow = 1000;

const EditorData& editor_data(const Install& install) {
  static const EditorData data = EditorData::load(install);
  return data;
}

// The editor started with all options on and the clock at `kNow`.
struct Session {
  explicit Session(const Install& install)
      : data(editor_data(install)), scenario(install, data, options, [] { return kNow; }) {}

  const EditorData& data;
  EditorOptions options;
  Scenario scenario;
};

std::vector<std::string> shipped_scenarios(const Install& install) {
  std::vector<std::string> files = install.list("SCEN", ".SCN");
  const std::vector<std::string> custom = install.list("CUSTOM", ".SCN");
  files.insert(files.end(), custom.begin(), custom.end());
  return files;
}

std::size_t ship_class(const EditorData& data, std::string_view name) {
  const std::optional<std::size_t> i = data.find_ship(name);
  REQUIRE(i);
  return *i;
}

std::size_t aircraft_type(const EditorData& data, std::string_view name) {
  const std::optional<std::size_t> i = data.find_aircraft(name);
  REQUIRE(i);
  return *i;
}

std::uint32_t model_size(const EditorData& data, std::string_view name) {
  const std::optional<std::uint32_t> size = data.models.size_of(name);
  REQUIRE(size);
  return *size;
}

// The estimate of a whole file computed from the tables: each object's cost
// plus each distinct model file once.
std::int32_t estimate(const EditorData& data, const ScnFile& file) {
  std::set<std::string> models;
  std::int32_t total = 0;
  const auto model = [&](const std::string& name) {
    if (!name.empty() && data.models.size_of(name) && models.insert(name).second) {
      total += static_cast<std::int32_t>(*data.models.size_of(name));
    }
  };
  for (const ScnRecord& r : file.records) {
    if (const auto* s = std::get_if<ScnShip>(&r)) {
      const ShipClass& c = data.tables.ships[*data.find_ship(scn_text(s->ship_class))];
      total += 1300;
      model(c.sprite1);
      model(c.sprite2);
    } else if (const auto* e = std::get_if<ScnEnemySub>(&r)) {
      const ShipClass& c = data.tables.ships[*data.find_ship(scn_text(e->sub_class))];
      total += 3000;
      model(c.sprite1);
      model(c.sprite2);
    } else if (const auto* a = std::get_if<ScnAircraft>(&r)) {
      total += 900;
      model(data.tables.aircraft[*data.find_aircraft(scn_text(a->aircraft_type))].sprite);
    } else if (std::holds_alternative<BomberGroupRecord>(r)) {
      total += 500;
    } else if (std::holds_alternative<PhotoReconRecord>(r)) {
      total += 300;
    }
  }
  return total;
}

// The record kinds in file order: 1 player, 2 ship, 3 aircraft, 4 enemy
// sub, 5 airstrike, 6 photo recon.
std::vector<int> tags(const ScnFile& file) {
  std::vector<int> out;
  for (const ScnRecord& r : file.records) {
    out.push_back(static_cast<int>(r.index()) + 1);  // the variant follows the tags
  }
  return out;
}

ScnShip ship_record(const EditorData& data, std::string_view cls, std::int32_t x, std::int32_t y) {
  ScnShip s;
  s.x = x;
  s.y = y;
  const std::string& name = data.tables.ships[ship_class(data, cls)].name;
  std::copy(name.begin(), name.end(), s.ship_class.begin());
  return s;
}

}  // namespace

TEST_CASE("Shipped version-7 scenarios load into objects and save byte-identical", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  int version7 = 0;
  for (const std::string& file : shipped_scenarios(install)) {
    INFO(file);
    const std::vector<std::uint8_t> data = install.read(file);
    const ScnFile scn = read_scn_file(data, file);
    if (scn.version != kScnWriteVersion) {
      continue;
    }
    ++version7;
    REQUIRE(session.scenario.load(scn));
    CHECK(session.scenario.objects().size() == scn.records.size());
    CHECK(session.scenario.selected() == &session.scenario.player());
    CHECK_FALSE(session.scenario.modified);
    CHECK(write_scn_file(session.scenario.save()) == data);
  }
  CHECK(version7 == 31);
}

TEST_CASE("The memory estimate matches the shipped required_memory", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  std::vector<std::string> one_low;
  for (const std::string& file : shipped_scenarios(install)) {
    INFO(file);
    ScnFile scn = read_scn_file(install.read(file), file);
    if (scn.version != kScnWriteVersion) {
      continue;
    }
    const std::int32_t shipped = scn.required_memory;
    // The object model's own estimate: the file loaded as a version 6
    // accumulates every object from the reader's -1.
    scn.version = 6;
    scn.required_memory = -1;
    REQUIRE(session.scenario.load(scn));
    const std::int32_t computed = session.scenario.header.required_memory + 1;
    CHECK(computed == estimate(session.data, scn));
    if (computed == shipped + 1) {
      one_low.push_back(file.substr(file.size() - 10, 6));
    } else {
      CHECK(computed == shipped);
    }
  }
  // Original bug (docs/editor-design.md "Pre-v7 required_memory"): the files made by
  // re-saving older versions keep the estimate accumulated from -1.
  std::vector<std::string> expected = {"HIST06"};
  for (int i = 32; i <= 45; ++i) {
    expected.push_back("HIST" + std::to_string(i));
  }
  std::ranges::sort(one_low);
  CHECK(one_low == expected);
}

TEST_CASE("Older scenarios are re-saved as the original editor saves them", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  int older = 0;
  for (const std::string& file : shipped_scenarios(install)) {
    INFO(file);
    const ScnFile scn = read_scn_file(install.read(file), file);
    if (scn.version == kScnWriteVersion) {
      continue;
    }
    ++older;
    REQUIRE(session.scenario.load(scn));
    const ScnFile saved = read_scn_file(write_scn_file(session.scenario.save()), file);
    CHECK(saved.version == kScnWriteVersion);
    // One byte low: accumulated from the reader's -1.
    CHECK(saved.required_memory == estimate(session.data, scn) - 1);
    CHECK(saved.special_mission == 0);
    CHECK(saved.end_on_lost_contact == (scn.version >= 4 ? scn.end_on_lost_contact : -1));
    // Below version 4 the seed is the one the reset before the load drew.
    CHECK(saved.random_seed == (scn.version >= 4 ? scn.random_seed : kNow + 1));
    CHECK(saved.records == scn.records);  // already in the reader's order
    for (std::size_t i = 0; i < 3; ++i) {
      CHECK(saved.reloads[i].type == static_cast<std::int32_t>(i) + 1);  // MARK10, MARK14, MARK18-1
      CHECK(saved.reloads[i].aft == scn.reloads[i].aft);
      CHECK(saved.reloads[i].forward == scn.reloads[i].forward);
    }
    CHECK(saved.reloads[3] == scn.reloads[3]);
    CHECK(saved.reloads[4] == scn.reloads[4]);
  }
  CHECK(older == 30);
}

TEST_CASE("A loaded file's records are regrouped in the reader's order", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  ScnFile scn = read_scn_file(install.read("SCEN/HIST57.SCN"), "HIST57.SCN");
  REQUIRE(tags(scn) == std::vector<int>{1, 2, 2, 2, 2, 2, 2, 2, 2, 4, 3});
  // Reverse the records: photo recon, airstrike, aircraft, sub, ships, player.
  scn.records.push_back(BomberGroupRecord{.base = 2, .size = BomberGroupSize::k30Planes});
  scn.records.push_back(PhotoReconRecord{.base = 3, .delay_min = 5, .duration_min = 60});
  std::ranges::reverse(scn.records);
  scn.version = 4;
  scn.required_memory = -1;  // as the reader leaves it below version 7
  REQUIRE(session.scenario.load(scn));
  const ScnFile saved = session.scenario.save();
  CHECK(tags(saved) == std::vector<int>{1, 2, 2, 2, 2, 2, 2, 2, 2, 4, 3, 5, 6});
  CHECK(saved.special_mission == -1);
  // Ships keep their relative order (reversed here).
  CHECK(saved.records[1] == scn.records[4]);
  CHECK(saved.required_memory == estimate(session.data, scn) - 1);
}

TEST_CASE("File / New gives SE's default scenario", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  // Something else first, to see New reset it.
  REQUIRE(s.load(read_scn_file(install.read("SCEN/HIST60.SCN"), "HIST60.SCN")));
  s.header.equipment = 0x1ff;
  s.new_scenario();
  CHECK(s.objects().size() == 1);
  CHECK(s.selected() == &s.player());
  CHECK(s.date() == ScenarioDate{7, 11, 11});
  CHECK(s.header.hour == 12);
  CHECK(s.header.minute == 0);
  CHECK(s.header.weather == Weather::kClear);
  CHECK(s.header.sea_state == SeaState::kCalm);
  CHECK(s.header.briefing[0] == '\0');
  CHECK(s.header.random_seed == kNow + 1);
  CHECK(s.header.end_on_lost_contact == kOptionOn);
  CHECK(s.header.equipment == 0x1c0);  // bits 0-5 cleared, the others kept
  CHECK(s.header.required_memory == 0);
  CHECK_FALSE(s.modified);
  // The last class in service on 7 Dec 1941 and its first boat.
  const ScnPlayerSub& p = s.player().rec;
  CHECK(scn_text(p.sub_class) == "T CLASS");
  CHECK(scn_text(p.name) == "(SS-200) Thresher");
  CHECK(p.depth == 14);
  CHECK(p.heading == 0);
  CHECK(p.engine_order == 0);
  CHECK(p.crew == CrewQuality::kGreen);
  CHECK(s.world().floor().height_at(p.x, p.y) <= -0x500);
  // Modern torpedoes: MARK14 (id 2) in all tubes, the class's reloads.
  CHECK(s.header.tube_torpedoes == std::array<std::int32_t, 10>{2, 2, 2, 2, 2, 2, 2, 2, 2, 2});
  CHECK(s.reload_aft == std::vector<std::int32_t>{0, 4, 0, 0});
  CHECK(s.reload_forward == std::vector<std::int32_t>{0, 10, 0, 0});
  // The S class has only the MARK10.
  std::copy_n("S CLASS", 8, s.player().rec.sub_class.begin());
  s.reset_torpedo_load();
  CHECK(s.header.tube_torpedoes[9] == 1);
  CHECK(s.reload_aft == std::vector<std::int32_t>{1, 0, 0, 0});
  CHECK(s.reload_forward == std::vector<std::int32_t>{6, 0, 0, 0});
  const ScnFile saved = s.save();
  CHECK(saved.reloads[0] == TorpedoReload{1, 1, 6});
  CHECK(saved.reloads[1] == TorpedoReload{2, 0, 0});
  CHECK(saved.reloads[2] == TorpedoReload{3, 0, 0});
  CHECK(saved.reloads[3].type == -1);
}

TEST_CASE("New and an older file leave no stale password bytes", "[editor][scenario]") {
  // Regression test for the original bug "Password storage" (SE zeroed
  // the first byte only, so the old password's bytes were saved).
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  s.header.password = encode_scn_password("Secret", sh::test::se_password_key(install));
  s.new_scenario();
  CHECK(s.save().password == ScnHeader{}.password);
  for (const std::string& file : shipped_scenarios(install)) {
    const ScnFile scn = read_scn_file(install.read(file), file);
    if (scn.version >= 4) {
      continue;
    }
    INFO(file);
    s.header.password = encode_scn_password("Secret", sh::test::se_password_key(install));
    REQUIRE(s.load(scn));
    CHECK(s.save().password == ScnHeader{}.password);
    break;
  }
}

TEST_CASE("SE's home view is centred on the declared map size", "[editor][scenario]") {
  // sb_setup_world centres the view on half the map extents 0xa4e20 /
  // 0xa4e24, the declared chart size (1200 wide, one less than the rows)
  // less one: x 599, not 600 (a regression after the game's chart width
  // fix).
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  const World& world = session.scenario.world().world();
  REQUIRE(world.chart.width == 1201);
  const std::array<std::int32_t, 2> home = view_home_center(world);
  CHECK(home[0] == 599 << 16);
  CHECK(home[1] == (world.chart.height - 1) / 2 << 16);
  // Both the default player (through a view of its own) and the chart
  // screen start from it.
  ChartView view(world, 0, 0, 639, 458);
  view.set_zoom(0x10000);
  view.set_center(home[0], home[1]);
  CHECK(view.center_x() == home[0]);
  CHECK(view.center_y() == home[1]);
}

TEST_CASE("Adding and removing objects updates the records and the estimate", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const EditorData& data = session.data;
  const WorldPoint at = {s.player().rec.x + 0x10000, s.player().rec.y};
  REQUIRE(s.can_place_vessel(ship_class(data, "FUSO"), at));

  Vessel& fuso = s.add_vessel(ship_class(data, "FUSO"), at, Vessel::Kind::kShip);
  CHECK(s.selected() == &fuso);
  CHECK(s.modified);
  const auto fuso_models = static_cast<std::int32_t>(model_size(data, "FUSO.SHP") + model_size(data, "FUSOS.SHP"));
  CHECK(s.header.required_memory == 1300 + fuso_models);
  Vessel& sub = s.add_vessel(ship_class(data, "KAIDAI TYPE"), at, Vessel::Kind::kSubmarine);
  const auto japsub = static_cast<std::int32_t>(model_size(data, "JAPSUB.SHP"));
  CHECK(s.header.required_memory == 1300 + fuso_models + 3000 + japsub);
  Aircraft& zeke = s.add_aircraft(aircraft_type(data, "ZEKE"), AircraftMission::kSearchOrbit2);
  SpecialMission& strike = s.add_airstrike();
  SpecialMission& recon = s.add_photo_recon();
  CHECK(s.header.required_memory ==
        1300 + fuso_models + 3000 + japsub + 900 + static_cast<std::int32_t>(model_size(data, "ZEKE.SHP")) + 500 + 300);

  const ScnFile saved = s.save();
  CHECK(tags(saved) == std::vector<int>{1, 2, 4, 3, 5, 6});
  CHECK(saved.special_mission == -1);
  const auto& ship = std::get<ScnShip>(saved.records[1]);
  CHECK(scn_text(ship.ship_class) == "FUSO");
  CHECK(ship.x == at.x);
  CHECK(ship.orders == ShipOrders::kNone);
  CHECK(ship.waypoints.count == 0);
  const auto& enemy = std::get<ScnEnemySub>(saved.records[2]);
  CHECK(scn_text(enemy.sub_class) == "KAIDAI TYPE");
  CHECK(enemy.side == 1);
  CHECK(enemy.target_x == -1);
  const auto& air = std::get<ScnAircraft>(saved.records[3]);
  CHECK(air.bomb_load == 2);
  CHECK(air.strafes == 1);
  CHECK(air.relaunch == 1);
  CHECK(std::get<BomberGroupRecord>(saved.records[4]).base == -1);
  CHECK(std::get<PhotoReconRecord>(saved.records[5]).duration_min == 1440);

  s.remove(recon);
  s.remove(strike);
  s.remove(zeke);
  s.select(&sub);
  s.remove(sub);  // a sub the editor made releases both model names
  CHECK(s.selected() == &s.player());
  CHECK(s.model_cache().references("JAPSUB.SHP") == 0);
  CHECK(s.header.required_memory == 1300 + fuso_models);
  s.remove(fuso);
  CHECK(s.header.required_memory == 0);
  CHECK(s.objects().size() == 1);
  CHECK_THROWS_AS(s.remove(s.player()), std::invalid_argument);
}

TEST_CASE("A loaded enemy sub releases both models on removal", "[editor][scenario]") {
  // Regression test for the original bug "Enemy-sub model names" (SE kept
  // the first model's reference).
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  REQUIRE(s.load(read_scn_file(install.read("SCEN/HIST58.SCN"), "HIST58.SCN")));
  const auto it = std::ranges::find_if(s.objects(), [](const auto& o) { return o->type() == ObjectType::kSubmarine; });
  REQUIRE(it != s.objects().end());
  auto& sub = static_cast<Vessel&>(**it);
  CHECK(sub.model_name1() == "JAPSUB.SHP");
  CHECK(sub.model_name2() == "JAPSUB.SHP");
  REQUIRE(s.model_cache().references("JAPSUB.SHP") == 2);
  const std::int32_t before = s.header.required_memory;
  s.remove(sub);
  CHECK(s.model_cache().references("JAPSUB.SHP") == 0);
  CHECK(s.header.required_memory < before - 3000);
}

TEST_CASE("Changing a vessel between ship and sub classes keeps the shared fields", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const EditorData& data = session.data;
  ScnFile file = s.save();
  ScnShip ship = ship_record(data, "MUTSUKI", s.player().rec.x + 0x8000, s.player().rec.y);
  ship.heading = 315;
  ship.speed = 12;
  ship.crew = CrewQuality::kElite;
  ship.damage_pct = 30;
  ship.orders = ShipOrders::kPatrol;
  ship.waypoints.count = 2;
  ship.waypoints.x[0] = ship.x;
  ship.waypoints.y[0] = ship.y;
  ship.waypoints.x[1] = ship.x + 100;
  ship.waypoints.y[1] = ship.y + 100;
  file.records.push_back(ship);
  REQUIRE(s.load(file));
  auto& v = static_cast<Vessel&>(*s.objects()[1]);
  const std::int32_t before = s.header.required_memory;

  s.change_vessel_class(v, ship_class(data, "KAIDAI TYPE"));
  CHECK(v.kind == Vessel::Kind::kSubmarine);
  CHECK(v.type() == ObjectType::kSubmarine);
  const auto sub = std::get<ScnEnemySub>(v.record());
  CHECK(scn_text(sub.sub_class) == "KAIDAI TYPE");
  CHECK(sub.x == ship.x);
  CHECK(sub.y == ship.y);
  CHECK(sub.heading == 315);
  CHECK(sub.speed == 12);
  CHECK(sub.crew == CrewQuality::kElite);
  CHECK(sub.damage_pct == 30);
  CHECK(sub.waypoints == ship.waypoints);
  CHECK(sub.orders == ShipOrders::kNone);  // another role group
  CHECK(sub.depth == 0);
  CHECK(sub.target_x == -1);
  const auto mutsuki = static_cast<std::int32_t>(model_size(data, "MUTSUKI.SHP") + model_size(data, "MUTSUKIS.SHP"));
  const auto japsub = static_cast<std::int32_t>(model_size(data, "JAPSUB.SHP"));
  CHECK(s.header.required_memory == before - mutsuki + japsub);

  s.change_vessel_class(v, ship_class(data, "FUSO"));
  CHECK(v.kind == Vessel::Kind::kShip);
  const auto back = std::get<ScnShip>(v.record());
  CHECK(scn_text(back.ship_class) == "FUSO");
  CHECK(back.heading == 315);
  CHECK(back.waypoints == ship.waypoints);
  CHECK(s.model_cache().references("JAPSUB.SHP") == 0);
}

TEST_CASE("Convoy operations follow SE's rules", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const EditorData& data = session.data;
  const WorldPoint at = {s.player().rec.x + 0x10000, s.player().rec.y};
  Vessel& leader = s.add_vessel(ship_class(data, "LFREIGHT"), at, Vessel::Kind::kShip);  // top speed 10
  Vessel& member = s.add_vessel(ship_class(data, "MERCHANT"), at, Vessel::Kind::kShip);
  Vessel& escort = s.add_vessel(ship_class(data, "FUSO"), at, Vessel::Kind::kShip);  // top speed 22.5
  Aircraft& cover = s.add_aircraft(aircraft_type(data, "ZEKE"), AircraftMission::kSearchOrbit);

  CHECK(s.first_free_convoy_id() == 1);
  CHECK(s.first_existing_convoy_id() == 0);
  leader.ship.orders = ShipOrders::kConvoyLeader;
  leader.ship.convoy_group = s.first_free_convoy_id();
  leader.set_heading(900);
  leader.set_speed(8);
  CHECK(s.first_free_convoy_id() == 2);
  CHECK(s.convoy_leader(1) == &leader);
  CHECK_FALSE(s.convoy_has_followers(1));

  // A member joins: the leader's heading and speed, no course.
  member.ship.orders = ShipOrders::kConvoyMember;
  member.set_damage(40);
  member.waypoints().count = 3;
  REQUIRE(s.pick_convoy(member, leader));
  CHECK(member.ship.convoy_group == 1);
  CHECK(member.ship.heading == 900);
  CHECK(member.sub.heading == 900);
  CHECK(member.ship.speed == 8);
  CHECK(member.ship.waypoints.count == 0);
  // Regression test for the original bug "Convoy member join" (SE zeroed
  // the damage and the sub part's speed).
  CHECK(member.ship.damage_pct == 40);
  CHECK(member.sub.speed == 8);

  // An escort: twice the leader's speed, at most its own top speed.
  escort.ship.orders = ShipOrders::kEscort;
  REQUIRE(s.pick_convoy(escort, member));
  CHECK(escort.ship.convoy_group == 1);
  CHECK(escort.ship.speed == 16);
  leader.set_speed(12);
  s.update_escort_speed(1);
  CHECK(escort.ship.speed == 22);  // "22.5" rounded half to even
  REQUIRE(s.pick_convoy(cover, leader));
  CHECK(cover.rec.escort_group == 1);
  // Regression test for the original bug "Escort speed of aircraft" (SE
  // wrote the escort speed into an escorting aircraft's x).
  const std::int32_t cover_x = cover.rec.x;
  s.update_escort_speed(1);
  CHECK(cover.rec.x == cover_x);
  CHECK(s.convoy_has_followers(1));
  CHECK(s.convoy_slowest_speed(1) == 10);
  CHECK(s.ship_max_speed(leader) == 10);
  CHECK_FALSE(s.pick_convoy(member, cover));  // only ships are picked

  // Removing the leader releases its followers.
  CHECK(s.removal_releases_convoy(leader));
  s.remove(leader);
  CHECK(escort.ship.orders == ShipOrders::kPatrol);
  CHECK(escort.ship.convoy_group == 0);
  CHECK(member.ship.orders == ShipOrders::kNone);
  CHECK(member.ship.convoy_group == 0);
  CHECK(cover.rec.mission == AircraftMission::kSearchOrbit2);
  CHECK(cover.rec.escort_group == 0);
  CHECK(s.first_free_convoy_id() == 1);

  // Joining without a convoy drops the orders.
  member.ship.orders = ShipOrders::kConvoyMember;
  s.apply_convoy_membership(member);
  CHECK(member.ship.orders == ShipOrders::kNone);
}

TEST_CASE("Courses take 24 points with a loop or stop terminator", "[editor][scenario]") {
  Waypoints course;
  Scenario::start_course(course, {100, 200});
  CHECK(course.count == 1);
  CHECK(course.x[0] == 100);
  CHECK(Scenario::add_course_point(course, {110, 210}, false, false) == CourseClick::kAdded);
  CHECK(course.count == 2);
  CHECK(course.x[2] == 0);  // open course: stop
  // Closing needs more than 2 points.
  CHECK(Scenario::add_course_point(course, {100, 200}, false, true) == CourseClick::kAdded);
  CHECK(course.count == 3);
  CHECK(Scenario::add_course_point(course, {101, 201}, false, true) == CourseClick::kClosed);
  CHECK(course.count == 3);
  CHECK(course.x[3] == -1);  // looped
  for (int i = 3; i < Scenario::kMaxCoursePoints; ++i) {
    CHECK(Scenario::add_course_point(course, {i, i}, true, false) == CourseClick::kAdded);
  }
  CHECK(course.count == 24);
  CHECK(course.x[24] == -1);
  CHECK(Scenario::add_course_point(course, {1, 1}, true, false) == CourseClick::kFull);
  CHECK(course.count == 24);

  Aircraft a;
  CHECK(Scenario::add_aircraft_point(a, {500, 600}));
  CHECK(a.rec.x == 500);
  CHECK(a.rec.y == 600);
  CHECK(Scenario::add_aircraft_point(a, {700, 800}));
  CHECK(a.rec.waypoints.count == 2);
  CHECK(a.rec.waypoints.x[2] == -1);
  a.move_to({600, 600});
  CHECK(a.rec.waypoints.x[1] == 800);
  CHECK(a.rec.waypoints.y[1] == 800);
}

TEST_CASE("Moving objects follows the depth and margin rules", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const EditorData& data = session.data;
  const WorldPoint start = {s.player().rec.x, s.player().rec.y};
  Vessel& v = s.add_vessel(ship_class(data, "FUSO"), start, Vessel::Kind::kShip);
  Scenario::start_course(v.waypoints(), start);
  Scenario::add_course_point(v.waypoints(), {start.x + 0x100, start.y}, false, false);
  s.modified = false;
  const WorldPoint to = {start.x + 0x4000, start.y};
  REQUIRE(s.move_object(v, to));
  CHECK(v.ship.x == to.x);
  CHECK(v.sub.x == to.x);
  CHECK(v.ship.waypoints.x[0] == to.x);  // waypoint 0 follows
  CHECK(v.sub.waypoints.x[0] == to.x);
  CHECK(s.modified);
  CHECK_FALSE(s.move_object(v, {Scenario::kMapMargin - 1, start.y}));
  CHECK(v.ship.x == to.x);
}

TEST_CASE("The info texts of each object class", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const EditorData& data = session.data;
  const World& world = s.world().world();
  const auto loc = [&](WorldPoint p) {
    const auto angle = [](const GeoAngle& a) {
      return std::to_string(a.whole_degrees) + "\x1a" + (a.minutes < 10 ? "0" : "") + std::to_string(a.minutes) +
             "\x1b" + a.hemisphere;
    };
    return "Loc: " + angle(world.latitude_at(p.y)) + ", " + angle(world.longitude_at(p.x));
  };
  const ObjectEnv env = s.object_env();

  PlayerSub& player = s.player();
  player.rec.heading = 45;
  player.rec.engine_order = 4;
  const DescribeText p = player.describe(env);
  CHECK(p[0] == "(SS-200) Thresher");
  CHECK(p[1] == "Class: T CLASS");
  CHECK(p[2] == loc({player.rec.x, player.rec.y}));
  CHECK(p[3] == "Depth: 14 feet");
  CHECK(p[4] == "Heading: 045");
  CHECK(p[5] == "Speed: Ahead Full");
  CHECK(player.describe(ObjectEnv{data, s.world(), false}) == DescribeText{});
  CHECK(player.type_name(data.se) == "PLAYER");

  const WorldPoint at = {player.rec.x + 0x8000, player.rec.y};
  Vessel& fuso = s.add_vessel(ship_class(data, "FUSO"), at, Vessel::Kind::kShip);
  fuso.set_speed(7);
  fuso.ship.orders = ShipOrders::kConvoyLeader;
  fuso.ship.convoy_group = 2;
  const DescribeText f = fuso.describe(env);
  CHECK(f[0] == "FUSO (BB)");
  CHECK(f[1] == loc(at));
  CHECK(f[2] == "Heading: 000");
  CHECK(f[3] == "Speed: 07 kts");
  CHECK(f[4] == "Orders: Convoy Leader [B]");
  CHECK(fuso.type_name(data.se) == "SHIP");
  CHECK(s.add_vessel(ship_class(data, "LFREIGHT"), at, Vessel::Kind::kShip).describe(env)[0] ==
        "Large Freighter (MR)");
  CHECK(s.add_vessel(ship_class(data, "SAMPAN"), at, Vessel::Kind::kShip).describe(env)[0] == "Sampan");

  Vessel& sub = s.add_vessel(ship_class(data, "KAIDAI TYPE"), at, Vessel::Kind::kSubmarine);
  sub.sub.depth = 60;
  sub.sub.orders = ShipOrders::kFollowPath;
  const DescribeText u = sub.describe(env);
  CHECK(u[0] == "KAIDAI TYPE (SS) ");
  CHECK(u[2] == "Depth: 60 feet");
  CHECK(u[4] == "Speed: 00 kts");
  CHECK(u[5] == "Orders: Follow Path");
  CHECK(sub.type_name(data.se) == "SUBMARINE");

  Aircraft& zeke = s.add_aircraft(aircraft_type(data, "ZEKE"), AircraftMission::kSearchOrbit);
  zeke.rec.escort_group = 1;
  const DescribeText z = zeke.describe(env);
  CHECK(z[0] == "MITSUBISHI A6M 'ZEKE'");
  CHECK(z[1] == "Fighter");
  CHECK(z[2] == "Guns: Yes");
  CHECK(z[3] == "Bombs: 2");
  CHECK(z[4] == "Orders: Escort [A]");

  REQUIRE(s.world().bases().size() > 3);
  const BaseRecord& base = s.world().bases()[3];
  SpecialMission& strike = s.add_airstrike();
  strike.rec = BomberGroupRecord{.base = 3, .size = BomberGroupSize::k30Planes, .launch_delay_min = 90, .hold_min = 45};
  const DescribeText a = strike.describe(env);
  CHECK(a[0] == "AIRSTRIKE");
  CHECK(a[1] == "Target: " + base.name);
  CHECK(a[2] == loc({base.station.x, base.station.z}));
  CHECK(a[3] == "Strike Size: MEDIUM");
  CHECK(a[4] == "Time delay: 01:30");
  CHECK(a[5] == "Flight delay: 00:45");
  CHECK(strike.position(env) == WorldPoint{base.station.x, base.station.z});
  const DescribeText r = s.add_photo_recon().describe(env);
  CHECK(r[0] == "PHOTO-RECON");
  CHECK(r[1] == "Target: ");
  CHECK(r[3] == "Time delay: 00:00");
  CHECK(r[4] == "Time limit: 24:00");
}

TEST_CASE("Visibility from the player uses the periscope eye and the radar fit", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const WorldPoint p = {s.player().rec.x, s.player().rec.y};
  Vessel& near = s.add_vessel(ship_class(session.data, "FUSO"), {p.x + 0x1000, p.y}, Vessel::Kind::kShip);
  CHECK(near.in_detection_range(false));
  CHECK_FALSE(near.in_detection_range(true));  // no surface radar
  // Green crew: the range less 20 %.
  CHECK(near.visual_range() > 0);
  s.header.equipment = static_cast<std::uint32_t>(Equipment::kSjaRadar);
  s.update_all_visibility();
  CHECK(near.in_detection_range(true));
  CHECK(player_radar_range_yd(static_cast<std::uint32_t>(Equipment::kSj1Radar)) == 35000);
  CHECK(player_radar_range_yd(static_cast<std::uint32_t>(Equipment::kSjaRadar)) == 15000);
}

TEST_CASE("The memory budget uses SE's BASMEM and SFXBUF defaults", "[editor][scenario]") {
  // Enough memory: SFXBUF at its 0x1ab3f0 cap.
  CHECK(memory_budget(0, 0xf00000) == 0xf00000 + 0x183000 - 0x1ab3f0 - 0x4b6f90);
  // Less: 530000 plus 1191/2000 of the KiB above 0x6d4f70.
  CHECK(memory_budget(0, 0x600000) == 0x783000 - 953936 - 0x4b6f90);
  CHECK(memory_budget(1000, 0x600000) == 0x783000 - 953936 - 0x4b6f90 - 1000);
}

TEST_CASE("Date helpers of the war period", "[editor][scenario]") {
  CHECK(war_period({6, 11, 11}) == WarPeriod::kBefore);
  CHECK(war_period(kWarStart) == WarPeriod::kDuring);
  CHECK(war_period(kWarEnd) == WarPeriod::kDuring);
  CHECK(war_period({16, 7, 15}) == WarPeriod::kAfter);
  CHECK(war_period({1, 0, 16}) == WarPeriod::kAfter);
  CHECK(days_in_month(1, 14) == 29);  // 1944
  CHECK(days_in_month(1, 13) == 28);
  CHECK(days_in_month(11, 11) == 31);
}

TEST_CASE("A refused or unreadable load leaves a new scenario", "[editor][scenario]") {
  SH_REQUIRE_GAME_DATA(install);
  Session session(install);
  Scenario& s = session.scenario;
  const ScnFile hist = read_scn_file(install.read("SCEN/HIST60.SCN"), "HIST60.SCN");
  CHECK_FALSE(s.load(hist, [](const ScnHeader&) { return false; }));
  CHECK(s.objects().size() == 1);
  CHECK(scn_text(s.header.world) == "WORLD-1.WLD");
  ScnFile bad = hist;
  std::copy_n("NOWORLD.WLD", 12, bad.world.begin());
  CHECK_THROWS_AS(s.load(bad), DataError);
  CHECK(scn_text(s.header.world) == "WORLD-1.WLD");
  CHECK(s.objects().size() == 1);
  // A file without a player record spawns the last file's player.
  REQUIRE(s.load(hist));
  ScnFile no_player = hist;
  no_player.records.erase(no_player.records.begin());
  REQUIRE(s.load(no_player));
  CHECK(s.player().rec == std::get<ScnPlayerSub>(hist.records[0]));
}

}  // namespace sh::editor
