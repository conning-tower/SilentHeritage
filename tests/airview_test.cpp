// The aircraft, enemy submarines and air reports in the outside view, the
// chart and the radio log, on HIST57 (a Kaidai submarine and a Val escort).
#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/patrol_zones.h"
#include "assets/pcx.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game/radio_room.h"
#include "game/stations/chart.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "platform/screenshot.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/enemy_sub.h"
#include "sim/simulation.h"
#include "stations_control_fixture.h"

namespace sh {

namespace {

constexpr const char* kScenario = "SCEN/HIST57.SCN";

// Saves `image` with `palette` as airview_`name`.bmp into the directory
// named by SH_SCREENS, when set (for checking the views by eye).
void save_view(const std::string& name, const Image8& image, const VgaPalette& palette) {
  const char* dir = std::getenv("SH_SCREENS");
  if (dir == nullptr || *dir == '\0') {
    return;
  }
  save_bmp(std::filesystem::path(dir) / ("airview_" + name + ".bmp"), image, to_palette(palette));
}

const RenderObject* find_object(const Scene& scene, RenderType type) {
  for (const RenderObject& o : scene.objects) {
    if (o.type == type) {
      return &o;
    }
  }
  return nullptr;
}

// Moves `sub` to world point `p` (its integrated position too).
void place(EnemySub& sub, const fx::Vec3& p) {
  MotionState& m = sub.motion();
  m.position = p;
  m.pos_x = std::int64_t{p.x} << 16;
  m.pos_y = std::int64_t{p.y} << 16;
  m.pos_z = std::int64_t{p.z} << 16;
}

// Returns the screen pixels of the 41 x 41 square around world point (`x`,
// `z`) on `chart`.
std::vector<std::uint8_t> chart_patch(const test::ControlRig& rig, const ChartStation& chart, std::int32_t x,
                                      std::int32_t z) {
  const ChartPoint p = chart.view().world_to_screen(x, z);
  std::vector<std::uint8_t> out;
  const Image8& screen = rig.screen->screen();
  for (int dy = -20; dy <= 20; ++dy) {
    for (int dx = -20; dx <= 20; ++dx) {
      const int sx = chart.view().x0() + p.x + dx;
      const int sy = chart.view().y0() + p.y + dy;
      out.push_back(screen.contains(sx, sy) ? screen.at(sx, sy) : 0);
    }
  }
  return out;
}

}  // namespace

TEST_CASE("The view adds a sighted aircraft and an enemy submarine with their sprites", "[airview][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, kScenario, {});
  HeadlessRunner runner(*mission);
  runner.run(1);
  REQUIRE(mission->enemy_subs().size() == 1);
  REQUIRE(mission->aircraft().size() == 1);
  EnemySub& sub = *mission->enemy_subs()[0];
  Aircraft& val = *mission->aircraft()[0];
  const fx::Vec3 own = mission->player().position();

  // The Kaidai surfaced 1500 yd due north of the boat, heading east; the
  // Val 600 yd north at 300 ft, banked and nose down.
  sub.motion().position = {own.x, 0, own.z + 1500 * kUnitsPerYard};
  sub.motion().heading = Angle10(900);
  SimContext ctx = mission->sim().context();
  val.take_off(ctx, {own.x + 100 * kUnitsPerYard, 300 * kUnitsPerFoot, own.z + 600 * kUnitsPerYard}, 9000);
  val.flight().pitch = Angle10(100);
  val.flight().roll = Angle10(-300);

  WorldViewAdapter view(install, *mission);
  view.update_sky(*mission);
  const ViewEye eye = bridge_eye(mission->player(), Angle10(-mission->player().heading().tenths()));
  WatcomRandom random;

  // Not sighted yet: the aircraft is left out, the submarine is not.
  const Scene unseen = view.build_scene(*mission, eye, random);
  CHECK(find_object(unseen, RenderType::kAircraft) == nullptr);
  val.mark_sighted(mission->sim().clock());
  const Scene scene = view.build_scene(*mission, eye, random);

  const RenderObject* s = find_object(scene, RenderType::kEnemySub);
  REQUIRE(s != nullptr);
  const ShipClass& kaidai = *sub.setup().ship_class;
  REQUIRE(s->sprite != nullptr);
  CHECK(s->x == own.x);
  CHECK(s->z == own.z + 1500 * kUnitsPerYard);
  CHECK(s->heading.tenths() == 900);
  // enemy_sub_add_render: SHIPS.DAT +0x58 render size; the render callback
  // passes the visual size +0xbc alone as the hull-down height.
  CHECK(s->size == (static_cast<std::uint32_t>(kaidai.raw[0x58]) | static_cast<std::uint32_t>(kaidai.raw[0x59]) << 8 |
                    static_cast<std::uint32_t>(kaidai.raw[0x5a]) << 16 |
                    static_cast<std::uint32_t>(kaidai.raw[0x5b]) << 24));
  CHECK(s->ship.hull_down_height_ft == static_cast<std::int32_t>(kaidai.visual_size));
  CHECK(s->ship.hull_down_end_yards == sub.player_range_yd());
  CHECK(s->ship.range_yards == 1500);

  const RenderObject* a = find_object(scene, RenderType::kAircraft);
  REQUIRE(a != nullptr);
  REQUIRE(a->sprite != nullptr);
  // VAL.SHP from 3DAIR.CAT (12 yaw x 7 elevation views), 4500 units large
  // (AIRCRAFT.DAT +0x6c).
  CHECK(a->sprite->frame_count() >= 7 * 7);
  CHECK(a->size == 4500u << 16);
  CHECK(a->y == 300 * kUnitsPerFoot);
  CHECK(a->pitch.tenths() == 100);
  CHECK(a->roll.tenths() == -300);

  // Both project in front of the bridge looking north.
  view.render(*mission, eye, random);
  bool sub_drawn = false;
  bool val_drawn = false;
  const RenderList list = view.render(*mission, eye, random);
  for (const std::size_t i : list.order()) {
    const RenderEntry& e = list.entries()[i];
    sub_drawn |= e.type == RenderType::kEnemySub;
    val_drawn |= e.type == RenderType::kAircraft;
  }
  CHECK(sub_drawn);
  CHECK(val_drawn);
  VgaPalette palette = load_pcx(install, "ART/BRIDGE.PCX").palette;
  view.shade_palette(palette);
  save_view("bridge", view.frame(), palette);
}

TEST_CASE("Smoke puffs grow from 2000 to 6000 units over their half second", "[airview]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, kScenario, {});
  const fx::Vec3 own = mission->player().position();
  Smoke& smoke = mission->sim().add(std::make_unique<Smoke>(fx::Vec3{own.x, 3000, own.z + 20000}));
  WorldViewAdapter view(install, *mission);
  WatcomRandom random;
  const ViewEye eye = bridge_eye(mission->player(), Angle10(0));
  const Scene first = view.build_scene(*mission, eye, random);
  const RenderObject* o = find_object(first, RenderType::kStaticObject);
  REQUIRE(o != nullptr);
  CHECK(o->size == 2000u << 16);
  CHECK(o->sprite == &view.assets().effect(EffectSprite::kSmoke));
  SimContext ctx = mission->sim().context();
  smoke.update(ctx, Fixed16::from_raw(0x4000));  // a quarter second
  const Scene later = view.build_scene(*mission, eye, random);
  o = find_object(later, RenderType::kStaticObject);
  REQUIRE(o != nullptr);
  CHECK(o->size == 4000u << 16);
}

TEST_CASE("Radio reports read like the original's RMSG.DAT messages", "[airview][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, kScenario, {});
  const ExeImage exe = ExeImage::load(install);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "DATA/PZ.DAT");
  // WORLD-14 belongs to a ComSubSoWesPac zone.
  const std::string from = command_name(exe, command_of_map(zones, "WORLD-14.WLD"));
  CHECK(from == "ComSubSoWesPac");
  CHECK(command_name(exe, Command::kComSubPac) == "ComSubPac");

