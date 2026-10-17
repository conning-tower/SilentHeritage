// The extended variant's career REQUESTS (game/career_requests.h): good
// standing, the torpedo supply and the load editor, the deck gun fit and its
// class, ROSTER<n>.EXT and the patrol state's "BOAT" section, the patrol
// and mission they change, and the screens; without the rule the career
// and its patrols are as the original's.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/patrol_zones.h"
#include "career_fixture.h"
#include "game/boat_fit.h"
#include "game/career_flow.h"
#include "game/career_menus.h"
#include "game/career_requests.h"
#include "game/career_rules.h"
#include "game/career_save.h"
#include "game/mission.h"
#include "game/patrol.h"
#include "game/saves_options.h"
#include "game/sim_state.h"
#include "game/torpedo_art.h"
#include "game/torpedo_load.h"
#include "game_data.h"
#include "sim/torpedo.h"

namespace sh {
namespace {

using test::button;
using test::Fixture;
using test::ScriptedHost;

constexpr std::uint32_t kMk10 = kMark10Id;
constexpr std::uint32_t kMk14 = kMark14Id;
constexpr std::uint32_t kMk18_1 = kMark18_1Id;
constexpr std::uint32_t kMk18_2 = kMark18_2Id;

// A career in port at Saipan (ComSubPac) in September 1944 with a Gato,
// under the extended rules.
Career saipan_career() {
  Career c;
  CareerRecord& r = c.record;
  r.name = "Tester";
  r.month = 8;
  r.day = 1;
  r.year = 14;
  r.status = CareerStatus::kInPort;
  r.boat = "(SS-212) Gato";
  r.class_name = "GATO";
  r.rank = 1;
  r.home_port = "Saipan";
  r.location = "Saipan";
  r.seed = 4321;
  r.transit_speed = 17;
  r.fuel_use = 3;
  c.summary.current_command = static_cast<std::int32_t>(SubCommand::kComSubPac);
  c.rules = MissionRules::extended();
  return c;
}

// A boat with `tubes` tubes (half bow) and `racks` places in each room.
SubClass small_boat(std::uint32_t tubes, std::uint32_t racks) {
  SubClass s;
  s.name = "TEST";
  s.bow_tubes = tubes / 2;
  s.stern_tubes = tubes - tubes / 2;
  s.bow_reloads = racks;
  s.stern_reloads = racks;
  s.modern_torpedoes = true;
  s.deck_gun_count = 1;
  s.deck_gun_caliber_in = 4;
  s.single_gun_mount = 1;
  s.deck_gun_rounds = 95;
  s.sprite_profile = "P_GATO.SHP";
  return s;
}

// The y of the second row of PATROL AREA's list in its dialog.
constexpr int kAreaRow1Y = 0xa0;

// The REQUESTS menu: its entries (then BASE MENU), centred.
constexpr int kRequestEntries = 4;
ScriptedHost::Step request_button(int entry) {
  return button(5, (480 - (0x82 + kRequestEntries * 0x28 + 0x46)) / 2, 0x1e, 0x82 + 0x28 * entry, 0xc6);
}

std::vector<PatrolZone> load_zones(const Install& install) {
  return parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
}

TEST_CASE("Good standing: a Silver Star or better, or the top rank, and no poor patrol", "[requests]") {
  CareerRecord r;
  CHECK_FALSE(in_good_standing(r));
  r.award(Award::kBronzeStar) = 3;
  r.award(Award::kNuc) = 1;
  CHECK_FALSE(in_good_standing(r));
  for (const Award a : {Award::kSilverStar, Award::kNavyCross, Award::kDsm, Award::kMedalOfHonor}) {
    CareerRecord d = r;
    d.award(a) = 1;
    CHECK(in_good_standing(d));
    d.poor_patrol_streak = 1;
    CHECK_FALSE(in_good_standing(d));
  }
  r.rank = 2;
  CHECK(in_good_standing(r));
  r.poor_patrol_streak = 2;
  CHECK_FALSE(in_good_standing(r));
}

TEST_CASE("The supply: types by date and command, short for three months", "[requests][torpedo_types]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const SubClass& gato = *tables.find_sub("GATO");
  const SubClass& s_class = *tables.find_sub("S CLASS");
  const MissionRules rules = MissionRules::extended();
  const TorpedoLoad none;
  const auto offers = [&](const SubClass& sub, std::int32_t year, std::int32_t month, SubCommand command,
                          bool good = false, const TorpedoLoad& generated = TorpedoLoad{}) {
    return torpedo_offers(sub, tables, year, month, command, rules, good, generated);
  };
  using O = TorpedoOffer;
  // A boat without modern torpedoes: the Mark 10 only.
  CHECK(offers(s_class, 14, 8, SubCommand::kComSubPac) == std::vector<O>{{kMk10, std::nullopt}});
  // September 1943: the straight runners; the 18-1 from October, short.
  CHECK(offers(gato, 13, 8, SubCommand::kComSubPac) == std::vector<O>{{kMk10, std::nullopt}, {kMk14, std::nullopt}});
  CHECK(offers(gato, 13, 9, SubCommand::kComSubPac) ==
        std::vector<O>{{kMk10, std::nullopt}, {kMk14, std::nullopt}, {kMk18_1, 4}});
  CHECK(offers(gato, 13, 11, SubCommand::kComSubPac, true)[2] == O{kMk18_1, 6});
  CHECK(offers(gato, 14, 0, SubCommand::kComSubPac)[2] == O{kMk18_1, std::nullopt});
  // August 1944 for ComSubPac: the 18-2 new, the Mark 27 in its third month.
  CHECK(offers(gato, 14, 7, SubCommand::kComSubPac) ==
        std::vector<O>{{kMk10, std::nullopt}, {kMk14, std::nullopt}, {kMk18_1, std::nullopt}, {kMk18_2, 4},
                       {kMark27Id, 2}});
  CHECK(offers(gato, 14, 7, SubCommand::kComSubPac, true)[4] == O{kMark27Id, 4});
  // September: the Mark 27 plenty for ComSubPac, new elsewhere.
  CHECK(offers(gato, 14, 8, SubCommand::kComSubPac)[4] == O{kMark27Id, std::nullopt});
  CHECK(offers(gato, 14, 8, SubCommand::kComSubSoWesPac)[4] == O{kMark27Id, 2});
  CHECK(offers(gato, 14, 7, SubCommand::kComSubSoWesPac).size() == 4);
  // Without the Mark 27 rule, none.
  MissionRules no27 = rules;
  no27.mark27 = false;
  CHECK(torpedo_offers(gato, tables, 14, 8, SubCommand::kComSubPac, no27, false, none).size() == 4);
  // The generated load's own count is always allowed.
  TorpedoLoad generated;
  generated.tubes.fill(static_cast<std::int32_t>(kMk18_2));
  CHECK(offers(gato, 14, 7, SubCommand::kComSubPac, false, generated)[3] ==
        O{kMk18_2, static_cast<std::int32_t>(gato.bow_tubes + gato.stern_tubes)});
}

TEST_CASE("The load editor keeps the supply, three types and the places", "[requests]") {
  const SubClass boat = small_boat(4, 2);
  TorpedoLoad generated;
  generated.tubes = {2, 2, 2, 2};
  const std::vector<TorpedoOffer> offers{{kMk10, std::nullopt}, {kMk14, std::nullopt}, {kMk18_1, 2},
                                         {kMk18_2, std::nullopt}};
  TorpedoLoadEditor e(boat, generated, offers, std::nullopt);
  using P = TorpedoPlace;
  using G = TorpedoPlace::Group;
  CHECK(e.load() == generated);
  CHECK_FALSE(e.chosen());
  CHECK(e.places(G::kTube) == 4);
  CHECK(e.places(G::kSternRack) == 2);

  // Taken from the supply, put in a rack; the held one counts.
  REQUIRE(e.take(kMk18_1));
  CHECK(e.count(kMk18_1) == 1);
  CHECK(e.can_take(kMk14) == TorpedoLoadEditor::Refusal::kNotOffered);
  REQUIRE(e.put(P{G::kBowRack, 1}));
  CHECK(e.held() == 0);
  CHECK(e.at(P{G::kBowRack, 1}) == static_cast<std::int32_t>(kMk18_1));
  CHECK(e.add(kMk18_1));
  CHECK(e.can_take(kMk18_1) == TorpedoLoadEditor::Refusal::kSupply);
  CHECK_FALSE(e.add(kMk18_1));
  // A fourth type is refused.
  REQUIRE(e.add(kMk10));
  CHECK(e.types() == std::vector<std::uint32_t>{kMk10, kMk14, kMk18_1});
  CHECK(e.can_take(kMk18_2) == TorpedoLoadEditor::Refusal::kTypes);
  // Put on a loaded place, it swaps.
  REQUIRE(e.pick(P{G::kTube, 0}));
  CHECK(e.at(P{G::kTube, 0}) == 0);
  REQUIRE(e.put(P{G::kBowRack, 1}));
  CHECK(e.held() == kMk18_1);
  CHECK(e.at(P{G::kBowRack, 1}) == static_cast<std::int32_t>(kMk14));
  // Returned to the supply, it is unloaded.
  e.drop();
  CHECK(e.count(kMk18_1) == 1);
  // Full: every place loaded.
  while (e.add(kMk14)) {
  }
  CHECK(e.can_take(kMk14) == TorpedoLoadEditor::Refusal::kFull);
  CHECK(e.remove(kMk14));
  CHECK(e.can_take(kMk14) == TorpedoLoadEditor::Refusal::kNone);
  REQUIRE(e.chosen());
  CHECK(torpedo_load_allowed(*e.chosen(), boat, offers));
  // Back to the generated load: no request.
  e.reset();
  CHECK_FALSE(e.chosen());

  // A chosen load is taken back when it is still allowed.
  TorpedoLoad mine;
  mine.tubes = {3, 3, 0, 1};
  TorpedoLoadEditor again(boat, generated, offers, mine);
  CHECK(again.load() == mine);
  mine.tubes = {3, 3, 3, 1};
  TorpedoLoadEditor over(boat, generated, offers, mine);
  CHECK(over.load() == generated);
}

TEST_CASE("An allowed load: supplied types within their limits and the class's places", "[requests]") {
  const SubClass boat = small_boat(4, 2);
  const std::vector<TorpedoOffer> offers{{kMk10, std::nullopt}, {kMk14, std::nullopt}, {kMk18_1, 1},
                                         {kMk18_2, std::nullopt}};
  TorpedoLoad load;
  load.tubes = {1, 2, 3, 0};
  CHECK(torpedo_load_allowed(load, boat, offers));
  load.bow_racks[2] = 2;  // past the class's two places
  CHECK_FALSE(torpedo_load_allowed(load, boat, offers));
  load.bow_racks[2] = 0;
  load.tubes[3] = 4;  // a fourth type
  CHECK_FALSE(torpedo_load_allowed(load, boat, offers));
  load.tubes[3] = 3;  // a second 18-1
  CHECK_FALSE(torpedo_load_allowed(load, boat, offers));
  load.tubes[3] = static_cast<std::int32_t>(kMark27Id);  // not supplied
  load.tubes[0] = 2;
  CHECK_FALSE(torpedo_load_allowed(load, boat, offers));
}

TEST_CASE("The deck gun fit makes the boat's class", "[requests]") {
  const SubClass gato = small_boat(10, 10);
  CHECK(fleet_boat_with_one_gun(gato));
  CHECK(fitted_sub_class(gato, {}).single_gun_mount == 1);
  const SubClass forward = fitted_sub_class(gato, {.gun_mount = 0, .second_gun = false});
  CHECK(forward.single_gun_mount == 0);
  CHECK(forward.deck_gun_count == 1);
  CHECK(forward.sprite_profile == "P_GATO.SHP");
  const SubClass two = fitted_sub_class(gato, {.gun_mount = 0, .second_gun = true});
  CHECK(two.deck_gun_count == 2);
  CHECK(two.deck_gun_rounds == 100);
  CHECK(two.deck_gun_caliber_in == 4);
  CHECK(two.sprite_profile == "P_TENCH.SHP");
  // Another class's picture stays; more rounds stay.
  SubClass salmon = gato;
  salmon.sprite_profile = "P_SALMON.SHP";
  salmon.deck_gun_rounds = 120;
  CHECK(fitted_sub_class(salmon, {.gun_mount = std::nullopt, .second_gun = true}).sprite_profile == "P_SALMON.SHP");
  CHECK(fitted_sub_class(salmon, {.gun_mount = std::nullopt, .second_gun = true}).deck_gun_rounds == 120);
  // Not a fleet boat with one gun: unchanged.
  SubClass old = gato;
  old.modern_torpedoes = false;
  CHECK_FALSE(fleet_boat_with_one_gun(old));
  CHECK(fitted_sub_class(old, {.gun_mount = 0, .second_gun = true}).deck_gun_count == 1);
  CHECK(fitted_sub_class(old, {.gun_mount = 0, .second_gun = true}).single_gun_mount == 1);
  SubClass tench = gato;
  tench.deck_gun_count = 2;
  CHECK_FALSE(fleet_boat_with_one_gun(tench));

  CareerRecord r;
  CHECK(can_move_deck_gun(gato, {}));
  CHECK_FALSE(can_move_deck_gun(gato, {.gun_mount = std::nullopt, .second_gun = true}));
  CHECK_FALSE(can_fit_second_gun(gato, {}, r));
  r.rank = 2;
  CHECK(can_fit_second_gun(gato, {}, r));
  CHECK_FALSE(can_fit_second_gun(old, {}, r));

  for (const BoatFit& f : {BoatFit{}, BoatFit{.gun_mount = 1, .second_gun = false},
                           BoatFit{.gun_mount = std::nullopt, .second_gun = true}}) {
    CHECK(decode_boat_fit(encode_boat_fit(f)) == f);
  }
}

TEST_CASE("Requests round-trip through ROSTER<n>.EXT for their boat", "[requests][save]") {
  CHECK(career_requests_config({}).empty());
  CareerRequests r{.boat = "(SS-212) Gato", .class_name = "GATO", .fit = {.gun_mount = 0, .second_gun = true},
                   .torpedo_load = std::nullopt};
  TorpedoLoad load;
  load.tubes = {2, 2, 3, 3, 0, 0, 2, 2, 0, 0};
  load.bow_racks[0] = 5;
  r.torpedo_load = load;
  const std::string text = career_requests_config(r);
  CHECK(text.starts_with("boat.name = (SS-212) Gato\nboat.class = GATO\nboat.gun_mount = 0\nboat.second_gun = 1\n"
                         "request.tubes = 2,2,3,3,0,0,2,2,0,0\n"));
  CHECK(parse_career_requests(text) == r);
  // With the rules' lines and unknown ones; a damaged load is dropped.
  CHECK(parse_career_requests("rule.mark27 = 1\nlater.thing = 3\n" + text) == r);
  CareerRequests damaged = parse_career_requests("boat.name = A\nboat.class = B\nrequest.tubes = 1,2\n"
                                                 "request.bow_racks = 0\nrequest.stern_racks = 0\n");
  CHECK_FALSE(damaged.torpedo_load);

  // Another boat's requests do not apply, and are dropped when asking again.
  Career c = saipan_career();
  c.requests = r;
  CHECK(career_boat_fit(c) == r.fit);
  c.record.boat = "(SS-213) Greenling";
  CHECK(career_boat_fit(c).empty());
  CHECK(current_requests(c) == CareerRequests{.boat = "(SS-213) Greenling", .class_name = "GATO", .fit = {},
                                               .torpedo_load = std::nullopt});
  // Without the rule, nothing applies.
  c.requests = r;
  c.record.boat = r.boat;
  c.rules->career_requests = false;
  CHECK(career_boat_fit(c).empty());

  // The saved career keeps them beside its rules; the classic one writes no
  // file.
  const test::CareerTempDir dir;
  const SaveDir saves(dir.path());
  Career saved = saipan_career();
  saved.requests = r;
  REQUIRE(save_career(saves, saved));
  CHECK(load_career_requests(saves, saved.record.roster_slot) == r);
  CHECK(load_career_rules(saves, saved.record.roster_slot) == MissionRules::extended());
  Career classic = saipan_career();
  classic.rules.reset();
  classic.record.roster_slot = 5;
  REQUIRE(save_career(saves, classic));
  CHECK_FALSE(saves.read(career_rules_file_name(5)));
}

TEST_CASE("A requested load replaces the generated one after the same draws", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const std::vector<PatrolZone> zones = load_zones(install);
  const SubClass& gato = *tables.find_sub("GATO");
  TorpedoLoad mine;
  for (std::size_t i = 0; i < gato.bow_tubes + gato.stern_tubes; ++i) {
    mine.tubes[i] = static_cast<std::int32_t>(i < 2 ? kMark27Id : kMk18_1);
  }
  mine.bow_racks[0] = static_cast<std::int32_t>(kMk14);
  const auto plan_of = [&](Career& c, WatcomRandom& random) {
    return build_patrol_scenario(c, zones.at(9), 9, 20000, 100, tables, random);
  };

  Career plain = saipan_career();
  WatcomRandom plain_random(77);
  const PatrolPlan generated = plan_of(plain, plain_random);
  CHECK_FALSE(generated.torpedo_load);
  CHECK(generated.boat_fit.empty());

  Career asked = saipan_career();
  asked.requests = {.boat = asked.record.boat, .class_name = "GATO", .fit = {.gun_mount = 0, .second_gun = false},
                    .torpedo_load = mine};
  WatcomRandom asked_random(77);
  const PatrolPlan plan = plan_of(asked, asked_random);
  CHECK(asked_random.state() == plain_random.state());
  CHECK(plan.scenario.header.tube_torpedoes == generated.scenario.header.tube_torpedoes);
  CHECK(plan.start_fuel == generated.start_fuel);
  CHECK(plan.torpedo_load == mine);
  CHECK(plan.boat_fit == BoatFit{.gun_mount = 0, .second_gun = false});
  // Applied once: the next patrol starts from the generated load.
  CHECK_FALSE(asked.requests.torpedo_load);
  CHECK(asked.requests.fit == plan.boat_fit);

  // A load no longer allowed (two Mark 27 more than the supply of the
  // month elsewhere) falls back to the generated one, and is dropped.
  Career elsewhere = saipan_career();
  elsewhere.summary.current_command = static_cast<std::int32_t>(SubCommand::kComSubSoWesPac);
  TorpedoLoad four27 = mine;
  four27.tubes[2] = four27.tubes[3] = static_cast<std::int32_t>(kMark27Id);
  elsewhere.requests = {.boat = elsewhere.record.boat, .class_name = "GATO", .fit = {}, .torpedo_load = four27};
  WatcomRandom elsewhere_random(77);
  CHECK_FALSE(plan_of(elsewhere, elsewhere_random).torpedo_load);
  CHECK_FALSE(elsewhere.requests.torpedo_load);

  // Without the rule (a classic career keeps none), the requests are left
  // alone and nothing applies.
  for (const bool classic : {false, true}) {
    Career off = saipan_career();
    if (classic) {
      off.rules.reset();
    } else {
      off.rules->career_requests = false;
    }
    off.requests = asked.requests;
    off.requests.torpedo_load = mine;
    WatcomRandom off_random(77);
    const PatrolPlan p = plan_of(off, off_random);
    CHECK_FALSE(p.torpedo_load);
    CHECK(p.boat_fit.empty());
    CHECK(off.requests.torpedo_load == mine);
  }
}

TEST_CASE("The preview is the patrol START WAR PATROL makes, drawing nothing", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const CampaignData campaign = CampaignData::load(install, tables);
  Career career = saipan_career();
  const Career before = career;
  const std::optional<PatrolPlan> preview = preview_patrol(career, campaign, tables);
  REQUIRE(preview);
  CHECK(career.record.seed == before.record.seed);
  CHECK(career.record.last_patrol_zone == before.record.last_patrol_zone);
  // The same steps on the career itself.
  WatcomRandom random;
  random.srand(career.record.seed);
  std::optional<PatrolAssignment> a = assign_patrol(career, campaign, random);
  REQUIRE(a);
  const PatrolPlan plan = depart_on_patrol(career, *a, tables, random);
  CHECK(plan.zone_index == preview->zone_index);
  CHECK(plan.scenario.header.tube_torpedoes == preview->scenario.header.tube_torpedoes);
  CHECK(plan.scenario.header.month == preview->scenario.header.month);
  CHECK(plan.scenario.header.hour == preview->scenario.header.hour);
  CHECK(plan.start_fuel == preview->start_fuel);
}

// Builds the patrol of `career` (seed 99) and its mission.
struct PatrolMission {
  PatrolMission(const Install& install, const GameTables& tables, Career& career) {
    const std::vector<PatrolZone> zones = load_zones(install);
    WatcomRandom random(99);
    PatrolPlan plan = build_patrol_scenario(career, zones.at(9), 9, 20000, 100, tables, random);
    patrol = std::make_unique<Patrol>(install, tables, career, std::move(plan));
    MissionSetup setup;
    setup.options = option::kModeCareer | RealismFile::kDefaultBits;
    setup.random = random;
    setup.rules = career.rules.value_or(MissionRules{});
    mission = patrol->create_mission(std::move(setup));
  }
  std::unique_ptr<Patrol> patrol;
  std::unique_ptr<Mission> mission;
};

TEST_CASE("The patrol's mission carries the chosen load and the fitted guns", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const SubClass& gato = *tables.find_sub("GATO");

  // Without requests: the class itself, the load generated.
  Career plain = saipan_career();
  PatrolMission without(install, tables, plain);
  CHECK(&without.mission->player_class() == &gato);
  CHECK(without.mission->boat_fit().empty());
  CHECK(without.mission->player_combat().deck_gun.selected() == static_cast<std::int32_t>(gato.single_gun_mount));

  // A Mark 18-1 load past the 18-2's changeover stays as chosen.
  Career career = saipan_career();
  career.record.year = 15;
  career.record.month = 2;
  career.record.rank = 2;
  career.record.home_port = "Guam";
  TorpedoLoad mine;
  for (std::size_t i = 0; i < gato.bow_tubes + gato.stern_tubes; ++i) {
    mine.tubes[i] = static_cast<std::int32_t>(kMk18_1);
  }
  mine.stern_racks[0] = static_cast<std::int32_t>(kMk10);
  career.requests = {.boat = career.record.boat, .class_name = "GATO",
                     .fit = {.gun_mount = std::nullopt, .second_gun = true}, .torpedo_load = mine};
  PatrolMission with(install, tables, career);
  const Mission& m = *with.mission;
  CHECK(m.torpedoes() == mine);
  CHECK(m.start_torpedo_types() == std::vector<std::uint32_t>{kMk10, kMk18_1});
  CHECK(m.torpedo_types().types[0].id == static_cast<std::int32_t>(kMk10));
  CHECK(m.torpedo_types().types[1].id == static_cast<std::int32_t>(kMk18_1));
  CHECK(m.torpedo_types().types[1].available);
  CHECK_FALSE(m.torpedo_types().types[2].available);
  // Two guns: both usable, the rounds raised.
  CHECK(m.player_class().deck_gun_count == 2);
  CHECK(m.player_class().name == "GATO");
  CHECK(m.player_combat().deck_gun.ammo() == static_cast<std::int32_t>(std::max(gato.deck_gun_rounds, 100u)));
  CHECK(m.player().systems()[15].flags == 0);
  CHECK(without.mission->player().systems()[15].flags == 2);

  // The state saved on patrol keeps the fit ("BOAT"), which a resumed
  // patrol is built with; none without a fit.
  SimStateFile state = capture_sim_state(*with.mission, {with.patrol.get()}, {});
  CHECK(decode_boat_fit(state.boat_extension) == career.requests.fit);
  const std::vector<std::uint8_t> bytes = state.serialize();
  ByteReader reader(bytes, "state");
  CHECK(SimStateFile::parse(reader, kSimStateVersion).boat_extension == state.boat_extension);
  CHECK(capture_sim_state(*without.mission, {without.patrol.get()}, {}).boat_extension.empty());

  Career resumed = career;
  resumed.requests = {};
  resumed.record.on_patrol = -1;
  resumed.patrol_state = bytes;
  PatrolMission back(install, tables, resumed);
  CHECK(back.mission->player_class().deck_gun_count == 2);
}

TEST_CASE("REQUESTS on the BASE MENU: DECK GUN moves the gun and fits a second", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  Fixture f(install, true);
  CareerFlow flow(f.ui, tables, campaign);
  Career career = saipan_career();
  career.record.rank = 2;
  // The base: START WAR PATROL, TOP COMMANDERS, VIEW CAREER, REQUESTS,
  // SAVE CAREER, MAIN MENU; the requests: TORPEDO LOAD, DECK GUN, BASE
  // MENU; DECK GUN: MOVE, FIT SECOND GUN, DONE.
  const int base_y = (480 - 0x190) / 2;
  const int gun_x = (640 - 0x1a4) / 2;
  const int gun_y = (480 - 0x158) / 2;
  const int gun_button = (0x1a4 - 0xc6) / 2;
  f.host.script = {button(5, base_y, 0x1e, 0xfa, 0xc6),
                   request_button(1),
                   button(gun_x, gun_y, gun_button, 0xc2, 0xc6),
                   button(gun_x, gun_y, gun_button, 0xea, 0xc6),
                   ScriptedHost::key(0x1559),
                   button(gun_x, gun_y, gun_button, 0x112, 0xc6),
                   request_button(kRequestEntries),
                   button(5, base_y, 0x1e, 0x14a, 0xc6)};
  flow.resume(career);
  CHECK(f.host.script.empty());
  // The gun moved to the other mount, then the second fitted.
  const std::uint32_t mount = tables.find_sub("GATO")->single_gun_mount == 0 ? 1 : 0;
  const BoatFit fit{.gun_mount = mount, .second_gun = true};
  CHECK(flow.career().requests.fit == fit);
  CHECK(flow.career().requests.boat == career.record.boat);
  // Saved with the career.
  CHECK(load_career_requests(f.saves, career.record.roster_slot).fit == fit);
}

TEST_CASE("REQUESTS: TORPEDO LOAD adds a torpedo to the generated load and keeps it", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const CampaignData campaign = CampaignData::load(install, tables);
  Fixture f(install, true);
  CareerFlow flow(f.ui, tables, campaign);
  Career career = saipan_career();
  const std::optional<PatrolPlan> preview = preview_patrol(career, campaign, tables);
  REQUIRE(preview);
  const SubClass& gato = *tables.find_sub("GATO");
  const TorpedoLoad generated = generated_torpedo_load(preview->scenario.header, gato, tables, *career.rules);
  // Make room: the generated load fills every place; then + on the Mark 10
  // (first of the supply), which the Gato does not carry. Five types are
  // supplied, 0x80 apart from x 0x40.
  const int base_y = (480 - 0x190) / 2;
  const auto minus = [](int column) { return ScriptedHost::click(0x40 + 0x80 * column - 0x22 + 0xf, 0x4e + 8); };
  const auto plus = [](int column) { return ScriptedHost::click(0x40 + 0x80 * column + 4 + 0xf, 0x4e + 8); };
  const std::vector<std::uint32_t> carried = carried_torpedo_types(generated, gato);
  REQUIRE(carried.size() <= 2);
  const int carried_column = carried.front() == kMk14 ? 1 : carried.front() == kMk18_1 ? 2 : 3;
  f.host.script = {button(5, base_y, 0x1e, 0xfa, 0xc6), request_button(0), minus(carried_column),
                   plus(0), ScriptedHost::click(0x1e0 + 0x46, 0x1bd + 8), request_button(kRequestEntries),
                   button(5, base_y, 0x1e, 0x14a, 0xc6)};
  flow.resume(career);
  CHECK(f.host.script.empty());
  const std::optional<TorpedoLoad>& chosen = flow.career().requests.torpedo_load;
  REQUIRE(chosen);
  TorpedoLoadEditor expected(gato, generated,
                             torpedo_offers(gato, tables, 14, 8, SubCommand::kComSubPac, *career.rules, false,
                                            generated),
                             std::nullopt);
  REQUIRE(expected.remove(carried.front()));
  REQUIRE(expected.add(kMk10));
  CHECK(*chosen == expected.load());
  CHECK(load_career_requests(f.saves, career.record.roster_slot).torpedo_load == chosen);
}

TEST_CASE("Without the rule the BASE MENU has no REQUESTS", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  Fixture f(install, true);
  Career career = saipan_career();
  career.rules->career_requests = false;
  // SAVE CAREER right after VIEW CAREER, as the original.
  const int dy = (480 - (0xfa + 0x6e)) / 2;
  f.host.script = {button(5, dy, 0x1e, 0x122, 0xc6)};
  CHECK(base_menu_screen(f.ui, career, true, [] {}) == career_code::kLeaveCareer);
  career.rules.reset();
  f.host.script = {button(5, dy, 0x1e, 0x122, 0xc6)};
  CHECK(base_menu_screen(f.ui, career, true, [] {}) == career_code::kLeaveCareer);
}

TEST_CASE("PATROL AREA: a zone the command may assign, after the same draws", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  Career career = saipan_career();
  const std::vector<std::int32_t> zones = patrol_zone_candidates(career, campaign);
  REQUIRE(zones.size() >= 2);
  // Every zone drawn is a candidate.
  for (std::uint32_t seed = 1; seed <= 30; ++seed) {
    Career c = saipan_career();
    WatcomRandom random(seed);
    const std::optional<PatrolAssignment> a = assign_patrol(c, campaign, random);
    REQUIRE(a);
    CHECK(std::ranges::find(zones, a->zone_index) != zones.end());
  }
  // Not in good standing (rank 1, no medal): no request.
  CHECK_FALSE(can_request_patrol_area(career, campaign));
  career.record.award(Award::kSilverStar) = 1;
  CHECK(can_request_patrol_area(career, campaign));

  for (const std::int32_t asked : zones) {
    Career plain = career;
    WatcomRandom plain_random(5);
    const std::optional<PatrolAssignment> drawn = assign_patrol(plain, campaign, plain_random);
    Career c = career;
    current_requests(c).patrol_zone = asked;
    WatcomRandom random(5);
    const std::optional<PatrolAssignment> a = assign_requested_patrol(c, campaign, random);
    REQUIRE(a);
    CHECK(random.state() == plain_random.state());
    CHECK(a->zone_index == asked);
    CHECK(a->zone.name == campaign.zones.at(static_cast<std::size_t>(asked)).name);
    CHECK(a->previous_zone == drawn->previous_zone);
    CHECK(c.record.last_patrol_zone == asked);
    const Transit t = compute_transit(*find_home_port(campaign, c.record, c.record.home_port), asked, c.record);
    CHECK(a->transit_hours == t.hours);
    CHECK(a->transit_fuel == t.fuel);
    // The patrol clears the request.
    PatrolAssignment departing = *a;
    depart_on_patrol(c, departing, tables, random);
    CHECK_FALSE(c.requests.patrol_zone);
  }
  // Ignored: out of good standing since, a zone not assigned now, the rule
  // off, or another boat's request.
  const auto drawn_zone = [&](Career c) {
    WatcomRandom random(5);
    return assign_requested_patrol(c, campaign, random)->zone_index;
  };
  Career plain = career;
  const std::int32_t usual = drawn_zone(plain);
  const std::int32_t other = zones.front() != usual ? zones.front() : zones.back();
  Career asked = career;
  current_requests(asked).patrol_zone = other;
  CHECK(drawn_zone(asked) == other);
  Career poor = asked;
  poor.record.poor_patrol_streak = 1;
  CHECK(drawn_zone(poor) == usual);
  Career off = asked;
  off.rules->career_requests = false;
  CHECK(drawn_zone(off) == usual);
  Career moved = asked;
  moved.record.boat = "(SS-213) Greenling";
  CHECK(drawn_zone(moved) == usual);
  Career nowhere = asked;
  nowhere.requests.patrol_zone = 9999;
  CHECK(drawn_zone(nowhere) == usual);

  CareerRequests r{.boat = "B", .class_name = "C"};
  r.patrol_zone = 7;
  CHECK(career_requests_config(r) == "boat.name = B\nboat.class = C\nrequest.patrol_zone = 7\n");
  CHECK(parse_career_requests(career_requests_config(r)) == r);
}

TEST_CASE("REQUESTS: PATROL AREA keeps the zone chosen for the next patrol", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  Fixture f(install, true);
  CareerFlow flow(f.ui, tables, campaign);
  Career career = saipan_career();
  career.record.rank = 2;
  const std::vector<std::int32_t> zones = patrol_zone_candidates(career, campaign);
  REQUIRE(!zones.empty());
  // PATROL AREA's list: AS ASSIGNED, then the zones; the first zone, SELECT.
  const int list_x = (640 - 0x1c0) / 2;
  const int list_y = (480 - 0x170) / 2;
  const int base_y = (480 - 0x190) / 2;
  f.host.script = {button(5, base_y, 0x1e, 0xfa, 0xc6),
                   request_button(2),
                   ScriptedHost::screenshot("requests_patrol_area"),
                   ScriptedHost::click(list_x + 0x60, list_y + kAreaRow1Y),
                   button(list_x, list_y, 0x1c0 - 0x2a - 0x79, 0x170 - 0x46, 0x79),
                   request_button(kRequestEntries),
                   button(5, base_y, 0x1e, 0x14a, 0xc6)};
  flow.resume(career);
  CHECK(f.host.script.empty());
  CHECK(flow.career().requests.patrol_zone == zones.front());
  CHECK(load_career_requests(f.saves, career.record.roster_slot).patrol_zone == zones.front());
}

TEST_CASE("BOAT TRANSFER: the chance, fixed for a port visit", "[requests]") {
  CareerRecord r;
  CHECK(boat_transfer_chance(r) == 0);
  r.award(Award::kBronzeStar) = 2;
  CHECK(boat_transfer_chance(r) == 10);
  r.award(Award::kSilverStar) = 1;
  r.rank = 1;
  CHECK(boat_transfer_chance(r) == 30);
  r.award(Award::kDsm) = 1;
  r.patrols_since_new_boat = 4;
  CHECK(boat_transfer_chance(r) == 50);
  r.award(Award::kMedalOfHonor) = 1;
  r.rank = 2;
  CHECK(boat_transfer_chance(r) == 70);
  r.patrols_since_new_boat = 8;
  CHECK(boat_transfer_chance(r) == 75);
  r.poor_patrol_streak = 1;
  CHECK(boat_transfer_chance(r) == 0);
  CHECK_FALSE(boat_transfer_granted(r));

  // The draw depends on the visit only, and follows the chance.
  CareerRecord half;
  half.rank = 2;
  half.award(Award::kNavyCross) = 1;
  REQUIRE(boat_transfer_chance(half) == 50);
  int granted = 0;
  for (std::uint32_t seed = 1; seed <= 400; ++seed) {
    half.seed = seed * 7919;
    half.patrols = static_cast<std::int32_t>(seed % 9);
    const bool g = boat_transfer_granted(half);
    CHECK(boat_transfer_granted(half) == g);
    granted += g ? 1 : 0;
  }
  CHECK(granted > 150);
  CHECK(granted < 250);
}

TEST_CASE("BOAT TRANSFER: asked for a newer class after a patrol", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  Career career = saipan_career();
  CHECK_FALSE(can_request_boat_transfer(career, SubCommand::kComSubPac, tables));
  career.record.patrols = 1;
  career.record.class_index = static_cast<std::uint8_t>(tables.find_sub("GATO")->class_index);
  CHECK(can_request_boat_transfer(career, SubCommand::kComSubPac, tables));
  career.rules->career_requests = false;
  CHECK_FALSE(can_request_boat_transfer(career, SubCommand::kComSubPac, tables));
  // The newest class of the date already.
  Career balao = saipan_career();
  balao.record.patrols = 1;
  balao.record.class_name = "BALAO";
  balao.record.class_index = static_cast<std::uint8_t>(tables.find_sub("BALAO")->class_index);
  CHECK_FALSE(can_request_boat_transfer(balao, SubCommand::kComSubPac, tables));
}

TEST_CASE("REQUESTS: BOAT TRANSFER offers the newer boat, or says no", "[requests][career]") {
  SH_REQUIRE_INSTALL(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  const int base_y = (480 - 0x190) / 2;
  for (const bool grant : {true, false}) {
    Fixture f(install, true);
    CareerFlow flow(f.ui, tables, campaign);
    Career career = saipan_career();
    career.record.patrols = 3;
    career.record.rank = 2;
    career.record.class_index = static_cast<std::uint8_t>(tables.find_sub("GATO")->class_index);
    career.requests = {.boat = career.record.boat, .class_name = "GATO", .fit = {.gun_mount = 0}};
    // A visit whose draw grants (or refuses) it.
    while (boat_transfer_granted(career.record) != grant) {
      ++career.record.seed;
    }
    // Its requests as saved (the flow reads them back).
    REQUIRE(save_career(f.saves, career));
    f.host.script = {button(5, base_y, 0x1e, 0xfa, 0xc6), request_button(3)};
    if (grant) {
      // NEW SUBMARINE: OK accepts the offer.
      f.host.script.push_back(button(10, 0x27, 0x1b2, 0x15c, 0x79));
    } else {
      f.host.script.push_back(ScriptedHost::key(ui::kKeyEnter));
    }
    f.host.script.push_back(request_button(kRequestEntries));
    f.host.script.push_back(button(5, base_y, 0x1e, 0x14a, 0xc6));
    flow.resume(career);
    CHECK(f.host.script.empty());
    const CareerRecord& r = flow.career().record;
    if (grant) {
      CHECK(r.class_name != "GATO");
      CHECK(r.boat != career.record.boat);
      CHECK(r.rank == 1);
      CHECK(r.patrols_since_new_boat == 0);
      // The gun fit stayed with the old boat.
      CHECK(career_boat_fit(flow.career()).empty());
      CHECK(load_roster(f.saves, r.roster_slot)->career.record.boat == r.boat);
    } else {
      CHECK(r.boat == career.record.boat);
      CHECK(r.rank == 2);
      CHECK(career_boat_fit(flow.career()) == career.requests.fit);
    }
  }
}

TEST_CASE("Request screenshots", "[.request_screens]") {
  SH_REQUIRE_INSTALL(install);
  GameTables tables = GameTables::load(install);
  add_extended_torpedoes(tables);
  const CampaignData campaign = CampaignData::load(install, tables);
  Fixture f(install, true);
  CareerFlow flow(f.ui, tables, campaign);
  Career career = saipan_career();
  career.record.rank = 2;
  const int base_y = (480 - 0x190) / 2;
  using S = ScriptedHost;
  f.host.script = {S::screenshot("requests_base_menu"), button(5, base_y, 0x1e, 0xfa, 0xc6),
                   S::screenshot("requests_menu"), request_button(0),
                   S::screenshot("requests_torpedo_load"),
                   // Picks up the first stern tube's torpedo, then the Mark 27.
                   S::click(0x38 - 20, 0x9e + 8), S::screenshot("requests_torpedo_held"),
                   S::click(0x40 + 0x80 * 2, 0x10), S::click(0x40 + 0x80 * 4 + 4 + 0xf, 0x4e + 8),
                   S::click(0x40 + 0x80 * 4 + 4 + 0xf, 0x4e + 8), S::click(0x40 + 0x80 * 4 + 4 + 0xf, 0x4e + 8),
                   S::screenshot("requests_torpedo_limit"), S::click(0x1e0 + 0x46, 0x1bd + 8),
                   request_button(1), S::screenshot("requests_deck_gun"),
                   button((640 - 0x1a4) / 2, (480 - 0x158) / 2, (0x1a4 - 0xc6) / 2, 0x112, 0xc6),
                   request_button(kRequestEntries), button(5, base_y, 0x1e, 0x14a, 0xc6)};
  flow.resume(career);
  CHECK(f.host.script.empty());
}

}  // namespace
}  // namespace sh
