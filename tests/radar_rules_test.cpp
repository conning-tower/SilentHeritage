// The extended radar rules (not in the original): the ST periscope radar
// (rule `st_radar`), the APR-1 radar detector (rule `apr1`) and the escorts'
// radar detectors (rule `detectable_radar`); with each rule off, the
// original's behaviour.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "assets/crew_messages.h"
#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/game_exe.h"
#include "assets/scenario.h"
#include "assets/shp.h"
#include "core/install.h"
#include "game/career_letters.h"
#include "game/command_bar.h"
#include "game/equipment_art.h"
#include "game/frontend_briefing.h"
#include "game/mission.h"
#include "game/mission_rules.h"
#include "game/mission_save.h"
#include "game/scengen.h"
#include "game/sim_state.h"
#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/player_sub.h"
#include "sim/radar.h"
#include "sim/radar_emissions.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim_classes.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;
constexpr std::int32_t kSecond = 0x10000;
// The options word: SJ (15000 yd), SD, the APR-1 bit.
constexpr std::uint32_t kSj = 0x10000;
constexpr std::uint32_t kSd = sensor_option::kSdRadar;

ShipAiClass destroyer() {
  ShipAiClass c;
  c.type = ShipType::kDestroyer;
  c.role = ShipRole::kEscort;
  c.screw = ScrewSound::kHighSpeed;
  c.length_ft = 388;
  c.draft_ft = 12;
  c.visual_size = 100;
  c.lookout_ft = 25;
  return c;
}

ShipAiClass freighter() {
  ShipAiClass c;
  c.type = static_cast<ShipType>(12);
  c.role = ShipRole::kMerchant;
  c.length_ft = 400;
  c.visual_size = 100;
  c.lookout_ft = 20;
  return c;
}

Ship& add_ship(Simulation& sim, const ShipAiClass& cls, fx::Vec3 at) {
  auto ship = std::make_unique<Ship>(test::fubuki(), at, Angle10(0), Fixed16(), ShipOrder::kNone);
  ship->ai().cls = cls;
  ship->setup().crew = 1;
  ship->setup().route.x[0] = at.x;
  ship->setup().route.z[0] = at.z;
  return sim.add(std::move(ship));
}

// A boat at `depth_ft` in a simulation dated `month` of `year` (since 1930).
struct Rig {
  Rig(std::uint32_t options, const MissionRules& rules, std::int32_t month, std::int32_t year,
      std::int32_t depth_ft = 0)
      : sim(conditions(options, rules), kDeepSea),
        boat(sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, -depth_ft * kUnitsPerFoot, kOrigin},
                                                 Angle10(0), 0))) {
    sim.clock().init(year, month, 1, 0, 0);
    boat.dive().depth_ft = depth_ft;
  }

  static MissionConditions conditions(std::uint32_t options, const MissionRules& rules) {
    MissionConditions c;
    c.options = options;
    c.rules = rules;
    return c;
  }

  std::vector<CrewReport> messages() { return sim.take_events().crew_messages; }

  Simulation sim;
  PlayerSub& boat;
};

MissionRules rules_with(bool MissionRules::* rule) {
  MissionRules r;
  r.*rule = true;
  return r;
}

bool has_message(const std::vector<CrewReport>& reports, std::int32_t id) {
  return std::any_of(reports.begin(), reports.end(), [id](const CrewReport& r) { return r.id == id; });
}

}  // namespace

// ------------------------------------------------------------------ dates

