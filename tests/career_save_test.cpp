// Tests of the career records and files: ROSTER<n>.SAV and BESTCARS.SAV
// (the shipped saves round trip byte for byte), the roster list and slots,
// the career boats and the start of a new career.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "core/install.h"
#include "game/career.h"
#include "game/career_boats.h"
#include "game/career_save.h"
#include "game/mission_rules.h"
#include "game/saves_options.h"
#include "game_data.h"

using namespace sh;

namespace {

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_career_save_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// The career of `slot` dated `month` (0-based) / `year` (since 1930).
Career dated_career(std::int32_t month, std::int32_t year) {
  Career c;
  c.record.month = month;
  c.record.year = year;
  c.record.day = 1;
  c.summary.current_command = -1;
  return c;
}

}  // namespace

TEST_CASE("ROSTER0.SAV decodes and round-trips byte for byte", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<std::uint8_t> data = install.read("SAVE/ROSTER0.SAV");
  const RosterFile f = parse_roster(data);
  CHECK(f.version == 7);
  const CareerRecord& r = f.career.record;
  CHECK(r.name == "Mathias");
  CHECK(r.start_month == 3);
  CHECK(r.start_day == 2);
  CHECK(r.start_year == 15);
  CHECK(r.month == 5);
  CHECK(r.day == 1);
  CHECK(r.year == 15);
  CHECK(r.hour == 18);
  CHECK(r.status == CareerStatus::kInPort);
  CHECK(r.roster_slot == 0);
  CHECK(r.boat == "(SS-337) Carbonero");
  CHECK(r.class_index == 7);
  CHECK(r.class_name == "BALAO");
  CHECK(r.class_title == "BALAO");
  CHECK(r.fuel_capacity == 117000);
  CHECK(r.fuel_use == 7209);
  CHECK(r.transit_speed == 20);
  CHECK(r.last_patrol_zone == 9);
  CHECK(r.equipment == 239);
  CHECK(r.home_port == "Darwin");
  CHECK(r.location == "Darwin");
  CHECK(r.patrols == 2);
  CHECK(r.tonnage == 2370);
  CHECK(r.ships_sunk == 5);
  CHECK(r.score == 2090);
  CHECK(r.unread_score == 2090);  // the same sum, never read
  CHECK(r.award(Award::kBronzeStar) == 1);
  CHECK(r.transit_hours == 378);
  CHECK(r.seed == 10416);
  CHECK(r.realism == std::array<std::int32_t, 12>{2, 2, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1});
  CHECK(r.thermal_layer_ft == 684);
  CHECK(r.log_count == 5);
  const CareerSummary& s = f.career.summary;
  CHECK(s.preferred_command == -1);
  CHECK(s.current_command == 2);
  CHECK(s.patrols_since_transfer == 2);
  CHECK(s.same_zone_repeats == 1);
  CHECK(s.fog_pct == 32);
  REQUIRE(f.career.log.size() == 5);
  const SunkListEntry& e = f.career.log[0].event;
  CHECK(e.log.day == 11);
  CHECK(e.log.month == 3);
  CHECK(e.log.type == 21);
  CHECK(e.log.tonnage == 70);
  CHECK(e.log.latitude.degrees == 1);
  CHECK(e.log.latitude.minutes == 48);
  CHECK(e.log.latitude.hemisphere == 'N');
  CHECK(e.status == SunkStatus::kSunk);
  CHECK(f.career.patrol_state.empty());

  CHECK(serialize_roster(f.career) == data);
}

TEST_CASE("Roster files: older versions, the unsunk log entries", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<std::uint8_t> data = install.read("SAVE/ROSTER0.SAV");
  // A version 4 file: no summary.
  std::vector<std::uint8_t> v4(data.begin(), data.begin() + 1 + 0x451);
  v4[0] = 4;
  v4.insert(v4.end(), data.begin() + 0x4c6, data.end());
  RosterFile f = parse_roster(v4);
  CHECK(f.career.summary.preferred_command == -1);
  CHECK(f.career.summary.current_command == -1);
  CHECK(f.career.summary.patrols_since_transfer == 2);
  CHECK(f.career.summary.fog_pct == 0);
  CHECK(f.career.log.size() == 5);
  CHECK(f.career.record.photo_recons == 0);

  // Only the sunk entries are written, and counted.
  f.career.log[1].event.status = SunkStatus::kDamaged;
  const RosterFile again = parse_roster(serialize_roster(f.career));
  CHECK(again.version == 7);
  CHECK(again.career.record.log_count == 4);
  CHECK(again.career.log.size() == 4);
}

