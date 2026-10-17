// The career's menu screens driven by synthetic input: CAREER MENU, START A
// CAREER, CAREER ROSTER and BASE MENU, and the texts of the roster and
// VIEW CAREER from the shipped ROSTER0.SAV.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "core/install.h"
#include "assets/dat_tables.h"
#include "assets/scenario.h"
#include "game/career_boats.h"
#include "game/career_flow.h"
#include "game/career_menus.h"
#include "game/career_save.h"
#include "game/career_ui.h"
#include "game/frontend.h"
#include "game/mission.h"
#include "game/patrol.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "career_fixture.h"
#include "game_data.h"
#include "ui/dialog.h"
#include "ui/menu_skin.h"

namespace sh {
namespace {

using test::Fixture;
using test::ScriptedHost;
using test::button;

TEST_CASE("CAREER MENU returns the code of its buttons", "[career]") {
  SH_REQUIRE_INSTALL(install);
  Fixture f(install);
  const auto at = [](int y) { return button(0x159, 0x4b, 0x23, y, 0xc6); };
  f.host.script = {at(0xaa)};
  CHECK(career_menu_screen(f.ui) == career_code::kContinueCareer);
  f.host.script = {at(0xfa)};
  CHECK(career_menu_screen(f.ui) == career_code::kMainMenu);
  f.host.script = {at(0x82)};
  CHECK(career_menu_screen(f.ui) == career_code::kStartCareer);
}

TEST_CASE("BASE MENU lays out its optional buttons and confirms a retirement", "[career]") {
  SH_REQUIRE_INSTALL(install);
  Fixture f(install);
  Career career;
  career.record.name = "Mathias";
  career.record.status = CareerStatus::kInPort;
  int saves = 0;
  const auto save = [&saves] { ++saves; };

  SECTION("Without transfer nor retirement SAVE CAREER follows VIEW CAREER") {
    // The dialog is 0xfa + 0x6e high, centred.
    const int dy = (480 - (0xfa + 0x6e)) / 2;
    f.host.script = {button(5, dy, 0x1e, 0xfa, 0xc6), button(5, dy, 0x1e, 0x122, 0xc6)};
    CHECK(base_menu_screen(f.ui, career, true, save) == career_code::kLeaveCareer);
    CHECK(saves == 1);
  }
  SECTION("REQUEST TRANSFER and RETIRE take the next places") {
    career.summary.transfer_offered = 1;
    career.record.patrols = 5;
    const int dy = (480 - (0x14a + 0x6e)) / 2;
    // RETIRE, then YES in the box (NO on the left, YES on the right).
    f.host.script = {button(5, dy, 0x1e, 0x122, 0xc6), ScriptedHost::key(0x1559)};
    CHECK(base_menu_screen(f.ui, career, true, save) == career_code::kRetire);
    f.host.script = {button(5, dy, 0x1e, 0xfa, 0xc6), ScriptedHost::key(0x1559)};
    CHECK(base_menu_screen(f.ui, career, false, save) == career_code::kTransfer);
    // NO stays in the menu; MAIN MENU leaves.
    f.host.script = {button(5, dy, 0x1e, 0xfa, 0xc6), ScriptedHost::key(0x314e), button(5, dy, 0x1e, 0x172, 0xc6)};
    CHECK(base_menu_screen(f.ui, career, false, save) == career_code::kLeaveCareer);
    CHECK(saves == 0);
  }
  SECTION("A career on patrol goes back to sea at once") {
    career.record.on_patrol = -1;
    CHECK(base_menu_screen(f.ui, career, true, save) == career_code::kStartPatrol);
  }
}

TEST_CASE("START A CAREER takes a trimmed name and a date in the war", "[career]") {
  SH_REQUIRE_INSTALL(install);
  Fixture f(install);
  const auto at = [](int x, int y, int w) { return button(0x122, 0x73, x, y, w); };
  SECTION("CANCEL") {
    // The name entry has the focus and edits: Tab ends the edit.
    f.host.script = {ScriptedHost::key(0x0f09), at(0x14, 300, 0x79)};
    CHECK_FALSE(new_career_screen(f.ui, true).has_value());
  }
  SECTION("OK once a name is typed") {
    // The focused name entry edits at once: Tab ends the edit; OK is
    // disabled without a name.
    f.host.script = {ScriptedHost::key(0x0f09), at(0xc4, 300, 0x79),
                     ScriptedHost::click(0x122 + 0x14 + 20, 0x73 + 0xa4 + 17 + 4),
                     ScriptedHost::key(0x3920),  // a leading space, dropped
                     ScriptedHost::key(0x1e41),
                     ScriptedHost::key(0x3042),
                     ScriptedHost::key(0x3920),  // a trailing space, trimmed
                     ScriptedHost::key(0x0f09),  // Tab ends the edit
                     // The month spinner: December 1941 -> January 1942 is
                     // not in 1941, so the month wraps to December.
                     ScriptedHost::click(0x122 + 0x68 + 20, 0x73 + 0xd5 + 17 + 4),
                     ScriptedHost::click(0x122 + 0xec + 20, 0x73 + 0xd5 + 17 + 4),
                     at(0xc4, 300, 0x79)};
    const std::optional<NewCareerChoice> c = new_career_screen(f.ui, false);
    REQUIRE(c.has_value());
    CHECK(c->name == "AB");
    CHECK(c->month == 11);
    CHECK(c->year == 12);
    CHECK(c->day == 1);
    CHECK(c->command == 0);
  }
}

TEST_CASE("The roster lists the shipped career and SELECT returns its row", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  const std::optional<RosterFile> roster = load_roster(f.saves, 0);
  REQUIRE(roster.has_value());
  const CareerRecord& r = roster->career.record;
  const std::vector<CareerRecord> careers = {r};

  const std::vector<std::string> rows = roster_rows(careers, f.exe, f.ui.menu_resources().large);
  REQUIRE(rows.size() == 1);
  CHECK(rows[0].starts_with("1:"));
  CHECK(rows[0].find("Mathias") != std::string::npos);
  CHECK(rows[0].find("Jun 1 1945") != std::string::npos);
  CHECK(rows[0].ends_with("In Port"));

  int selection = 0;
  // SELECT (enabled for a career in port).
  f.host.script = {button(0xb, 0xe, 0x1bb, 400, 0x79)};
  CHECK(roster_select_screen(f.ui, careers, selection, true) == 0);
  // VIEW.
  f.host.script = {button(0xb, 0xe, 0xb1, 400, 0x79)};
  CHECK(roster_select_screen(f.ui, careers, selection, false) == career_code::kRosterView);
  // DELETE, confirmed.
  f.host.script = {button(0xb, 0xe, 0x136, 400, 0x79), ScriptedHost::key(0x1559)};
  CHECK(roster_select_screen(f.ui, careers, selection, false) == career_code::kRosterDelete);
  // A retired career cannot be selected: SELECT does nothing, CANCEL leaves.
  std::vector<CareerRecord> retired = careers;
  retired[0].status = CareerStatus::kRetired;
  f.host.script = {button(0xb, 0xe, 0x1bb, 400, 0x79), button(0xb, 0xe, 0x2b, 400, 0x79)};
  CHECK(roster_select_screen(f.ui, retired, selection, false) == ui::kResultCancel);
}

TEST_CASE("VIEW CAREER shows the shipped career's values", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  const std::optional<RosterFile> roster = load_roster(f.saves, 0);
  REQUIRE(roster.has_value());
  const std::vector<std::string> fields = view_career_fields(roster->career.record, f.exe);
  REQUIRE(fields.size() == 10);
  CHECK(fields[0] == "June 1 1945");
  CHECK(fields[1] == "In Port");
  CHECK(fields[2] == "(SS-337) Carbonero");
  CHECK(fields[4] == "Darwin");
  CHECK(fields[5] == "Darwin");
  CHECK(fields[6] == "2");
  CHECK(fields[7] == "2370");
  CHECK(fields[8] == "5");
  CHECK(fields[9] == "2090");
}

TEST_CASE("The career flow continues the shipped career up to the base and back", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  CareerFlow flow(f.ui, tables, campaign);
  const std::optional<RosterFile> roster = load_roster(f.saves, 0);
  REQUIRE(roster.has_value());
  // CONTINUE A CAREER, SELECT, START WAR PATROL, CANCEL on the patrol
  // orders, then MAIN MENU of the base, which leaves the career menu too.
  const int base_y = (480 - 0x168) / 2;
  f.host.script = {button(0x159, 0x4b, 0x23, 0xaa, 0xc6), button(0xb, 0xe, 0x1bb, 400, 0x79),
                   button(5, base_y, 0x1e, 0x82, 0xc6), ScriptedHost::click(0x1b2 + 0x63, 0x194 + 12),
                   button(5, base_y, 0x1e, 0x122, 0xc6)};
  flow.run();
  CHECK(f.host.script.empty());
  const Career& c = flow.career();
  CHECK(c.record.name == "Mathias");
  CHECK(c.record.boat == "(SS-337) Carbonero");
  CHECK(c.log.size() == 5);
  // A cancelled patrol leaves the seed: the same orders come again.
  CHECK(c.record.seed == roster->career.record.seed);
  CHECK(flow.home_command() == SubCommand::kComSubSoWesPac);
}

}  // namespace
}  // namespace sh