TEST_CASE("Japanese radar comes by ship type and date", "[radar_rules]") {
  // Capital ships: Type 21 from 8/42, Type 22 from 1/44.
  CHECK(ship_radar_bands(ShipType::kHeavyCruiser, 6, 12) == RadarBands{});
  CHECK(ship_radar_bands(ShipType::kHeavyCruiser, 7, 12) == RadarBands{true, false});
  CHECK(ship_radar_bands(ShipType::kBattleship, 0, 14) == RadarBands{true, true});
  // Destroyers: 1/44 and 6/44; escorts 6/44 and 9/44; submarines 7/44.
  CHECK(ship_radar_bands(ShipType::kDestroyer, 11, 13) == RadarBands{});
  CHECK(ship_radar_bands(ShipType::kDestroyer, 0, 14) == RadarBands{true, false});
  CHECK(ship_radar_bands(ShipType::kDestroyer, 5, 14) == RadarBands{true, true});
  CHECK(ship_radar_bands(ShipType::kKaibokan, 4, 14) == RadarBands{});
  CHECK(ship_radar_bands(ShipType::kDestroyerEscort, 5, 14) == RadarBands{true, false});
  CHECK(ship_radar_bands(ShipType::kKaibokan, 8, 14) == RadarBands{true, true});
  CHECK(ship_radar_bands(ShipType::kSubmarine, 5, 14) == RadarBands{});
  CHECK(ship_radar_bands(ShipType::kSubmarine, 6, 14) == RadarBands{true, true});
  // Merchants and sampans never.
  CHECK(ship_radar_bands(static_cast<ShipType>(12), 7, 15) == RadarBands{});
  CHECK(ship_radar_bands(ShipType::kSampan, 7, 15) == RadarBands{});
  // Aircraft: the H-6 on the bombers from 1/44.
  CHECK(aircraft_radar_bands(AircraftRole::kLevelBomber, 11, 13) == RadarBands{});
  CHECK(aircraft_radar_bands(AircraftRole::kLevelBomber, 0, 14) == RadarBands{true, false});
  CHECK(aircraft_radar_bands(AircraftRole::kTorpedoBomber, 0, 15) == RadarBands{true, false});
  CHECK(aircraft_radar_bands(AircraftRole::kFighter, 0, 15) == RadarBands{});
}

TEST_CASE("The radio horizon grows with the square roots of the antenna heights", "[radar_rules]") {
  CHECK(radio_horizon_yd(100, 100) == 49800);
  CHECK(radio_horizon_yd(1, 1) == 4980);
  CHECK(radio_horizon_yd(0, -5) == 4980);
  CHECK(radio_horizon_yd(64, 1) == 2490 * 9);
}

TEST_CASE("The rules read from EXTENDED.CFG", "[radar_rules]") {
  const MissionRules r = parse_mission_rules_config("rule.st_radar = 1\nrule.apr1 = 1\nrule.detectable_radar = 1\n");
  CHECK(r.st_radar);
  CHECK(r.apr1);
  CHECK(r.detectable_radar);
  CHECK_FALSE(r.fix_obvious_errors);
  CHECK(decode_mission_rules(encode_mission_rules(r)) == r);
}

// --------------------------------------------------------------- ST radar

TEST_CASE("With the ST the SJ works below radar depth with the periscope raised", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  // At 60 ft: below the radar depth (48 ft), at periscope depth (65 ft).
  for (const bool st : {false, true}) {
    Rig rig(kSj | (st ? sensor_option::kStPeriscopeRadar : 0u), MissionRules{}, 0, 15, 60);
    rig.boat.set_periscope_raised(true);
    SimContext ctx = rig.sim.context();
    RadarSets& radar = rig.sim.sensors().radar();
    radar.init(ctx, rig.boat, rig.sim.sensors().environment());
    // radar_init switches it on when it can (A-scope without the PPI).
    CHECK((radar.sj().mode == SjMode::kAScope) == st);
    CHECK(sj_through_periscope(rig.boat, ctx.env.options) == st);
  }

  Rig rig(kSj | sensor_option::kStPeriscopeRadar, MissionRules{}, 0, 15, 60);
  SimContext ctx = rig.sim.context();
  RadarSets& radar = rig.sim.sensors().radar();
  SensorEnvironment& env = rig.sim.sensors().environment();
  env.auto_sj = true;
  // Periscope down: refused.
  radar.init(ctx, rig.boat, env);
  CHECK(radar.sj().mode == SjMode::kOff);
  radar.set_sj_power(ctx, rig.boat, true, false, false, false);
  CHECK(radar.sj().mode == SjMode::kOff);
  // A sweep that cannot transmit arms the auto SJ.
  radar.update(ctx, rig.boat, env, kSecond);
  // Raised: the auto SJ switches it on, with the SJ's own report.
  rig.boat.set_periscope_raised(true);
  rig.messages();
  radar.update(ctx, rig.boat, env, kSecond);
  CHECK(radar.sj().mode == SjMode::kAScope);
  CHECK(has_message(rig.messages(), 43));
  // Lowered after the warm-up: secured, reported.
  for (int i = 0; i < 11; ++i) {
    radar.update(ctx, rig.boat, env, kSecond);
  }
  rig.boat.set_periscope_raised(false);
  radar.update(ctx, rig.boat, env, kSecond);
  CHECK(radar.sj().mode == SjMode::kOff);
  CHECK(has_message(rig.messages(), 44));
  // At radar depth it works with the periscope down, as without the ST.
  rig.boat.dive().depth_ft = 40;
  CHECK_FALSE(sj_through_periscope(rig.boat, ctx.env.options));
  radar.update(ctx, rig.boat, env, kSecond);
  CHECK(radar.sj().mode == SjMode::kAScope);
}