TEST_CASE("Roster slots: save, list, save as, delete", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  TempDir dir;
  const SaveDir saves(dir.path(), &install, "SAVE/");

  // The install's ROSTER0.SAV is listed and takes slot 0.
  std::vector<RosterEntry> list = list_rosters(saves);
  REQUIRE(list.size() == 1);
  CHECK(list[0].file == "ROSTER0.SAV");
  CHECK(list[0].record.name == "Mathias");
  CHECK(find_free_roster_slot(saves) == 1);

  Career career = career_from_roster_entry(list[0], tables);
  CHECK(career.summary.current_command == 2);
  career.log = load_career_log(saves, career.record);
  CHECK(career.log.size() == 5);

  WatcomRandom random(5);
  const std::uint32_t old_seed = career.record.seed;
  const std::int32_t slot = save_career_as(saves, career, "Copy", random);
  CHECK(slot == 1);
  CHECK(career.record.seed != old_seed);
  CHECK(career.record.name == "Mathias");
  const std::optional<RosterFile> copy = load_roster(saves, 1);
  REQUIRE(copy);
  CHECK(copy->career.record.name == "Copy");
  CHECK(copy->career.record.roster_slot == 1);
  CHECK(copy->career.log.size() == 5);
  CHECK(list_rosters(saves).size() == 2);

  // Deleting the install's file frees its slot.
  delete_roster(saves, 0);
  list = list_rosters(saves);
  REQUIRE(list.size() == 1);
  CHECK(list[0].file == "ROSTER1.SAV");
  CHECK(find_free_roster_slot(saves) == 0);
  delete_roster(saves, 1);
  CHECK(list_rosters(saves).empty());
}

TEST_CASE("BESTCARS.SAV decodes, round-trips and ranks careers", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const std::vector<std::uint8_t> data = install.read("SAVE/BESTCARS.SAV");
  std::optional<BestCareersTable> table = BestCareersTable::parse(data);
  REQUIRE(table);
  CHECK(table->serialize() == data);
  REQUIRE(table->entries[0]);
  REQUIRE(table->entries[1]);
  REQUIRE(table->entries[2]);
  CHECK_FALSE(table->entries[3]);
  const BestCareerEntry& best = *table->entries[0];
  CHECK(best.record.name == "Mathias");
  CHECK(best.record.status == CareerStatus::kDeceased);
  CHECK(best.record.boat == "(SS-306) Tang");
  CHECK(best.record.score == 177771);
  CHECK(best.record.tonnage == 272998);
  CHECK(best.record.ships_sunk == 50);
  REQUIRE(best.log.size() == 51);
  CHECK(best.log[0].event.log.name == "FUBUKI");
  CHECK(best.log[0].event.log.tonnage == 2090);
  CHECK(table->entries[1]->record.name == "Mathias 2");
  CHECK(table->entries[1]->record.score == 44202);
  CHECK(table->entries[2]->record.name == "Mat");

  CHECK_FALSE(table->rank_of(0));
  CHECK(table->rank_of(200000) == 0u);
  CHECK(table->rank_of(44202) == 1u);
  CHECK(table->rank_of(1) == 3u);

  BestCareerEntry entry;
  entry.record.name = "New";
  entry.record.score = 50000;
  entry.log.resize(450);
  table->insert(1, entry);
  CHECK(table->entries[1]->record.name == "New");
  CHECK(table->entries[1]->log.size() == 400);
  CHECK(table->entries[2]->record.name == "Mathias 2");
  CHECK(table->entries[3]->record.name == "Mat");
  const std::optional<BestCareersTable> reread = BestCareersTable::parse(table->serialize());
  REQUIRE(reread);
  CHECK(reread->entries[1]->record.score == 50000);
  CHECK(reread->entries[1]->log.size() == 400);
  CHECK(reread->entries[3]->record.name == "Mat");
  CHECK_FALSE(reread->entries[4]);

  const ExeImage exe = ExeImage::load(install);
  const Font font = Font::load(install, "ART/MENU_S.FNT");
  const std::string text = career_end_summary(best, exe, font);
  CHECK(text.starts_with("Name: "));
  CHECK(text.find("(SS-306) Tang") != std::string::npos);
  CHECK(text.find("Deceased") != std::string::npos);
  CHECK(text.find("Bronze Star (3)\n") != std::string::npos);
  CHECK(text.find("41-58N, 141-07E") != std::string::npos);
  CHECK(text.find("(FUBUKI)") != std::string::npos);
  CHECK(text.ends_with("***END OF CAREER SUMMARY***"));
  const std::vector<std::string> rows = best_careers_rows(*reread, Font::load(install, "ART/MENU_L.FNT"));
  REQUIRE(rows.size() == 4);
  CHECK(rows[0].starts_with("1"));
  CHECK(rows[1].find("New") != std::string::npos);
  CHECK(rows[1].ends_with("0"));
}

