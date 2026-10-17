// The extended fathometer soundings (rule `fathometer_soundings`) and
// REALISTIC DEPTHS (rule `realistic_depths`); with each rule off, the
// original's behaviour.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "game/class_params.h"
#include "game/frontend.h"
#include "game/mission.h"
#include "game/mission_rules.h"
#include "game/mission_save.h"
#include "game/options_realism.h"
#include "game/scengen.h"
#include "game/stations/gauges.h"
#include "game_data.h"
#include "sim/ai_ship.h"
#include "sim/effects.h"
#include "sim/fathometer.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim_classes.h"
#include "stations_control_fixture.h"

namespace sh {

namespace {

const FlatSeaFloor kDeepSea(-3000 * kUnitsPerFoot);
constexpr std::int32_t kYard = kUnitsPerYard;
constexpr std::int32_t kOrigin = 4000000;
constexpr std::int32_t kSfxPing = 0x32;

const MissionRules kSoundings{.fix_obvious_errors = true, .fathometer_soundings = true};

ShipAiClass destroyer() {
  ShipAiClass c;
  c.type = ShipType::kDestroyer;
  c.role = ShipRole::kEscort;
  c.screw = ScrewSound::kHighSpeed;
  c.has_sonar = true;
  c.length_ft = 388;
  c.draft_ft = 12;
  c.visual_size = 100;
  c.lookout_ft = 25;
  return c;
}

// A boat 100 ft down at (kOrigin, kOrigin) over 400 ft of water.
struct Rig {
  explicit Rig(const MissionRules& rules, std::int32_t thermal_layer_ft = 10000)
      : sim(conditions(rules, thermal_layer_ft), kDeepSea),
        boat(sim.add(std::make_unique<PlayerSub>(test::gato(), fx::Vec3{kOrigin, -100 * kUnitsPerFoot, kOrigin},
                                                 Angle10(0), 0))) {
    boat.dive().depth_ft = 100;
    boat.motion().keel_clearance = -400 * kUnitsPerFoot;
  }

  static MissionConditions conditions(const MissionRules& rules, std::int32_t thermal_layer_ft) {
    MissionConditions c;
    c.rules = rules;
    c.thermal_layer_ft = thermal_layer_ft;
    return c;
  }

  // An escort (crew 1: 2800 yd of echo ranging) `yards` south of the boat,
  // heading `heading`.
  Ship& escort(std::int32_t yards, Angle10 heading = Angle10(0)) {
    auto ship = std::make_unique<Ship>(test::fubuki(), fx::Vec3{kOrigin, 0, kOrigin - yards * kYard}, heading,
                                       Fixed16(), ShipOrder::kNone);
    ship->ai().cls = destroyer();
    ship->setup().crew = 1;
    return sim.add(std::move(ship));
  }

  // Asks for a sounding and takes it.
  void sound() {
    Fathometer& f = sim.sensors().fathometer();
    f.request();
    SimContext ctx = sim.context();
    f.update(ctx, boat);
  }