TEST_CASE("Through the ST the SJ sees a destroyer from periscope depth", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  for (const bool st : {false, true}) {
    Rig rig(kSj | 0x80u | (st ? sensor_option::kStPeriscopeRadar : 0u), MissionRules{}, 0, 15, 60);
    rig.sim.sensors().environment().fog_pct = 100;
    rig.boat.set_periscope_raised(true);
    add_ship(rig.sim, destroyer(), {kOrigin, 0, kOrigin + 10000 * kYard});
    SimContext ctx = rig.sim.context();
    RadarSets& radar = rig.sim.sensors().radar();
    radar.init(ctx, rig.boat, rig.sim.sensors().environment());
    for (int i = 0; i < 50; ++i) {
      radar.update(ctx, rig.boat, rig.sim.sensors().environment(), 0x3334);
    }
    CHECK(radar.sj().blips.size() == (st ? 1u : 0u));
  }
}

TEST_CASE("A single mission fits the ST from November 1944 with its rule", "[radar_rules]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const auto options = [&](std::int32_t month, std::int32_t year, std::uint32_t equipment, bool rule) {
    Scenario s = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
    s.header.month = month;
    s.header.year = year;
    s.header.equipment = equipment;
    MissionSetup setup;
    setup.rules.st_radar = rule;
    return Mission::create(install, tables, std::move(s), setup)->sim().conditions().options;
  };
  const auto sj = static_cast<std::uint32_t>(Equipment::kSj1Radar);
  CHECK((options(10, 14, sj, true) & sensor_option::kStPeriscopeRadar) != 0);
  CHECK((options(9, 14, sj, true) & sensor_option::kStPeriscopeRadar) == 0);
  // Not without an SJ, nor without the rule.
  CHECK((options(10, 14, 0, true) & sensor_option::kStPeriscopeRadar) == 0);
  CHECK((options(10, 14, sj, false) & sensor_option::kStPeriscopeRadar) == 0);
  // The ST's bit fits it at any date.
  const auto st = static_cast<std::uint32_t>(Equipment::kStRadar);
  CHECK((options(0, 13, sj | st, true) & sensor_option::kStPeriscopeRadar) != 0);
  // Without the rule the bit changes nothing in the options word.
  CHECK(options(0, 13, sj | st, false) == options(0, 13, sj, false));
}

// ---------------------------------------------------------------- APR-1

TEST_CASE("The APR-1 hears radars within their range and the radio horizon", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  Rig rig(kSd | sensor_option::kApr1Detector, rules_with(&MissionRules::apr1), 5, 14);
  SensorEnvironment& env = rig.sim.sensors().environment();
  env.light_pct = 20;  // Night: the ships transmit.
  Ship& dd = add_ship(rig.sim, destroyer(), {kOrigin, 0, kOrigin + 15000 * kYard});
  add_ship(rig.sim, freighter(), {kOrigin + 2000 * kYard, 0, kOrigin});
  SimContext ctx = rig.sim.context();
  const std::int32_t mast_ft = (test::gato().periscope_height_ft * kUnitsPerFoot) / kUnitsPerFoot;
  const std::int32_t limit =
      std::min(20000 * 3 / 2, radio_horizon_yd(std::max(1, dd.visual_height() / kUnitsPerFoot), mast_ft));
  REQUIRE(limit > 15000);
  const Apr1Signal heard = apr1_listen(ctx, rig.boat, env);
  // June 1944: the destroyer has both bands; the freighter nothing.
  CHECK(heard.bands == RadarBands{true, true});
  CHECK(heard.strength == std::min(3, 1 + 3 * (limit - 15000) / limit));
  // The strength in words.
  CHECK(apr1_report_suffix({{true, false}, 1}) == ": faint");
  CHECK(apr1_report_suffix({{true, false}, 2}) == ": fair");
  CHECK(apr1_report_suffix({{false, true}, 3}) == ": strong");

  // By day an unalerted ship keeps silent; alerted, it transmits.
  env.light_pct = 100;
  CHECK(apr1_listen(ctx, rig.boat, env).strength == 0);
  dd.alert().level = 1;
  CHECK(apr1_listen(ctx, rig.boat, env).strength == heard.strength);
  // Below radar depth, or with the SD damaged, nothing.
  rig.boat.dive().depth_ft = 50;
  CHECK(apr1_listen(ctx, rig.boat, env).strength == 0);
  rig.boat.dive().depth_ft = 0;
  rig.boat.systems()[static_cast<std::size_t>(PlayerSystem::kSdRadar)].flags = 1;
  CHECK(apr1_listen(ctx, rig.boat, env).strength == 0);
  rig.boat.systems()[static_cast<std::size_t>(PlayerSystem::kSdRadar)].flags = 0;
  // Out of range: nothing.
  dd.motion().position.z = kOrigin + (limit + 100) * kYard;
  CHECK(apr1_listen(ctx, rig.boat, env).strength == 0);
}

