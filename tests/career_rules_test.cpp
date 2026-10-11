// The career rules (game/career_rules.h, game/awards.h) on synthetic
// careers and the real HOMEPORT.DAT, PZ.DAT and CMDLIST.DAT.

#include <catch2/catch_test_macros.hpp>

#include "assets/dat_tables.h"
#include "core/install.h"
#include "game/awards.h"
#include "game/career_rules.h"
#include "game_data.h"

namespace sh {
namespace {

// A career at Darwin on 1 June 1945 in the Balao of ROSTER0.SAV.
Career darwin_career() {
  Career c;
  CareerRecord& r = c.record;
  r.day = 1;
  r.month = 5;
  r.year = 15;
  r.status = CareerStatus::kInPort;
  r.home_port = "Darwin";
  r.location = "Darwin";
  r.fuel_capacity = 117000;
  r.fuel_use = 7209;
  r.transit_speed = 20;
  r.last_patrol_zone = 9;
  r.class_index = 7;
  c.summary.preferred_command = -1;
  c.summary.current_command = 2;
  c.summary.same_zone_repeats = 1;
  return c;
}

CampaignData all_zones(const Install& install) {
  const GameTables tables = GameTables::load(install);
  CampaignData data = CampaignData::load(install, tables);
  return data;
}

}  // namespace

TEST_CASE("career calendar", "[career]") {
  CareerRecord r;
  r.day = 31;
  r.month = 0;
  r.year = 12;
  career_advance_hours(r, 23);
  CHECK(r.day == 31);
  career_advance_hours(r, 24);
  CHECK((r.day == 1 && r.month == 1 && r.year == 12));
  r.day = 31;
  r.month = 11;
  career_advance_hours(r, 48);
  CHECK((r.day == 2 && r.month == 0 && r.year == 13));

  CHECK_FALSE(date_after_war_end(14, 7, 15));
  CHECK(date_after_war_end(15, 7, 15));
  CHECK(date_after_war_end(1, 0, 16));
  CHECK(campaign_month_index_abs(3, 1941) == 0);
  CHECK(campaign_month_index_abs(0, 1942) == 1);
  CHECK(campaign_month_index_abs(7, 1945) == 44);
}

TEST_CASE("career patrol zone pick and transit", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const CampaignData data = all_zones(install);
  REQUIRE(data.zones.size() == 15);
  CHECK(data.all_maps);

  Career c = darwin_career();
  // Darwin in June 1945 (campaign month 42): Malaysia 30, Sulu Sea 10,
  // South China Sea 35, Indochina 35, Java Sea 30; Malaysia (zone 9, the
  // last zone, after a repeat) is left out.
  WatcomRandom random(1234);
  WatcomRandom check(1234);
  const std::int32_t draw = check.random_int(110);
  const std::int32_t expected = draw < 10 ? 10 : draw < 45 ? 11 : draw < 80 ? 12 : 13;
  const std::optional<std::int32_t> zone = pick_patrol_zone(c, data, random);
  REQUIRE(zone);
  CHECK(*zone == expected);
  CHECK(c.record.last_patrol_zone == expected);

  // Without the repeat, Malaysia is in: weights 30 first.
  Career d = darwin_career();
  d.summary.same_zone_repeats = 0;
  WatcomRandom r2(99);
  WatcomRandom c2(99);
  const std::int32_t draw2 = c2.random_int(140);
  const std::int32_t expected2 = draw2 < 30 ? 9 : draw2 < 40 ? 10 : draw2 < 75 ? 11 : draw2 < 110 ? 12 : 13;
  CHECK(pick_patrol_zone(d, data, r2) == expected2);

  // On patrol, the last zone.
  Career e = darwin_career();
  e.record.on_patrol = 1;
  CHECK(pick_patrol_zone(e, data, random) == 9);

  // 2500 nm at 2/3 of 20 kn (13) = 192 h; 7209 / 65536 x 3600 = 396 per
  // hour, 35 % = 138, x 192.
  const std::optional<HomePort> darwin = find_home_port(data, c.record, "DARWIN");
  REQUIRE(darwin);
  const Transit t = compute_transit(*darwin, 9, c.record);
  CHECK(t.hours == 192);
  CHECK(t.fuel == 138 * 192);

  Career f = darwin_career();
  WatcomRandom r3(5);
  std::optional<PatrolAssignment> a = assign_patrol(f, data, r3);
  REQUIRE(a);
  CHECK(a->previous_zone == 9);
  CHECK(a->zone.name == data.zones[static_cast<std::size_t>(a->zone_index)].name);
  CHECK(a->transit_fuel * 2 < 117000 - 11700);
  accept_patrol_zone(f, *a);
  CHECK(f.summary.same_zone_repeats == 0);
}

TEST_CASE("career home ports by date", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const CampaignData data = all_zones(install);
  CareerRecord r;
  r.day = 1;
  r.month = 5;
  r.year = 15;
  std::vector<std::string> names;
  for (const std::size_t i : open_home_ports(data, r)) {
    names.push_back(data.home_ports[i].name);
  }
  CHECK(names == std::vector<std::string>{"Darwin", "Guam"});
  // Manila on 7 December 1941, its ComSubSoWesPac read as ComSubsAsiatic.
  r.day = 7;
  r.month = 11;
  r.year = 11;
  const std::optional<HomePort> manila = find_home_port(data, r, "Manila");
  REQUIRE(manila);
  CHECK(manila->command == SubCommand::kComSubsAsiatic);
  CHECK_FALSE(find_home_port(data, r, "Darwin"));
}

TEST_CASE("career transfers", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const CampaignData data = all_zones(install);
  Career c = darwin_career();
  WatcomRandom random(7);
  // Same command: Darwin is the only one, and it is the home port.
  CHECK_FALSE(pick_transfer_port(c, data, false, random));
  // Other command: Guam (ComSubPac).
  const std::optional<HomePort> guam = pick_transfer_port(c, data, true, random);
  REQUIRE(guam);
  CHECK(guam->name == "Guam");

  PatrolAssignment a;
  WatcomRandom r2(11);
  WatcomRandom c2(11);
  const std::int32_t days = 10 + c2.random_int(10);
  const std::optional<HomePort> moved = transfer_home_port(c, a, data, true, false, r2);
  REQUIRE(moved);
  CHECK(a.travel_days == days);
  CHECK(c.record.home_port == "Guam");
  CHECK(c.record.location == "Guam");
  CHECK(c.summary.current_command == 1);
  CHECK(c.record.day == 1 + days);

  // The same zone twice in a row forces a transfer; without a port the
  // date is put back.
  Career d = darwin_career();
  d.summary.same_zone_repeats = 2;
  d.summary.current_command = 2;
  d.summary.patrols_since_transfer = 0;
  PatrolAssignment b;
  WatcomRandom r3(3);
  CHECK_FALSE(check_transfer(d, b, data, r3));
  CHECK(d.record.day == 1);
  CHECK(b.travel_days == 0);
}

TEST_CASE("career patrol bookkeeping", "[career]") {
  Career c;
  CareerRecord& r = c.record;
  r.day = 1;
  r.month = 2;
  r.year = 13;
  r.home_port = "Fremantle";
  r.on_patrol = -1;
  MissionResults res;
  res.day = 20;
  res.month = 3;
  res.year = 13;
  res.ships_sunk = 3;
  res.sampans_sunk = 1;
  res.tonnage = 12000;
  res.score = 9000;
  res.hull_damage_pct = 40;
  res.system_damage_pct = 50;
  PatrolAssignment a;
  WatcomRandom random(42);
  WatcomRandom check(42);
  // 10 + (0 / 2 - 0) + 10 + (0 / 2 - 0) days: no spread below 20 days.
  check.random_int(0);
  check.random_int(0);
  const std::int32_t refit = check.random_int(7) + 5;
  const PatrolBookkeeping b = apply_patrol_results(c, a, res, random);
  CHECK_FALSE(b.career_over);
  CHECK(a.repair_days == 20);
  CHECK(a.refit_days == refit);
  CHECK((r.patrols == 1 && r.promotion_patrols == 1 && r.patrols_from_port == 1));
  CHECK((r.ships_sunk == 3 && r.tonnage == 12000 && r.score == 9000 && r.sunk_since_bronze_star == 3));
  CHECK(r.status == CareerStatus::kInPort);
  CHECK(r.location == "Fremantle");
  CHECK(r.on_patrol == 0);
  // The career's date is the results' date.
  CHECK((r.day == 20 && r.month == 3 && r.year == 13));
  // Two ships besides sampans: a high-realism patrol.
  CHECK((r.high_realism_patrols == 1 && r.nuc_streak == 1 && r.puc_streak == 1));
  CHECK(r.poor_patrol_streak == 0);

  // Nothing sunk: a poor patrol.
  MissionResults nothing;
  nothing.day = 1;
  nothing.month = 5;
  nothing.year = 13;
  apply_patrol_results(c, a, nothing, random);
  CHECK(r.poor_patrol_streak == 1);
  CHECK(r.puc_streak == 0);
  CHECK(r.nuc_streak == 1);

  // Killed.
  MissionResults lost;
  lost.fate = BoatFate::kKilled;
  const PatrolBookkeeping k = apply_patrol_results(c, a, lost, random);
  CHECK(k.career_over);
  CHECK(r.status == CareerStatus::kDeceased);
  CHECK(r.location == "****");

  // The time in port runs past the war's end.
  Career e;
  MissionResults late;
  late.day = 10;
  late.month = 7;
  late.year = 15;
  e.record.day = 10;
  e.record.month = 7;
  e.record.year = 15;
  const PatrolBookkeeping w = apply_patrol_results(e, a, late, random);
  CHECK(w.war_ended);
  CHECK(late.fate == BoatFate::kRecalled);
  CHECK(e.record.status == CareerStatus::kRetired);
  CHECK((e.record.day == 15 && e.record.month == 7 && e.record.year == 15));
}

TEST_CASE("With the rules' corrections a patrol counts for the unit awards by ships and realism",
          "[career][rules]") {
  // ships x (100 + realism %) >= 400: 4 ships below 34 %, 3 below 100 %, 2
  // from 100 %; the original counts 2 or more ships whatever the realism.
  struct Case {
    std::int32_t ships;
    std::int32_t realism_pct;
    bool fixed_counts;
  };
  for (const Case& k : {Case{2, 100, true}, Case{2, 99, false}, Case{3, 34, true}, Case{3, 33, false},
                        Case{4, 8, true}, Case{1, 126, false}}) {
    for (const bool fixed : {false, true}) {
      Career c;
      c.record.day = 1;
      c.record.month = 2;
      c.record.year = 13;
      if (fixed) {
        c.rules = MissionRules::extended();
      }
      MissionResults res;
      res.day = 20;
      res.month = 3;
      res.year = 13;
      res.ships_sunk = k.ships;
      res.realism_pct = k.realism_pct;
      PatrolAssignment a;
      WatcomRandom random(42);
      apply_patrol_results(c, a, res, random);
      const bool counts = fixed ? k.fixed_counts : k.ships >= 2;
      CHECK(c.record.high_realism_patrols == (counts ? 1 : 0));
      CHECK(c.record.nuc_streak == (counts ? 1 : 0));
    }
  }
}

TEST_CASE("career awards", "[career]") {
  MissionResults res;
  Career c;
  c.record.status = CareerStatus::kInPort;
  res.warships_sunk = 1;
  c.record.score = 50000;
  // The Silver Star blocks the Navy Cross and the Medal of Honor.
  CHECK(check_awards(c, res) == std::vector<AwardCode>{AwardCode::kSilverStar});
  res.warships_sunk = 0;
  CHECK(check_awards(c, res) == std::vector<AwardCode>{AwardCode::kNavyCross});
  CHECK(check_awards(c, res).empty());

  Career h;
  h.record.score = 100000;
  h.record.award(Award::kNavyCross) = 5;
  h.record.high_realism_patrols = 5;
  h.record.sunk_since_bronze_star = 4;
  h.record.puc_streak = 6;
  h.record.nuc_streak = 3;
  CHECK(check_awards(h, res) == std::vector<AwardCode>{AwardCode::kMedalOfHonor, AwardCode::kDsm,
                                                        AwardCode::kBronzeStar, AwardCode::kPuc});
  CHECK(h.record.high_realism_patrols == 0);
  CHECK(h.record.sunk_since_bronze_star == 0);
  CHECK(h.record.nuc_streak == 3);
  CHECK(check_awards(h, res) == std::vector<AwardCode>{AwardCode::kNuc});
  CHECK(h.record.award(Award::kMedalOfHonor) == 1);

  // Damaged above 50 %: the Silver Star for ships besides sampans.
  Career d;
  MissionResults hit;
  hit.hull_damage_pct = 60;
  hit.system_damage_pct = 50;
  hit.ships_sunk = 2;
  hit.sampans_sunk = 1;
  CHECK(check_awards(d, hit) == std::vector<AwardCode>{AwardCode::kSilverStar});

  // The Asiatic-Pacific medal at the war's end after 5 patrols; nothing
  // during a poor-patrol streak.
  Career end;
  end.record.patrols = 5;
  MissionResults over;
  over.fate = BoatFate::kRecalled;
  CHECK(check_awards(end, over) == std::vector<AwardCode>{AwardCode::kAsiaticPacific});
  end.record.poor_patrol_streak = 1;
  CHECK(check_awards(end, over).empty());
}

TEST_CASE("career relieved of command", "[career]") {
  Career c;
  c.record.status = CareerStatus::kInPort;
  MissionResults res;
  res.fuel = 0;
  c.record.fuel_failures = 2;
  CHECK(check_relieved(c, res) == std::vector<AwardCode>{AwardCode::kPoorFuel});
  CHECK(c.record.status == CareerStatus::kInPort);
  CHECK(check_relieved(c, res) == std::vector<AwardCode>{AwardCode::kPoorFuel, AwardCode::kRelieved});
  CHECK(c.record.status == CareerStatus::kRelieved);

  Career p;
  p.record.status = CareerStatus::kInPort;
  MissionResults fuel;
  fuel.fuel = 1000;
  p.record.poor_patrol_streak = 3;
  CHECK(check_relieved(p, fuel) == std::vector<AwardCode>{AwardCode::kPoorPatrol, AwardCode::kRelieved});

  Career b;
  b.record.status = CareerStatus::kInPort;
  b.record.boats_lost = 2;
  CHECK(check_relieved(b, fuel) == std::vector<AwardCode>{AwardCode::kRelieved});
  // Not in port: no check.
  Career dead;
  dead.record.status = CareerStatus::kDeceased;
  dead.record.boats_lost = 2;
  CHECK(check_relieved(dead, fuel).empty());
}

TEST_CASE("career promotions and new boats", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const CampaignData data = all_zones(install);
  Career c = darwin_career();
  c.record.promotion_patrols = 2;
  CHECK_FALSE(check_new_command(c, data).promoted);
  c.record.promotion_patrols = 3;
  CHECK(check_new_command(c, data).promoted);
  CHECK(c.record.rank == 1);
  c.record.promotion_patrols = 5;
  CHECK_FALSE(check_new_command(c, data).promoted);
  c.record.promotion_patrols = 6;
  CHECK(check_new_command(c, data).promoted);
  CHECK(c.record.rank == 2);
  CHECK_FALSE(check_new_command(c, data).promoted);

  // A Balao (class 7) sinking more ships than patrols after 4 patrols in
  // the boat.
  c.record.patrols = 4;
  c.record.ships_sunk = 5;
  c.record.patrols_since_new_boat = 4;
  const NewCommand n = check_new_command(c, data);
  CHECK(n.offer_boat);
  CHECK(n.from_fleet_boat);
  CHECK(c.record.patrols_since_new_boat == 0);
  // An S-boat after 3 patrols.
  Career s = darwin_career();
  s.record.class_index = 0;
  s.record.patrols = 3;
  const NewCommand m = check_new_command(s, data);
  CHECK(m.offer_boat);
  CHECK_FALSE(m.from_fleet_boat);
}

TEST_CASE("career top commanders", "[career]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  CareerRecord r;
  r.day = 20;
  r.month = 7;
  r.year = 15;
  r.tonnage = 80000;
  const std::vector<TopCommanderRow> rows = top_commanders(tables.commanders, r);
  REQUIRE(rows.size() == 10);
  CHECK(tables.commanders[static_cast<std::size_t>(rows[0].commander)].name == "Fluckey, E. B.");
  CHECK(rows[0].tonnage == 94409);
  CHECK(rows[1].tonnage == 93874);
  CHECK(rows[2].player);
  CHECK(rows[2].tonnage == 80000);
  CHECK(tables.commanders[static_cast<std::size_t>(rows[3].commander)].name == "Underwood, G. W.");
  CHECK(rows[9].tonnage == 58837);  // Triebel, 9th commander

  // In December 1941 nobody has sunk anything yet: the player alone.
  r.month = 11;
  r.year = 11;
  r.tonnage = 0;
  const std::vector<TopCommanderRow> first = top_commanders(tables.commanders, r);
  REQUIRE(first.size() == 1);
  CHECK(first[0].player);
}

TEST_CASE("career award presentations", "[career]") {
  CHECK(award_of(AwardCode::kPuc) == Award::kPuc);
  CHECK_FALSE(award_of(AwardCode::kRelieved));
  CHECK(award_presentation(AwardCode::kBronzeStar).medal_sprite);
  CHECK(award_presentation(AwardCode::kPuc).box_y == 0xaf);
  CHECK(award_presentation(AwardCode::kPoorFuel).layout == CitationLayout::kLetter);
}

TEST_CASE("career realism options", "[career]") {
  CareerRecord r;
  r.realism = {2, 2, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1};
  // ROSTER0's values: combat 2, sailing 2, visibility, reloads, duds,
  // aground; the mode bit and the radar bits stay.
  CHECK(career_realism_options(0x30001u | 0xfffcu, r) ==
        (0x30001u | 2u << 2 | 2u << 4 | 0x80u | 0x2000u | 0x4000u | 0x100u));
}

}  // namespace sh