  // The boat's start: 3-03S 109-08E (world_pos_to_latlon on WORLD-14's
  // bounds, computed with the Python reference).
  RadioReport pilot;
  pilot.kind = RadioReport::Kind::kDownedPilot;
  pilot.x = 9699328;
  pilot.z = 14942208;
  const RadioMessage m = radio_message(pilot, mission->world(), mission->player_name(), from);
  CHECK(m.template_index == 1);
  CHECK(m.to == "(SS-268) Puffer");
  CHECK(m.from == "ComSubSoWesPac");
  const std::vector<std::string> bases = {"Surabaya", "Balikpapan"};
  const std::vector<std::string> types = {"Buoy", "City", "Port Facilities"};
  const RadioTextContext names{m.to, bases, types};
  CHECK(format_radio_message(m, tables.radio_messages[1], names) ==
        "CALLING (SS-268) PUFFER...PILOT DOWN...LAST REPORTED POSITION CLOSE TO 3-03S,109-08E");

  RadioReport strike;
  strike.kind = RadioReport::Kind::kAirStrike;
  strike.args = {7, 5, 23, 1, 1, 0};  // ETA 07:05, 23 February, Balikpapan
  strike.x = 10152960;
  strike.z = 14620160;
  const RadioMessage s = radio_message(strike, mission->world(), mission->player_name(), from);
  CHECK(format_radio_message(s, tables.radio_messages[0], names) ==
        "AT 07:05, 23 FEBRUARY BE ON LIFEGUARD STATION WITHIN TWENTY MILES OF BALIKPAPAN (3-11S,109-20E)"
        "...CONDUCT LIFEGUARD SERVICES...PROCEDURE IN ACCORDANCE WITH SOP TWO");