TEST_CASE("The APR-1's operator reports new and stronger transmissions", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  Rig rig(kSd | sensor_option::kApr1Detector, rules_with(&MissionRules::apr1), 5, 14);
  SensorEnvironment& env = rig.sim.sensors().environment();
  env.light_pct = 20;
  Ship& dd = add_ship(rig.sim, destroyer(), {kOrigin, 0, kOrigin + 20000 * kYard});
  SimContext ctx = rig.sim.context();
  Apr1Receiver& apr1 = rig.sim.sensors().apr1();
  const std::uint32_t seed = rig.sim.random().state();
  rig.sim.clock().double_compression();

  apr1.update(ctx, rig.boat, env, kSecond);
  std::vector<CrewReport> said = rig.messages();
  REQUIRE(said.size() == 1);
  CHECK(said[0].id == 61);
  CHECK(said[0].text == apr1_report_suffix(apr1_listen(ctx, rig.boat, env)));
  CHECK(rig.sim.clock().requested_compression() == 1);
  const std::int32_t first = apr1.state().reported;
  CHECK(first > 0);
  // The same strength is not reported again.
  apr1.update(ctx, rig.boat, env, 40 * kSecond);
  CHECK(rig.messages().empty());
  // Closer: stronger, reported (30 s after the last report).
  dd.motion().position.z = kOrigin + 3000 * kYard;
  apr1.update(ctx, rig.boat, env, kSecond);
  said = rig.messages();
  REQUIRE(said.size() == 1);
  CHECK(apr1.state().reported > first);
  // A minute of silence, then heard again: reported.
  env.light_pct = 100;
  apr1.update(ctx, rig.boat, env, 61 * kSecond);
  CHECK(apr1.state().reported == 0);
  env.light_pct = 20;
  apr1.update(ctx, rig.boat, env, kSecond);
  CHECK(rig.messages().size() == 1);
  // No draws from the game's generator.
  CHECK(rig.sim.random().state() == seed);
}

TEST_CASE("Without its rule or its fit the APR-1 says nothing", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  for (const auto& [options, rules] : {std::pair{kSd | sensor_option::kApr1Detector, MissionRules{}},
                                       std::pair{kSd, rules_with(&MissionRules::apr1)}}) {
    Rig rig(options, rules, 5, 14);
    rig.sim.sensors().environment().light_pct = 20;
    add_ship(rig.sim, destroyer(), {kOrigin, 0, kOrigin + 5000 * kYard});
    SimContext ctx = rig.sim.context();
    Apr1Receiver& apr1 = rig.sim.sensors().apr1();
    apr1.update(ctx, rig.boat, rig.sim.sensors().environment(), kSecond);
    CHECK(rig.messages().empty());
    CHECK(apr1.state() == Apr1Receiver::State{});
  }
}

TEST_CASE("The APR-1 report shows its suffix after the original text", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  const CrewMessages messages(game_exe());
  CHECK(format_crew_message(messages, {61, -1, -1, ": strong"}) ==
        "Radar Operator: Radar emissions detected on APR-1: strong");
  // As the original: without a text, and a text in its place.
  CHECK(format_crew_message(messages, {61, -1, -1, ""}) == "Radar Operator: Radar emissions detected on APR-1");
  const CrewMessage* damaged = nullptr;
  for (const CrewMessage& m : messages.all()) {
    if (m.text.find('\x03') != std::string::npos) {
      damaged = &m;
      break;
    }
  }
  REQUIRE(damaged != nullptr);
  const std::string text = format_crew_message(messages, {damaged->id, -1, -1, "SD Radar"});
  CHECK(text.find("SD Radar") != std::string::npos);
  CHECK(text.size() == damaged->speaker_name.size() + 2 + damaged->text.size() - 1 + 8);
}