  Simulation sim;
  PlayerSub& boat;
};

bool alerted(const Ship& s) { return s.alert().level == 1 && s.ai().contact.age == 0; }

}  // namespace

// ------------------------------------------------------------- soundings

TEST_CASE("A sounding holds the depth under the keel until the next one", "[soundings]") {
  Rig rig(kSoundings);
  const Fathometer& f = rig.sim.sensors().fathometer();
  CHECK_FALSE(f.sounded_keel_clearance());
  const std::uint32_t seed = rig.sim.random().state();
  rig.sound();
  CHECK(f.sounded_keel_clearance() == -400 * kUnitsPerFoot);
  CHECK(fathometer_feet(*f.sounded_keel_clearance()) == 400);
  CHECK(f.state().soundings == 1);
  // The sounder's ping, full volume; no random draws.
  const MissionEvents events = rig.sim.take_events();
  CHECK(events.sfx == std::vector<MissionEvents::Sfx>{{kSfxPing, -1}});
  CHECK(rig.sim.random().state() == seed);
  // The bottom shoals: the reading stays until the next sounding.
  rig.boat.motion().keel_clearance = -50 * kUnitsPerFoot;
  SimContext ctx = rig.sim.context();
  rig.sim.sensors().fathometer().update(ctx, rig.boat);
  CHECK(f.sounded_keel_clearance() == -400 * kUnitsPerFoot);
  rig.sound();
  CHECK(f.sounded_keel_clearance() == -50 * kUnitsPerFoot);
  CHECK(f.state().soundings == 2);
}

TEST_CASE("Without the rule a sounding is never taken nor heard", "[soundings]") {
  for (const MissionRules& rules : {MissionRules{}, MissionRules{.fix_obvious_errors = true}}) {
    Rig rig(rules);
    Ship& escort = rig.escort(1000);
    rig.sound();
    CHECK(rig.sim.sensors().fathometer().state() == Fathometer::State{});
    CHECK(rig.sim.take_events().sfx.empty());
    CHECK_FALSE(alerted(escort));
  }
}

TEST_CASE("The simulation takes the sounding asked for in the boat's update", "[soundings]") {
  Rig rig(kSoundings);
  rig.sim.sensors().fathometer().request();
  rig.sim.step(10);
  CHECK(rig.sim.sensors().fathometer().state().soundings == 1);
  rig.sim.step(10);
  CHECK(rig.sim.sensors().fathometer().state().soundings == 1);
}

TEST_CASE("The escorts hear a sounding within their echo-ranging range", "[soundings]") {
  Rig rig(kSoundings);
  Ship& near = rig.escort(2000);
  Ship& far = rig.escort(3000);
  // (crew + 1) x 400 + 2000 yd, an eighth less per sea state.
  CHECK(sonar_active_range_yd(near, 0) == 2800);
  CHECK(sonar_active_range_yd(near, 2) == 2100);
  rig.sound();
  CHECK(alerted(near));
  CHECK_FALSE(alerted(far));
  // On the true bearing, at half the range: never the boat's position.
  CHECK(near.ai().contact.vehicle == &rig.boat);
  const fx::Vec3 guess = near.ai().contact.position;
  CHECK(guess.x == kOrigin);
  CHECK(std::abs(guess.z - (kOrigin - 600 * kYard)) <= kYard);
}

TEST_CASE("No escort hears a sounding under the layer, in its baffles or deafened by a blast", "[soundings]") {
  {
    // Under the thermal layer.
    Rig rig(kSoundings, 50);
    Ship& escort = rig.escort(1000);
    rig.sound();
    CHECK_FALSE(alerted(escort));
    CHECK(rig.sim.sensors().fathometer().state().soundings == 1);
  }
  {
    // Heading away: the boat astern, in its own screws' noise.
    Rig rig(kSoundings);
    Ship& escort = rig.escort(1000, Angle10(1800));
    rig.sound();
    CHECK_FALSE(alerted(escort));
  }
  for (const bool keen : {false, true}) {
    // A depth charge's blast 1000 yd from the escort (within its 2400 yd):
    // deafened, unless UNDEAFENED ESCORTS; the sounding is taken anyway.
    MissionRules rules = kSoundings;
    rules.keen_escort_sonar = keen;
    Rig rig(rules);
    Ship& escort = rig.escort(1000);
    rig.sim.add(std::make_unique<UnderwaterBlast>(fx::Vec3{kOrigin + 1000 * kYard, 0, kOrigin - 1000 * kYard}));
    CHECK(sonar_deafened(escort, rig.sim.context()) == !keen);
    rig.sound();
    CHECK(alerted(escort) == keen);
    CHECK(rig.sim.sensors().fathometer().state().soundings == 1);
  }
  {
    // A blast farther than 2400 yd from the escort masks nothing.
    Rig rig(kSoundings);
    Ship& escort = rig.escort(1000);
    rig.sim.add(std::make_unique<UnderwaterBlast>(fx::Vec3{kOrigin + 3000 * kYard, 0, kOrigin - 1000 * kYard}));
    rig.sound();
    CHECK(alerted(escort));
  }
}

TEST_CASE("The fathometer's sounding is saved with its rule", "[soundings][save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.random = WatcomRandom(7);
  setup.rules = MissionRules::extended();
  std::unique_ptr<Mission> m = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
  m->simulate_frame(10, false);
  m->sim().sensors().fathometer().request();
  m->simulate_frame(10, false);
  const Fathometer::State sounded = m->sim().sensors().fathometer().state();
  REQUIRE(sounded.soundings == 1);
  const MissionSaveFile save = capture_mission_save(install, *m, 0, "Sounding", {});
  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(save.serialize());
  REQUIRE(parsed);
  MissionSetup again;
  again.random = WatcomRandom(1);
  const std::unique_ptr<Mission> back = restore_mission(install, tables, *parsed, again);
  CHECK(back->sim().sensors().fathometer().state() == sounded);
}

TEST_CASE("The gauges' SOUND button takes a sounding the needle holds", "[soundings][stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const bool rule : {false, true}) {
    MissionSetup setup;
    setup.rules.fathometer_soundings = rule;
    test::ControlRig rig(install, "SCEN/HIST01.SCN", setup);
    rig.screen->install(StationId::kGauges,
                        std::make_unique<GaugesStation>(rig.screen->env(), rig.screen->dive_recorder()));
    rig.frames(1);
    rig.settle();
    rig.key(0x4100);  // F7
    auto* gauges = dynamic_cast<GaugesStation*>(rig.screen->loop().open_station());
    REQUIRE(gauges != nullptr);
    rig.settle();
    rig.frames(2);
    rig.save(rule ? "gauges_soundings" : "gauges_depth_data");
    const ui::Rect button = gauges->sounding_button();
    rig.click((button.x0 + button.x1) / 2, (button.y0 + button.y1) / 2);
    rig.frames(2);
    const Fathometer& f = rig.mission().sim().sensors().fathometer();
    // Without the rule there is no button: the original station.
    CHECK(f.state().soundings == (rule ? 1 : 0));
    if (rule) {
      CHECK(f.sounded_keel_clearance());
      rig.save("gauges_sounded");
    }
  }
}