  RadioReport recon;
  recon.kind = RadioReport::Kind::kPhotoRecon;
  recon.args = {2, 0, 18, 30, 24, 1};  // Port Facilities near Surabaya before 18:30, 24 February
  recon.x = 10152960;
  recon.z = 14620160;
  const RadioMessage r = radio_message(recon, mission->world(), mission->player_name(), from);
  CHECK(format_radio_message(r, tables.radio_messages[2], names) ==
        "REQUEST PHOTO RECONNAISSANCE MISSION...PHOTOGRAPH PORT FACILITIES NEAR SURABAYA...LOCATION "
        "3-11S,109-20E BEFORE 18:30, 24 FEBRUARY");
}

TEST_CASE("Radio messages wait for the radio, then reach the log with the crew's report", "[airview]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, kScenario, {});
  RadioRoom radio(*mission, "ComSubSoWesPac");
  mission->add_frame_task(radio);
  const auto has_report = [](const MissionEvents& e) {
    for (const CrewReport& c : e.crew_messages) {
      if (c.id == kMsgIncomingRadio) {
        return true;
      }
    }
    return false;
  };

  // A pilot's report goes out during a frame: it waits for the next one.
  RadioReport pilot;
  pilot.kind = RadioReport::Kind::kDownedPilot;
  pilot.x = mission->player().position().x;
  pilot.z = mission->player().position().z;
  mission->sim().context().events.radio_reports.push_back(pilot);
  MissionEvents first = mission->simulate_frame(5, false);
  CHECK_FALSE(has_report(first));
  CHECK(radio.waiting().size() == 1);
  CHECK(radio.take_delivered().empty());

  // With the radio damaged it keeps waiting.
  SystemState& set = mission->player().systems()[static_cast<std::size_t>(PlayerSystem::kRadio)];
  set.flags = 1;
  set.repair_time = 10000;
  CHECK_FALSE(has_report(mission->simulate_frame(5, false)));
  CHECK(radio.waiting().size() == 1);

  set.flags = 0;
  mission->sim().clock().double_compression();
  const MissionEvents up = mission->simulate_frame(5, false);
  CHECK(has_report(up));
  CHECK(radio.waiting().empty());
  const std::vector<RadioMessage> log = radio.take_delivered();
  REQUIRE(log.size() == 1);
  const GameClock& clock = mission->sim().clock();
  CHECK(log[0].unread);
  CHECK(log[0].template_index == 1);
  CHECK(log[0].hour == clock.hour());
  CHECK(log[0].minute == clock.minute());
  CHECK(log[0].day == clock.day());
  CHECK(log[0].month == clock.month());
  CHECK(log[0].year == clock.year());
  CHECK(clock.requested_compression() == 1);
}