// ------------------------------------------------- escort radar detectors

TEST_CASE("Escorts carry radar detectors from 1944, not all of them", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  Simulation sim(MissionConditions{}, kDeepSea);
  std::vector<Ship*> escorts;
  for (int i = 0; i < 400; ++i) {
    escorts.push_back(&add_ship(sim, destroyer(), {kOrigin + i * 1777 * kYard, 0, kOrigin + i * 911 * kYard}));
  }
  const auto count = [&](std::int32_t month, std::int32_t year) {
    return std::count_if(escorts.begin(), escorts.end(),
                         [&](const Ship* s) { return escort_has_radar_detector(*s, month, year); });
  };
  CHECK(count(11, 13) == 0);
  CHECK(count(0, 14) > 100);
  CHECK(count(0, 14) < 170);
  CHECK(count(6, 14) > 230);
  CHECK(count(6, 14) < 300);
  CHECK(count(0, 15) > 270);
  CHECK(count(0, 15) < 330);
  // Fixed per ship: the 1944 fits stay fitted.
  for (const Ship* s : escorts) {
    if (escort_has_radar_detector(*s, 0, 14)) {
      CHECK(escort_has_radar_detector(*s, 0, 15));
    }
  }
  // Merchants never.
  const Ship& merchant = add_ship(sim, freighter(), {kOrigin, 0, kOrigin});
  CHECK_FALSE(escort_has_radar_detector(merchant, 0, 15));
}

namespace {

// Returns an escort with a detector in early 1945, 10000 yd north of the boat.
Ship& detecting_escort(Rig& rig) {
  for (int i = 0; i < 100; ++i) {
    Ship& s = add_ship(rig.sim, destroyer(), {kOrigin + i * 16, 0, kOrigin + 10000 * kYard});
    if (escort_has_radar_detector(s, 1, 15)) {
      return s;
    }
    s.remove();
  }
  FAIL("no escort with a detector");
  throw;
}

}  // namespace

TEST_CASE("A detector's bearing is off by a fixed error, up to 45 degrees at first", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  Simulation sim(MissionConditions{}, kDeepSea);
  const Ship& a = add_ship(sim, destroyer(), {kOrigin, 0, kOrigin});
  const Ship& b = add_ship(sim, destroyer(), {kOrigin + 5000 * kYard, 0, kOrigin});
  std::vector<std::int32_t> seen;
  for (std::int32_t minute = 0; minute < 600; minute += 5) {
    const std::int32_t e = detector_bearing_error(a, minute, 0);
    CHECK(e >= -450);
    CHECK(e <= 450);
    // The same for the 5 minutes of a window.
    CHECK(detector_bearing_error(a, minute + 4, 0) == e);
    seen.push_back(e);
  }
  // It changes from window to window and from ship to ship.
  std::sort(seen.begin(), seen.end());
  CHECK(std::unique(seen.begin(), seen.end()) - seen.begin() > 100);
  CHECK(seen.front() < -350);
  CHECK(seen.back() > 350);
  int differ = 0;
  for (std::int32_t minute = 0; minute < 100; minute += 5) {
    differ += detector_bearing_error(a, minute, 0) != detector_bearing_error(b, minute, 0) ? 1 : 0;
  }
  CHECK(differ > 15);
}