namespace sh {
namespace {

using test::Fixture;
using test::ScriptedHost;
using test::button;

TEST_CASE("A career patrol updates the career from its results", "[career][content]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  CareerFlow flow(f.ui, tables, campaign);
  // The patrol sinks a 5000-ton freighter and returns with fuel to spare.
  PatrolLogEntry sunk;
  sunk.day = 3;
  sunk.month = 5;
  sunk.year = 15;
  sunk.hour = 12;
  sunk.type = 13;
  sunk.tonnage = 5000;
  f.host.patrol_end = 5;
  f.host.patrol_result.end = {.end_code = 5, .fuel = 50000, .return_hours = 0};
  MissionOutcome& o = f.host.patrol_result.outcome;
  o.end_code = 5;
  o.day = 3;
  o.month = 5;
  o.year = 15;
  o.fuel = 50000;
  o.boat_name = "(SS-337) Carbonero";
  o.class_name = "BALAO";
  o.profile_sprite = "P_GATO.SHP";
  o.patrol_log = {sunk};
  const int base_y = (480 - 0x168) / 2;
  // CONTINUE A CAREER, SELECT, START WAR PATROL, BEGIN PATROL, then Enter
  // through the departure, the return, the results, the letters and the
  // calendar (5 screens), and MAIN MENU of the base.
  f.host.script = {button(0x159, 0x4b, 0x23, 0xaa, 0xc6), button(0xb, 0xe, 0x1bb, 400, 0x79),
                   button(5, base_y, 0x1e, 0x82, 0xc6), ScriptedHost::click(0x1b2 + 0x63, 0x168 + 12)};
  for (int i = 0; i < 5; ++i) {
    f.host.script.push_back(ScriptedHost::key(ui::kKeyEnter));
  }
  f.host.script.push_back(button(5, base_y, 0x1e, 0x122, 0xc6));
  flow.run();
  REQUIRE(f.host.patrol_request.has_value());
  CHECK((f.host.patrol_request->options & option::kModeMask) == option::kModeCareer);
  CHECK(f.host.patrol_request->plan.zone.name == "South China Sea");
  const CareerRecord& r = flow.career().record;
  CHECK(r.patrols == 3);
  CHECK(r.ships_sunk == 6);
  CHECK(r.tonnage == 2370 + 5000);
  CHECK(r.status == CareerStatus::kInPort);
  CHECK(flow.career().log.size() == 6);
  // The career was saved with its new log entry.
  const std::optional<RosterFile> saved = load_roster(f.saves, 0);
  REQUIRE(saved.has_value());
  CHECK(saved->career.record.tonnage == r.tonnage);
  CHECK(saved->career.log.size() == 6);
}

TEST_CASE("A career patrol takes the extended rules, and its port time their deliveries", "[career][radar_rules][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const CampaignData campaign = CampaignData::load(install, tables);
  for (const bool extended : {false, true}) {
    // The shipped career has no rules of its own: extended, it takes the
    // extended game's.
    Fixture f(install, extended);
    CareerFlow flow(f.ui, tables, campaign);
    f.host.patrol_end = 5;
    f.host.patrol_result.end = {.end_code = 5, .fuel = 50000, .return_hours = 0};
    MissionOutcome& o = f.host.patrol_result.outcome;
    o.end_code = 5;
    o.day = 3;
    o.month = 5;
    o.year = 15;
    o.fuel = 50000;
    o.boat_name = "(SS-337) Carbonero";
    o.class_name = "BALAO";
    o.profile_sprite = "P_GATO.SHP";
    PatrolLogEntry sunk;
    sunk.day = 3;
    sunk.month = 5;
    sunk.year = 15;
    sunk.hour = 12;
    sunk.type = 13;
    sunk.tonnage = 5000;
    o.patrol_log = {sunk};
    // Extended, REQUESTS adds a button after VIEW CAREER.
    const int base_y = (480 - (extended ? 0x190 : 0x168)) / 2;
    const int main_menu_y = extended ? 0x14a : 0x122;
    // As above, with the APR-1's letter in port when extended (6 screens).
    f.host.script = {button(0x159, 0x4b, 0x23, 0xaa, 0xc6), button(0xb, 0xe, 0x1bb, 400, 0x79),
                     button(5, base_y, 0x1e, 0x82, 0xc6), ScriptedHost::click(0x1b2 + 0x63, 0x168 + 12)};
    for (int i = 0; i < (extended ? 6 : 5); ++i) {
      f.host.script.push_back(ScriptedHost::key(ui::kKeyEnter));
    }
    f.host.script.push_back(button(5, base_y, 0x1e, main_menu_y, 0xc6));
    flow.run();
    CHECK(f.host.script.empty());
    REQUIRE(f.host.patrol_request.has_value());
    CHECK(f.host.patrol_request->rules == (extended ? MissionRules::extended() : MissionRules{}));
    CHECK(((flow.career().record.equipment & static_cast<std::uint32_t>(Equipment::kApr1Detector)) != 0) ==
          extended);
  }
}

TEST_CASE("Back takes each career menu's way back", "[career][back][content]") {
  SH_REQUIRE_GAME_DATA(install);
  Fixture f(install);
  const GameTables tables = GameTables::load(install);
  const ScriptedHost::Step back = ScriptedHost::back();
  f.host.script = {back};
  CHECK(career_menu_screen(f.ui) == career_code::kMainMenu);
  // The name entry edits at once: Back ends the edit and is CANCEL.
  f.host.script = {back};
  CHECK_FALSE(new_career_screen(f.ui, true).has_value());
  CHECK(f.host.script.empty());

  const std::optional<RosterFile> roster = load_roster(f.saves, 0);
  REQUIRE(roster.has_value());
  const CareerRecord& r = roster->career.record;
  const std::vector<CareerRecord> careers = {r};
  int selection = 0;
  f.host.script = {back};
  CHECK(roster_select_screen(f.ui, careers, selection, true) == ui::kResultCancel);
  Career career;
  career.record = r;
  REQUIRE(career.record.on_patrol == 0);
  f.host.script = {back};
  CHECK(base_menu_screen(f.ui, career, true, [] {}) == career_code::kLeaveCareer);

  // VIEW CAREER's LOG BOOK, left by its DONE sprite, then VIEW CAREER's
  // DONE.
  REQUIRE(r.log_count != 0);
  f.host.script = {button(0xb, 0xe, 0x179, 0x15a, 0xc6), back, back};
  view_career(f.ui, r, {});
  CHECK(f.host.script.empty());
  f.host.script = {back};
  top_commanders_screen(f.ui, r, top_commanders(tables.commanders, r), tables.commanders);
  f.host.script = {back};
  best_careers_screen(f.ui, BestCareersTable{});
  f.host.script = {back};
  top_career_screen(f.ui, r);
  CHECK(f.host.script.empty());
}

}  // namespace
}  // namespace sh