TEST_CASE("BESTCARS.SAV is created when missing", "[career]") {
  TempDir dir;
  const SaveDir saves(dir.path());
  const std::optional<BestCareersTable> table = BestCareersTable::load(saves);
  REQUIRE(table);
  CHECK_FALSE(table->entries[0]);
  CHECK(std::filesystem::file_size(dir.path() / "BESTCARS.SAV") == 1 + 10 * 0x8609);
}

TEST_CASE("Career boats by date and command", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::vector<CareerBoatClass> classes = career_boat_classes(tables);
  REQUIRE(classes.size() == 7);  // all but the Barracuda and Narwhal
  CHECK(classes[0].name == "S CLASS");
  CHECK(classes[0].last_month == 2);
  CHECK(classes[0].last_year == 1942);
  CHECK(classes[6].name == "TENCH");
  for (const CareerBoatClass& c : classes) {
    for (std::int32_t b = c.first_boat; b <= c.last_boat; ++b) {
      CHECK(tables.carsub[static_cast<std::size_t>(b)].class_index == static_cast<std::uint32_t>(c.class_index));
    }
  }

  WatcomRandom random(1);
  // December 1941: an S-boat, a P-boat at Pearl Harbor.
  Career c = dated_career(11, 11);
  REQUIRE(assign_new_boat(c.record, c.summary, SubCommand::kComSubsAsiatic, tables, random));
  CHECK(c.record.class_name == "S CLASS");
  CHECK(c.record.boat.starts_with("(SS-"));
  c = dated_career(11, 11);
  REQUIRE(assign_new_boat(c.record, c.summary, SubCommand::kComSubPac, tables, random));
  CHECK(c.record.class_name == "P CLASS");

  // March 1943: a Gato.
  c = dated_career(2, 13);
  REQUIRE(assign_new_boat(c.record, c.summary, SubCommand::kComSubPac, tables, random));
  CHECK(c.record.class_index == 6);
  CHECK(c.record.class_title == "GATO");
  CHECK(c.record.fuel_capacity == 117000);
  CHECK(c.record.fuel_use == 7209);
  CHECK(c.record.transit_speed == 20);
  const CareerBoatClass& gato = classes[4];
  REQUIRE(gato.name == "GATO");
  bool listed = false;
  for (std::int32_t b = gato.first_boat; b <= gato.last_boat; ++b) {
    const CarsubBoat& boat = tables.carsub[static_cast<std::size_t>(b)];
    listed = listed || c.record.boat == "(SS-" + std::to_string(boat.hull_number) + ") " + boat.name;
  }
  CHECK(listed);

  // An upgrade in October 1944: a Balao for a Gato commander.
  c.record.patrols = 3;
  c.record.month = 9;
  c.record.year = 14;
  REQUIRE(assign_new_boat(c.record, c.summary, SubCommand::kComSubsAsiatic, tables, random));
  CHECK(c.record.class_name == "BALAO");
  // No upgrade past the Tench.
  c.record.class_index = 8;
  CHECK_FALSE(assign_new_boat(c.record, c.summary, SubCommand::kComSubPac, tables, random));
}