// -------------------------------------------------------- realistic depths

TEST_CASE("REALISTIC DEPTHS give the classes the Historical Realism Patch's depths", "[soundings][depths]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  struct Expected {
    const char* name;
    std::int32_t periscope, radar;
    std::optional<std::int32_t> eye;
    std::int32_t bridge;
  };
  const std::array<Expected, 9> expected{{
      {"S CLASS", 48, 36, 55, 25},
      {"BARRACUDA", 55, 40, 62, 25},
      {"NARWHAL", 60, 45, 67, 28},
      {"P CLASS", 55, 40, 62, 24},
      {"SALMON", 55, 40, 62, 24},
      {"T CLASS", 60, 45, std::nullopt, 24},
      {"GATO", 60, 45, std::nullopt, 26},
      {"BALAO", 60, 45, std::nullopt, 26},
      {"TENCH", 60, 45, std::nullopt, 26},
  }};
  for (const Expected& e : expected) {
    INFO(e.name);
    const SubClass* sub = tables.find_sub(e.name);
    REQUIRE(sub != nullptr);
    const SubClassParams plain = sub_class_params(*sub);
    MissionRules off = MissionRules::extended();
    off.realistic_depths = false;
    CHECK(sub_class_params(*sub, off) == plain);
    MissionRules rules;
    rules.realistic_depths = true;
    const SubClassParams real = sub_class_params(*sub, rules);
    CHECK(real.periscope_depth_ft == e.periscope);
    CHECK(real.radar_depth_ft == e.radar);
    CHECK(real.periscope_height_ft == e.eye.value_or(plain.periscope_height_ft));
    CHECK(real.bridge_height_ft == e.bridge);
    // Nothing else changes.
    SubClassParams rest = real;
    rest.periscope_depth_ft = plain.periscope_depth_ft;
    rest.radar_depth_ft = plain.radar_depth_ft;
    rest.periscope_height_ft = plain.periscope_height_ft;
    rest.bridge_height_ft = plain.bridge_height_ft;
    CHECK(rest == plain);
  }
}

TEST_CASE("SUBS.DAT holds the depths REALISTIC DEPTHS replace", "[soundings][depths][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  struct Original {
    const char* name;
    std::uint32_t periscope, radar, eye, bridge;
  };
  for (const Original& o : {Original{"S CLASS", 65, 48, 68, 38}, Original{"BARRACUDA", 65, 48, 68, 39},
                            Original{"NARWHAL", 65, 48, 68, 40}, Original{"P CLASS", 65, 48, 68, 38},
                            Original{"SALMON", 65, 48, 68, 39}, Original{"T CLASS", 65, 48, 68, 39},
                            Original{"GATO", 65, 48, 68, 40}, Original{"BALAO", 65, 48, 68, 40},
                            Original{"TENCH", 65, 48, 68, 40}}) {
    INFO(o.name);
    const SubClass* sub = tables.find_sub(o.name);
    REQUIRE(sub != nullptr);
    CHECK(sub->periscope_depth_ft == o.periscope);
    CHECK(sub->radar_depth_ft == o.radar);
    CHECK(sub->periscope_eye_ft == o.eye);
    CHECK(sub->bridge_eye_ft == o.bridge);
  }
}

