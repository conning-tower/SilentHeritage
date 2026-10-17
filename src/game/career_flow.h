// The career's flow (module campaign/career): `career_menu_loop` (0x1b4bc)
// from the main menu's CAREER MENU, and `career_base_loop` (0x1b556), the
// BASE MENU between patrols, with the career in memory (0x116810).
//
//   CareerFlow flow(ui, tables, campaign);
//   flow.run();  // returns to the main menu
#pragma once

#include <cstdint>
#include <optional>

#include "assets/dat_tables.h"
#include "game/career.h"
#include "game/career_letters.h"
#include "game/career_rules.h"

namespace sh {

class CareerUi;

class CareerFlow {
 public:
  // All references must outlive the flow.
  CareerFlow(CareerUi& ui, const GameTables& tables, const CampaignData& campaign);

  // `career_menu_loop` (0x1b4bc): CAREER MENU until MAIN MENU, or a career
  // left through the base's MAIN MENU or ended on patrol.
  void run();
  // Resumes `career` (from the resume snapshot) at its base, or in its
  // patrol when it is on patrol, then goes on as `run` after its base.
  void resume(Career career);
  // Resumes the career of ROSTER<roster_slot>.SAV at its base (as loaded
  // by CONTINUE CAREER), or runs CAREER MENU when it cannot be read.
  void resume_base(std::int32_t roster_slot);

  CareerUi& ui() { return ui_; }
  const GameTables& tables() const { return tables_; }
  const CampaignData& campaign() const { return campaign_; }
  CareerLetters& letters() { return letters_; }
  // The career in memory (0x116810, 0x116c68 and the log 0x116c64).
  Career& career() { return career_; }

  // The command of the career's home port (ComSubsAsiatic when it is not
  // open on the career's date).
  SubCommand home_command() const;
  // `career_save` (0x18e52) of the career in memory.
  void save();
  // Returns the career's rules, the original's when it has none (not in
  // the original).
  MissionRules rules() const;
  // `new_submarine_screen` (0x7131c) with `carsub_assign_new_boat`: -1 when
  // a boat was assigned and accepted.
  int new_submarine(bool offer);
  // `best_careers_record_score` (0x1db03): a career with a score that ranks
  // among the ten best is congratulated (TOP CAREER) and recorded in
  // BESTCARS.SAV with its log.
  void record_best_career();

 private:
  // `career_base_loop` (0x1b556): returns career_code::kLeaveCareer when
  // the career is left or over, 0 after a retirement.
  int base_loop();
  // Sets the rules of the career just loaded: ROSTER<n>.EXT's, or the
  // extended defaults without it, with `CareerUi::extended_config` (not in
  // the original).
  void adopt_rules();
  // The START A CAREER branch: the new career set up and saved; returns
  // the NEW SUBMARINE result (-1 to go to the base).
  int start_career();
  // `career_roster_select_loop` (0x35082): -1 when a career was loaded.
  int roster_select_loop();
  // RETIRE (0x7f).
  void retire();
  // START WAR PATROL (0x78, career_patrol_flow.cpp): from
  // `career_assign_patrol` to the time in port; returns true when the
  // career is over (dead, captured, relieved or retired at the war's end
  // or an aborted patrol). Both draw the next patrol's seed (`rand()`) when
  // done, as the base loop does after them.
  bool start_patrol();
  // REQUEST TRANSFER (0x7e) once confirmed: the refit, the move to another
  // home port and the time in port.
  void request_transfer();
  // The new equipment, the save and the calendar that end the time in port
  // after a patrol or a transfer.
  void finish_port_time();
  // REQUESTS (not in the original; game/career_requests.h): the menu until
  // BASE MENU, each request granted saved with the career.
  void requests_menu();
  // TORPEDO LOAD from the patrol START WAR PATROL would make
  // (`preview_patrol`); false when there is none.
  bool request_torpedo_load();
  // DECK GUN.
  void request_deck_gun();
  // PATROL AREA: the zone of the next patrol among the candidates.
  void request_patrol_area();

  CareerUi& ui_;
  const GameTables& tables_;
  const CampaignData& campaign_;
  CareerLetters letters_;
  Career career_;
  // The patrol globals kept between patrols: the zone, the transit, the
  // repair and refit days, the transfer travel days.
  PatrolAssignment assignment_;
};

}  // namespace sh