TEST_CASE("New career choices and record", "[career]") {
  NewCareerChoice choice;
  CHECK(choice.date_in_war());
  choice.next_month();  // no other month of 1941 is in the war
  CHECK(choice.month == 11);
  CHECK(choice.year == 11);
  CHECK(choice.day == 7);
  choice.next_year();
  CHECK(choice.year == 12);
  CHECK(choice.day == 1);
  choice.next_year();
  choice.next_year();
  choice.next_year();  // December 1945 is past the war
  CHECK(choice.year == 11);
  CHECK(choice.day == 7);
  choice.year = 15;
  choice.month = 7;
  choice.day = 1;
  choice.next_month();  // September 1945 is past the war
  CHECK(choice.month == 0);
  choice.next_command();
  CHECK(choice.preferred_command() == 1);
  choice.next_command();
  choice.next_command();
  CHECK(choice.preferred_command() == -1);

  // The new career's equipment comes from SH.EXE's table.
  NewCareerChoice start;
  start.name = "Dick";
  start.command = 1;
  WatcomRandom random(1);
  WatcomRandom expected(1);
  const std::int32_t day = 8 + expected.random_int(3);
  const auto seed = static_cast<std::uint32_t>(expected.rand());
  const Career c = career_init_new(start, 0x61a8, random);
  CHECK(c.record.name == "Dick");
  CHECK(c.record.status == CareerStatus::kInPort);
  CHECK(c.record.start_day == day);
  CHECK(c.record.day == day);
  CHECK(c.record.month == 11);
  CHECK(c.record.year == 11);
  CHECK(c.record.seed == seed);
  CHECK(c.record.patrols_since_new_boat == 1);
  CHECK(c.record.realism == std::array<std::int32_t, 12>{2, 2, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1});
  CHECK(c.record.equipment == 0);
  CHECK(c.summary.preferred_command == 1);
  CHECK(c.summary.current_command == -1);
}

TEST_CASE("Equipment deliveries by date", "[career]") {
  // April 1943, outside ComSubPac: the refit items come 3 months late, but
  // the new career reads the supply flag of the last letter's record.
  Career c = dated_career(3, 13);
  CHECK(equipment_for_new_career(c.record, c.summary, 0) == 0x03);
  CHECK(equipment_for_new_career(c.record, c.summary, 7) == 0x23);
  c.record.equipment = 0x03;
  CHECK(deliver_next_equipment(c.record, c.summary) == -1);
  c.summary.current_command = 1;
  CHECK(deliver_next_equipment(c.record, c.summary) == 2);
  CHECK(c.record.equipment == 0x23);
  CHECK(deliver_next_equipment(c.record, c.summary) == -1);
  // August 1944: everything.
  c = dated_career(7, 14);
  c.summary.current_command = 1;
  std::int32_t delivered = 0;
  while (deliver_next_equipment(c.record, c.summary) >= 0) {
    ++delivered;
  }
  CHECK(delivered == 7);
  CHECK(c.record.equipment == 0xef);
}

TEST_CASE("With the rules' corrections a new career's equipment follows each delivery's own flag",
          "[career][rules]") {
  const MissionRules fixed = MissionRules::extended();
  for (const auto& [month, year] : {std::pair{3, 13}, std::pair{11, 12}, std::pair{7, 14}}) {
    for (const std::int32_t command : {-1, 1}) {
      Career c = dated_career(month, year);
      c.summary.current_command = command;
      // The deliveries due by their own flags, whatever the last letter.
      std::uint32_t due = 0;
      for (std::size_t i = 0; i < equipment_deliveries().size(); ++i) {
        if (equipment_due(i, c.record, c.summary)) {
          due |= equipment_deliveries()[i].bit;
        }
      }
      CHECK(equipment_for_new_career(c.record, c.summary, 0, fixed) == due);
      CHECK(equipment_for_new_career(c.record, c.summary, 7, fixed) == due);
    }
  }
  // The original's rules keep the last letter's flag.
  const Career c = dated_career(3, 13);
  CHECK(equipment_for_new_career(c.record, c.summary, 0, MissionRules{}) == 0x03);
  CHECK(equipment_for_new_career(c.record, c.summary, 7, MissionRules{}) == 0x23);
}