TEST_CASE("The detectors' bearing narrows while the boat's radar keeps transmitting", "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  // 45 deg at first, 10 deg after 3 minutes heard, evenly in between.
  CHECK(detector_error_limit(0) == 450);
  CHECK(detector_error_limit(90 * kSecond) == 275);
  CHECK(detector_error_limit(180 * kSecond) == 100);
  CHECK(detector_error_limit(600 * kSecond) == 100);
  Rig rig(kSd, rules_with(&MissionRules::detectable_radar), 1, 15);
  Ship& escort = detecting_escort(rig);
  SimContext ctx = rig.sim.context();
  EscortDetectors& detectors = rig.sim.sensors().escort_detectors();
  RadarSets& radar = rig.sim.sensors().radar();
  radar.sd().on = true;
  const std::int32_t minute = ctx.clock.minutes_elapsed();
  // The error of the same ship and window shrinks with the time heard.
  const std::int32_t first = std::abs(detector_bearing_error(escort, minute, 0));
  const std::int32_t later = std::abs(detector_bearing_error(escort, minute, 120 * kSecond));
  const std::int32_t settled = std::abs(detector_bearing_error(escort, minute, 180 * kSecond));
  CHECK(later <= first);
  CHECK(settled <= later);
  CHECK(settled <= 100);
  // Two minutes of SD heard, a second at a time.
  for (int i = 0; i < 120; ++i) {
    detectors.update(ctx, rig.boat, radar, rig.sim.sensors().environment(), kSecond);
  }
  CHECK(detectors.state().heard == 120 * kSecond);
  // The escort's estimate, refreshed now, is on the narrower bearing.
  escort.ai().contact.age = -1;
  detectors.update(ctx, rig.boat, radar, rig.sim.sensors().environment(), kSecond);
  const fx::Vec3 at = escort.position();
  const fx::Vec3 guess = escort.ai().contact.position;
  const fx::Vec3 boat = rig.boat.position();
  std::int32_t off = fx::angle_sub(fx::heading_from_delta(guess.x - at.x, guess.z - at.z),
                                   fx::heading_from_delta(boat.x - at.x, boat.z - at.z))
                         .tenths();
  off = off > 1800 ? off - 3600 : off;
  CHECK(std::abs(off - detector_bearing_error(escort, minute, 120 * kSecond)) <= 5);
  // Capped at 3 minutes; reset as soon as it stops.
  for (int i = 0; i < 100; ++i) {
    detectors.update(ctx, rig.boat, radar, rig.sim.sensors().environment(), kSecond);
  }
  CHECK(detectors.state().heard == 180 * kSecond);
  radar.sd().on = false;
  detectors.update(ctx, rig.boat, radar, rig.sim.sensors().environment(), kSecond);
  CHECK(detectors.state().heard == 0);
}

TEST_CASE("An escort with a detector searches along a coarse bearing for the boat's SD or SJ, not the ST",
          "[radar_rules]") {
  SH_REQUIRE_GAME_EXE();
  const MissionRules rules = rules_with(&MissionRules::detectable_radar);
  {
    // SD on: heard; the escort is alerted and searches toward a point at
    // half the SD's hearing range (26400 yd) on the bearing, off by its
    // error: never the boat's position.
    Rig rig(kSd, rules, 1, 15);
    Ship& escort = detecting_escort(rig);
    SimContext ctx = rig.sim.context();
    rig.sim.sensors().radar().sd().on = true;
    const std::uint32_t seed = rig.sim.random().state();
    rig.sim.sensors().escort_detectors().update(ctx, rig.boat, rig.sim.sensors().radar(), rig.sim.sensors().environment(), kSecond);
    CHECK(escort.alert().level == 1);
    CHECK(escort.ai().contact.age == 0);
    CHECK(escort.ai().contact.vehicle == &rig.boat);
    const fx::Vec3 at = escort.position();
    const fx::Vec3 guess = escort.ai().contact.position;
    const fx::Vec3 boat = rig.boat.position();
    CHECK(guess != boat);
    const std::int32_t off = fx::angle_sub(fx::heading_from_delta(guess.x - at.x, guess.z - at.z),
                                           fx::heading_from_delta(boat.x - at.x, boat.z - at.z))
                                 .tenths();
    const std::int32_t folded = off > 1800 ? off - 3600 : off;
    CHECK(std::abs(folded - detector_bearing_error(escort, ctx.clock.minutes_elapsed(), 0)) <= 5);
    // Heard: the boat's radar is now heard without a break.
    CHECK(rig.sim.sensors().escort_detectors().state().heard == kSecond);
    const auto reach = static_cast<std::int32_t>(fx::approx_distance(guess.x - at.x, guess.z - at.z) / kYard);
    CHECK(std::abs(reach - 13200) < 700);
    CHECK(rig.sim.random().state() == seed);
    // A fresh contact is not replaced for 30 s.
    escort.ai().contact.age = 0x100000;
    escort.ai().contact.position = {};
    rig.sim.sensors().escort_detectors().update(ctx, rig.boat, rig.sim.sensors().radar(), rig.sim.sensors().environment(), kSecond);
    CHECK(escort.ai().contact.position == fx::Vec3{});
  }
  for (const auto& [month, year, depth, raised, heard] :
       {std::tuple{5, 14, 0, false, false}, std::tuple{6, 14, 0, false, true}, std::tuple{1, 15, 60, true, false}}) {
    // The SJ alone: heard from July 1944; through the ST never.
    Rig rig(kSj | sensor_option::kStPeriscopeRadar, rules, month, year, depth);
    rig.boat.set_periscope_raised(raised);
    Ship& escort = detecting_escort(rig);
    SimContext ctx = rig.sim.context();
    rig.sim.sensors().radar().sj().mode = SjMode::kAScope;
    rig.sim.sensors().escort_detectors().update(ctx, rig.boat, rig.sim.sensors().radar(), rig.sim.sensors().environment(), kSecond);
    CHECK((escort.ai().contact.age == 0) == heard);
  }
  {
    // Without the rule nothing is heard.
    Rig rig(kSd, MissionRules{}, 1, 15);
    Ship& escort = detecting_escort(rig);
    SimContext ctx = rig.sim.context();
    rig.sim.sensors().radar().sd().on = true;
    rig.sim.sensors().escort_detectors().update(ctx, rig.boat, rig.sim.sensors().radar(), rig.sim.sensors().environment(), kSecond);
    CHECK(escort.alert().level == 0);
    CHECK(escort.ai().contact.age == -1);
  }
}