TEST_CASE("A mission's boat takes the realistic depths, also when restored", "[soundings][depths][save]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  MissionSetup setup;
  setup.random = WatcomRandom(7);
  setup.rules.realistic_depths = true;
  std::unique_ptr<Mission> m = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
  const SubDepths real = realistic_sub_depths(m->player_class());
  const PlayerSub& boat = m->player();
  CHECK(boat.sub().periscope_depth_ft == real.periscope_depth_ft);
  CHECK(boat.sub().radar_depth_ft == real.radar_depth_ft);
  CHECK(boat.sub().periscope_height_ft == real.periscope_eye_ft);
  CHECK(boat.sub().bridge_height_ft == real.bridge_eye_ft);
  // The periscope depth order (P) goes to the realistic depth.
  SimContext ctx = m->sim().context();
  m->player().order_periscope_depth(ctx);
  CHECK(boat.dive().ordered_depth_ft == real.periscope_depth_ft);
  // The original's depths without the rule.
  MissionSetup plain_setup;
  plain_setup.random = WatcomRandom(7);
  const std::unique_ptr<Mission> plain = Mission::load(install, tables, "SCEN/HIST01.SCN", plain_setup);
  CHECK(plain->player().sub() == sub_class_params(plain->player_class()));

  // A restored mission's boat follows the rules of its save, or of its
  // set-up for a save without rules (the original's, a classic build's).
  const std::optional<MissionSaveFile> parsed =
      MissionSaveFile::parse(capture_mission_save(install, *m, 0, "Depths", {}).serialize());
  REQUIRE(parsed);
  MissionSetup again;
  again.random = WatcomRandom(1);
  CHECK(restore_mission(install, tables, *parsed, again)->player().sub() == boat.sub());
  const std::optional<MissionSaveFile> plain_parsed =
      MissionSaveFile::parse(capture_mission_save(install, *plain, 0, "Depths", {}).serialize());
  REQUIRE(plain_parsed);
  again.rules.realistic_depths = true;
  CHECK(restore_mission(install, tables, *plain_parsed, again)->player().sub() == boat.sub());
  again.rules.realistic_depths = false;
  again.rules.mark27 = true;
  CHECK(restore_mission(install, tables, *parsed, again)->player().sub() == boat.sub());
}

TEST_CASE("REALISTIC DEPTHS take LIMITED DEPTH DATA's place and weight in the realism rating",
          "[soundings][depths][frontend]") {
  MissionRules rules = MissionRules::extended();
  CHECK(realistic_depths_item(rules));
  CHECK_FALSE(realistic_depths_item(MissionRules{}));
  rules.fathometer_soundings = false;
  CHECK_FALSE(realistic_depths_item(rules));
  // The original's rating without the item: the depth data bit counts.
  for (const std::uint32_t options : {0x61a8u, 0x61a8u | option::kLimitedDepthDataOff}) {
    CHECK(realism_rating(options, rules) ==
          realism_rating(options, MissionRules{.detectable_radar = true, .extended_realism = true}));
  }
  CHECK(realism_rating_percent(realism_rating(0x61a8 | option::kLimitedDepthDataOff)) == 95);
  // With it, the bit no longer counts; REALISTIC DEPTHS off cost the same 5 %.
  rules = MissionRules::extended();
  for (const std::uint32_t options : {0x61a8u, 0x61a8u | option::kLimitedDepthDataOff}) {
    CHECK(realism_rating_percent(realism_rating(options, rules)) == 100);
    rules.realistic_depths = false;
    CHECK(realism_rating_percent(realism_rating(options, rules)) == 95);
    rules.realistic_depths = true;
  }
  // The presets: on at EXPERT only, as LIMITED DEPTH DATA; untouched without
  // the item.
  for (const RealismPreset preset : {RealismPreset::kNovice, RealismPreset::kIntermediate, RealismPreset::kExpert}) {
    CHECK(apply_realism_preset_rules(MissionRules::extended(), preset).realistic_depths ==
          (preset == RealismPreset::kExpert));
    MissionRules without = MissionRules::extended();
    without.fathometer_soundings = false;
    CHECK(apply_realism_preset_rules(without, preset).realistic_depths);
  }
}

TEST_CASE("A generated patrol starts at the realistic periscope depth", "[soundings][depths]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  for (const bool realistic : {false, true}) {
    GeneratorParams params;
    params.sub_class = "GATO";
    params.year = 13;
    params.month = 5;
    params.day = 10;
    MissionRules rules;
    rules.realistic_depths = realistic;
    WatcomRandom random(5);
    const GeneratedMission g =
        generate_single_mission(install, tables, MissionType::kPatrol, params, 0, random, rules);
    REQUIRE(g.scenario.player_sub);
    const SubClass& gato = *tables.find_sub("GATO");
    CHECK(g.scenario.player_sub->depth == sub_class_params(gato, rules).periscope_depth_ft);
  }
}

}  // namespace sh