TEST_CASE("With the rules' corrections a new boat is in service and stays in its class", "[career][rules][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const MissionRules fixed = MissionRules::extended();
  const auto full_year = [](std::uint32_t y) { return static_cast<std::int32_t>(y < 100 ? 1900 + y : y); };
  // Returns the CARSUB boat named `name` ("(SS-n) Name").
  const auto boat_of = [&tables](const std::string& name) -> const CarsubBoat* {
    for (const CarsubBoat& b : tables.carsub) {
      if (name == "(SS-" + std::to_string(b.hull_number) + ") " + b.name) {
        return &b;
      }
    }
    return nullptr;
  };
  std::int32_t original_late = 0;
  std::int32_t original_fallbacks = 0;
  for (std::uint32_t seed = 1; seed <= 200; ++seed) {
    // March 1943, ComSubPac: a Gato, commissioned by then.
    WatcomRandom random(seed);
    Career c = dated_career(2, 13);
    REQUIRE(assign_new_boat(c.record, c.summary, SubCommand::kComSubPac, tables, random, fixed));
    REQUIRE(c.record.class_name == "GATO");
    const CarsubBoat* boat = boat_of(c.record.boat);
    REQUIRE(boat != nullptr);
    CHECK(full_year(boat->avail_year) * 12 + static_cast<std::int32_t>(boat->avail_month) <= 1943 * 12 + 3);
    // The second assignment of a new career: another Gato.
    const std::string first = c.record.boat;
    REQUIRE(assign_new_boat(c.record, c.summary, SubCommand::kComSubPac, tables, random, fixed));
    CHECK(c.record.class_name == "GATO");
    CHECK(c.record.boat != first);

    // The original's: any Gato hull, and the S-boats after a repeat.
    WatcomRandom r(seed);
    Career o = dated_career(2, 13);
    REQUIRE(assign_new_boat(o.record, o.summary, SubCommand::kComSubPac, tables, r));
    const CarsubBoat* ob = boat_of(o.record.boat);
    REQUIRE(ob != nullptr);
    original_late += full_year(ob->avail_year) * 12 + static_cast<std::int32_t>(ob->avail_month) > 1943 * 12 + 3;
    REQUIRE(assign_new_boat(o.record, o.summary, SubCommand::kComSubPac, tables, r));
    original_fallbacks += o.record.class_name == "S CLASS" ? 1 : 0;
  }
  CHECK(original_late > 0);
  CHECK(original_fallbacks > 0);
}

TEST_CASE("A career's rules are kept in ROSTER<n>.EXT, written only when it has some", "[career][rules]") {
  TempDir dir;
  const SaveDir saves(dir.path());
  Career c = dated_career(2, 13);
  c.record.roster_slot = 3;
  REQUIRE(save_career(saves, c));
  CHECK_FALSE(std::filesystem::exists(dir.path() / "ROSTER3.EXT"));
  CHECK_FALSE(load_career_rules(saves, 3));

  MissionRules keen = MissionRules::extended();
  keen.keen_escort_sonar = true;
  c.rules = keen;
  REQUIRE(save_career(saves, c));
  CHECK(std::filesystem::exists(dir.path() / "ROSTER3.EXT"));
  CHECK(load_career_rules(saves, 3) == keen);
  // A file without a rule's line gives that rule the extended default.
  REQUIRE(saves.write("ROSTER3.EXT", std::vector<std::uint8_t>{}));
  CHECK(load_career_rules(saves, 3) == MissionRules::extended());

  delete_roster(saves, 3);
  CHECK_FALSE(std::filesystem::exists(dir.path() / "ROSTER3.SAV"));
  CHECK_FALSE(std::filesystem::exists(dir.path() / "ROSTER3.EXT"));
}

TEST_CASE("Command names and title from SH.EXE", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  NewCareerChoice choice;
  CHECK(new_career_command_names(choice, exe) == std::array<std::string, 3>{"Random", "ComSubPac", "ComSubsAsiatic"});
  choice.year = 13;
  CHECK(new_career_command_names(choice, exe)[2] == "ComSubSoWesPac");
  CareerRecord r;
  r.name = "Dick";
  CHECK(career_commander_title(r, exe) == "Commander Dick");
  CHECK(std::string(exe.c_string(equipment_deliveries()[2].name)) == "Bathythermograph");
}