// ------------------------------------------------------- the career and the save

TEST_CASE("The extended deliveries come by their rules and dates", "[radar_rules]") {
  CareerRecord r;
  r.month = 10;
  r.year = 14;
  CHECK(extended_equipment_entries(MissionRules{}).empty());
  CHECK_FALSE(extended_equipment_due(r, 1, MissionRules{}));
  CHECK(extended_equipment_on_date(r, MissionRules{}) == 0);

  MissionRules both;
  both.apr1 = true;
  both.st_radar = true;
  const std::vector<EquipmentEntry> entries = extended_equipment_entries(both);
  REQUIRE(entries.size() == 2);
  CHECK(entries[0].name == "APR-1 Radar Detector");
  CHECK(entries[0].sprite == 5);
  CHECK(entries[1].name == "ST Radar");
  CHECK(entries[1].sprite == kStRadarPicture);
  // ComSubPac: on the date; elsewhere three months later.
  r.month = 4;
  CHECK(extended_equipment_due(r, 1, both)->bit == static_cast<std::uint32_t>(Equipment::kApr1Detector));
  CHECK_FALSE(extended_equipment_due(r, 2, both));
  r.month = 7;
  CHECK(extended_equipment_due(r, 2, both));
  r.equipment = static_cast<std::uint32_t>(Equipment::kApr1Detector);
  CHECK_FALSE(extended_equipment_due(r, 1, both));
  r.month = 10;
  CHECK(extended_equipment_due(r, 1, both)->bit == static_cast<std::uint32_t>(Equipment::kStRadar));
  // A new career of December 1944 starts with both.
  r.equipment = 0;
  r.month = 11;
  CHECK(extended_equipment_on_date(r, both) ==
        (static_cast<std::uint32_t>(Equipment::kApr1Detector) | static_cast<std::uint32_t>(Equipment::kStRadar)));
  CHECK(extended_equipment_on_date(r, rules_with(&MissionRules::apr1)) ==
        static_cast<std::uint32_t>(Equipment::kApr1Detector));
}

TEST_CASE("The original's equipment dates are unchanged", "[radar_rules][content]") {
  SH_REQUIRE_GAME_EXE();
  // The APR-1's bit from 5/44 as before; never the ST's.
  CHECK((equipment_on_date(3, 14) & static_cast<std::uint32_t>(Equipment::kApr1Detector)) == 0);
  CHECK((equipment_on_date(4, 14) & static_cast<std::uint32_t>(Equipment::kApr1Detector)) != 0);
  CHECK((equipment_on_date(7, 15) & static_cast<std::uint32_t>(Equipment::kStRadar)) == 0);
}