TEST_CASE("The chart shows a surfaced enemy submarine and hides a submerged one", "[airview][content]") {
  SH_REQUIRE_GAME_DATA(install);
  test::ControlRig rig(install, kScenario);
  auto owned = std::make_unique<ChartStation>(rig.screen->env());
  ChartStation& chart = *owned;
  rig.screen->install(StationId::kChart, std::move(owned));
  rig.gui.mouse().update(320, 200, 0, 0);
  rig.frames(1);
  rig.settle();
  rig.key(0x3f00);  // F5
  rig.settle();
  Mission& mission = rig.mission();
  EnemySub& sub = *mission.enemy_subs()[0];
  const fx::Vec3 own = mission.player().position();
  const std::int32_t sx = own.x + 3000 * kUnitsPerYard;
  const std::int32_t sz = own.z;
  place(sub, {sx, 0, sz});
  chart.view().set_zoom(16 << 16);
  chart.center_button();
  rig.frames(30, 10);
  const std::vector<std::uint8_t> shown = chart_patch(rig, chart, sx, sz);
  rig.save("airview_chart_sub");
  // Deeper than its visual size (SHIPS.DAT +0xbc) and never seen: nothing
  // marks it.
  const std::int32_t deep = -(static_cast<std::int32_t>(sub.setup().ship_class->visual_size) + 20) * kUnitsPerFoot;
  place(sub, {sx, deep, sz});
  sub.dive().depth_ft = -deep / kUnitsPerFoot;
  rig.frames(30, 10);
  const std::vector<std::uint8_t> hidden = chart_patch(rig, chart, sx, sz);
  CHECK(sub.visual_height() <= 0);
  CHECK(shown != hidden);
}

}  // namespace sh

namespace sh {

TEST_CASE("A downed pilot the boat touched comes aboard once it is slow", "[airview][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, kScenario, {});
  PlayerSub& boat = mission->player();
  const fx::Vec3 own = boat.position();
  DownedPilot& pilot = mission->sim().add(
      std::make_unique<DownedPilot>(fx::Vec3{own.x, 0, own.z}, Angle10(0), mission->air_theatre()));
  CHECK_FALSE(boat.can_rescue_pilot());

  // Touching him at 9 kn (the raft's update finds the overlap, the boat's
  // next one acts on it): alongside, but too fast to take him.
  std::int32_t rescued = mission->simulate_frame(5, false).pilots_rescued;
  rescued += mission->simulate_frame(5, false).pilots_rescued;
  CHECK(rescued == 0);
  CHECK(boat.motion().speed_tenths > 50);
  CHECK(boat.can_rescue_pilot());
  CHECK_FALSE(pilot.removed());

  // Slowed to 5 kn, the 'E' key takes him aboard.
  boat.motion().speed_tenths = 50;
  SimContext ctx = mission->sim().context();
  CHECK(boat.rescue_pilot(ctx));
  CHECK(pilot.removed());
  const MissionEvents e = mission->sim().take_events();
  CHECK(e.pilots_rescued == 1);
  REQUIRE(e.crew_messages.size() == 1);
  CHECK(e.crew_messages[0].id == 67);
  mission->simulate_frame(5, false);
  CHECK_FALSE(boat.can_rescue_pilot());

  const PatrolLogEntry entry = rescue_log_entry(mission->sim().clock(), mission->world(), 9699328, 14942208);
  CHECK(entry.type == PatrolLogEntry::kRescuedPilot);
  CHECK(entry.latitude.degrees == 3);
  CHECK(entry.latitude.minutes == 3);
  CHECK(entry.latitude.hemisphere == 'S');
  CHECK(entry.tonnage == 0);
  CHECK(entry.name.empty());
}

}  // namespace sh