TEST_CASE("The ST's picture is the SD's with a periscope head and an ST label", "[radar_rules]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp equip = Shp::load(install, "ART/EQUIP.SHP");
  const Image8 st = st_radar_picture(equip);
  const DecodedFrame sd = equip.frame(3).decode();
  REQUIRE(st.width() == sd.image.width());
  REQUIRE(st.height() == sd.image.height());
  const std::uint8_t sea = sd.image.at(20, 20);
  // The SD's dipole arms (rows 31..56 beside the mast) are gone; the mast
  // below row 72 and the frame are kept.
  const auto beside_mast = [sea](const Image8& image) {
    int n = 0;
    for (int y = 31; y <= 56; ++y) {
      for (int x = 8; x < 142; ++x) {
        n += (x < 45 || x > 70) && image.at(x, y) != sea ? 1 : 0;
      }
    }
    return n;
  };
  CHECK(beside_mast(sd.image) > 100);
  CHECK(beside_mast(st) == 0);
  for (int y = 72; y < 140; y += 7) {
    CHECK(st.at(57, y) == sd.image.at(57, y));
  }
  CHECK(st.at(2, 2) == sd.image.at(2, 2));
  // The periscope head with its dark window.
  CHECK(st.at(57, 19) == sd.image.at(51, 100));
  CHECK(st.at(52, 20) != sea);
  CHECK(st.at(50, 20) == sea);
  // The label: "S" kept, the "D" became a "T" (its bar spans 6 columns).
  CHECK(st.at(51, 160) == sd.image.at(51, 160));
  for (int x = 56; x < 62; ++x) {
    CHECK(st.at(x, 157) != sea);
  }
  CHECK(st.at(56, 165) == sea);
}

TEST_CASE("The APR-1's state is saved with its rule only", "[radar_rules]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const auto save = [&](const MissionRules& rules) {
    Scenario s = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
    s.header.equipment |= static_cast<std::uint32_t>(Equipment::kApr1Detector);
    MissionSetup setup;
    setup.random = WatcomRandom(3);
    setup.rules = rules;
    std::unique_ptr<Mission> m = Mission::create(install, tables, std::move(s), setup);
    m->sim().sensors().apr1().state() = {0x120000, 0x50000, 3};
    return capture_mission_save(install, *m, 0, "Radar", {}).serialize();
  };
  const std::vector<std::uint8_t> plain_file = save(MissionRules{});
  const std::vector<std::uint8_t> apr1_file = save(rules_with(&MissionRules::apr1));
  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(apr1_file);
  REQUIRE(parsed);
  MissionSetup setup;
  setup.random = WatcomRandom(1);
  const std::unique_ptr<Mission> back = restore_mission(install, tables, *parsed, setup);
  CHECK(back->sim().conditions().rules.apr1);
  CHECK(back->sim().sensors().apr1().state() == Apr1Receiver::State{0x120000, 0x50000, 3});
  // Without the rule nothing of it is saved: a restore starts afresh.
  const std::optional<MissionSaveFile> plain_parsed = MissionSaveFile::parse(plain_file);
  REQUIRE(plain_parsed);
  const std::unique_ptr<Mission> plain_back = restore_mission(install, tables, *plain_parsed, setup);
  CHECK(plain_back->sim().sensors().apr1().state() == Apr1Receiver::State{});
}

TEST_CASE("The briefing lists the APR-1 and the ST after the original's equipment", "[radar_rules]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const ExeImage exe = ExeImage::load(install);
  const ui::MenuResources menu = ui::MenuResources::load(install);
  const auto briefing = [&](const MissionRules& rules) {
    Scenario s = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01");
    s.header.month = 11;
    s.header.year = 14;
    s.header.equipment = static_cast<std::uint32_t>(Equipment::kSj1Radar) |
                         static_cast<std::uint32_t>(Equipment::kApr1Detector);
    MissionSetup setup;
    setup.rules = rules;
    const MissionBriefing b = mission_briefing_facts(*Mission::create(install, tables, std::move(s), setup));
    return std::tuple{b.apr1, b.st_radar, mission_briefing_text(b, exe, menu.small)};
  };
  MissionRules both;
  both.apr1 = true;
  both.st_radar = true;
  const auto [apr1, st, text] = briefing(both);
  CHECK(apr1);
  CHECK(st);
  const std::size_t at_apr1 = text.find("APR-1 Radar Detector\n");
  REQUIRE(at_apr1 != std::string::npos);
  CHECK(text.find("ST Periscope Radar\n") > at_apr1);
  CHECK(text.find("Improved SJ Radar (SJ-1)") < at_apr1);
  // As the original without the rules.
  const auto [plain_apr1, plain_st, plain_text] = briefing(MissionRules{});
  CHECK_FALSE(plain_apr1);
  CHECK_FALSE(plain_st);
  CHECK(plain_text.find("APR-1") == std::string::npos);
  CHECK(plain_text.find("ST Periscope") == std::string::npos);
}

}  // namespace sh
