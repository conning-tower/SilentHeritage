#include "game/career_flow.h"

#include <string>
#include <utility>
#include <vector>

#include "assets/exe_image.h"
#include "game/career_boats.h"
#include "game/career_menus.h"
#include "game/career_save.h"
#include "game/career_ui.h"
#include "game/mission_save.h"
#include "game/resume.h"
#include "game/saves_options.h"
#include "ui/dialog.h"

namespace sh {

namespace {

// SH.EXE addresses.
namespace addr {
constexpr std::uint32_t kRetiredLocation = 0xf0c58;  // "****"
constexpr std::uint32_t kHomeCine = 0x10333c;        // "HOME"
}  // namespace addr

std::vector<SunkListEntry> events_of(const std::vector<CareerLogEntry>& log) {
  std::vector<SunkListEntry> events;
  events.reserve(log.size());
  for (const CareerLogEntry& e : log) {
    events.push_back(e.event);
  }
  return events;
}

}  // namespace

CareerFlow::CareerFlow(CareerUi& ui, const GameTables& tables, const CampaignData& campaign)
    : ui_(ui), tables_(tables), campaign_(campaign), letters_(ui) {}

SubCommand CareerFlow::home_command() const {
  const std::optional<HomePort> port = find_home_port(campaign_, career_.record, career_.record.home_port);
  return port ? port->command : SubCommand::kComSubsAsiatic;
}

void CareerFlow::save() { save_career(ui_.saves(), career_); }

int CareerFlow::new_submarine(bool offer) {
  return letters_.new_submarine(career_, tables_, home_command(), offer, [this](Career& c) {
    return assign_new_boat(c.record, c.summary, home_command(), tables_, ui_.random(), rules());
  });
}

void CareerFlow::record_best_career() {
  // best_careers_record_score (0x1db03)
  const CareerRecord& r = career_.record;
  if (r.score == 0) {
    return;
  }
  std::optional<BestCareersTable> table = BestCareersTable::load(ui_.saves());
  if (!table) {
    return;
  }
  const std::optional<std::size_t> rank = table->rank_of(r.score);
  if (!rank) {
    return;
  }
  top_career_screen(ui_, r);
  table->insert(*rank, BestCareerEntry{.record = r, .log = load_career_log(ui_.saves(), r)});
  table->save(ui_.saves());
}

void CareerFlow::run() {
  // career_menu_loop (0x1b4bc)
  career_.log.clear();
  while (true) {
    ui_.set_location({FrontEndPlace::kCareerMenu, -1});
    const int choice = career_menu_screen(ui_);
    int code = ui::kResultCancel;
    switch (choice) {
      case career_code::kMainMenu:
        return;
      case career_code::kStartCareer:
        code = start_career();
        break;
      case career_code::kContinueCareer:
        code = roster_select_loop();
        break;
      case career_code::kBestCareers:
        // A table that cannot be read shows empty (the original reads
        // through a null pointer).
        best_careers_screen(ui_, BestCareersTable::load(ui_.saves()).value_or(BestCareersTable{}));
        break;
      default:
        break;
    }
    if (code == ui::kResultAccept && base_loop() == career_code::kLeaveCareer) {
      return;
    }
  }
}

MissionRules CareerFlow::rules() const { return career_.rules.value_or(MissionRules{}); }

void CareerFlow::adopt_rules() {
  // Not in the original: a career without ROSTER<n>.EXT (begun in the
  // original or the faithful variant) takes the extended defaults.
  if (ui_.extended_config()) {
    career_.rules = load_career_rules(ui_.saves(), career_.record.roster_slot).value_or(MissionRules::extended());
  } else {
    career_.rules.reset();
  }
}

void CareerFlow::resume(Career career) {
  career_ = std::move(career);
  adopt_rules();
  if (base_loop() == career_code::kLeaveCareer) {
    return;
  }
  run();
}

void CareerFlow::resume_base(std::int32_t roster_slot) {
  // croster_load_entry as CONTINUE CAREER loads it.
  const std::string file = roster_file_name(roster_slot);
  for (const RosterEntry& entry : list_rosters(ui_.saves())) {
    if (entry.file == file) {
      Career career = career_from_roster_entry(entry, tables_);
      career.log = load_career_log(ui_.saves(), career.record);
      load_roster_patrol_state(ui_.saves(), career);
      resume(std::move(career));
      return;
    }
  }
  run();
}

int CareerFlow::start_career() {
  // new_career_screen (0x5aa7f) after the dialog: `career_init_new` (with
  // `realism_load`), the home port (`career_transfer_home_port(0, -1)`,
  // showing its orders), a boat, a free roster slot and the save; then
  // `new_submarine_screen(0)`, which assigns a boat again.
  const std::optional<NewCareerChoice> choice = new_career_screen(ui_, campaign_.all_maps);
  if (!choice) {
    return ui::kResultCancel;
  }
  RealismFile::load(ui_.saves(), ui_.options());
  // Not in the original: the rules of a new extended game.
  const std::optional<MissionRules> new_rules =
      ui_.extended_config() ? std::optional<MissionRules>(ui_.mission_rules()) : std::nullopt;
  career_ = career_init_new(*choice, ui_.options(), ui_.random(), 0, new_rules.value_or(MissionRules{}));
  career_.rules = new_rules;
  PatrolAssignment assignment;
  if (const std::optional<HomePort> port =
          transfer_home_port(career_, assignment, campaign_, false, true, ui_.random())) {
    letters_.homeport(career_.record, *port, false);
  }
  assign_new_boat(career_.record, career_.summary, home_command(), tables_, ui_.random(), rules());
  career_.record.roster_slot = find_free_roster_slot(ui_.saves());
  save();
  return new_submarine(false);
}

int CareerFlow::roster_select_loop() {
  // career_roster_select_loop (0x35082)
  int selection = 0;
  bool redraw = true;
  while (true) {
    const std::vector<RosterEntry> entries = list_rosters(ui_.saves());
    std::vector<CareerRecord> records;
    records.reserve(entries.size());
    for (const RosterEntry& e : entries) {
      records.push_back(e.record);
    }
    bool relist = false;
    while (!relist) {
      const int result = roster_select_screen(ui_, records, selection, redraw);
      redraw = true;
      if (result == career_code::kRosterView) {
        // A career saved on patrol shows its patrol log too.
        const CareerView view = career_view(ui_.saves(), records.at(static_cast<std::size_t>(selection)));
        view_career(ui_, view.record, view.events);
      } else if (result == career_code::kRosterDelete) {
        delete_roster(ui_.saves(), records.at(static_cast<std::size_t>(selection)).roster_slot);
        redraw = false;
        relist = true;
      } else if (result >= 0 && result < static_cast<int>(entries.size())) {
        // croster_load_entry (0x35603), with the career log the original
        // loads before each use.
        career_ = career_from_roster_entry(entries[static_cast<std::size_t>(result)], tables_);
        career_.log = load_career_log(ui_.saves(), career_.record);
        load_roster_patrol_state(ui_.saves(), career_);
        adopt_rules();
        return ui::kResultAccept;
      } else {
        return ui::kResultCancel;
      }
    }
  }
}

int CareerFlow::base_loop() {
  // career_base_loop (0x1b556)
  bool redraw = true;
  const auto save_as = [this] {
    // croster_save_career_as (0x35669)
    if (const std::optional<std::string> name = backup_career_dialog(ui_)) {
      save_career_as(ui_.saves(), career_, *name, ui_.random());
    }
  };
  while (true) {
    ui_.set_location({FrontEndPlace::kCareerBase, career_.record.roster_slot});
    const int code = base_menu_screen(ui_, career_, redraw, save_as);
    switch (code) {
      case career_code::kStartPatrol: {
        const bool over = start_patrol();
        if (over) {
          return career_code::kLeaveCareer;
        }
        redraw = true;
        break;
      }
      case career_code::kTopCommanders:
        top_commanders_screen(ui_, career_.record, top_commanders(tables_.commanders, career_.record),
                              tables_.commanders);
        redraw = true;
        break;
      case career_code::kViewCareer:
        view_career(ui_, career_.record, events_of(load_career_log(ui_.saves(), career_.record)));
        redraw = true;
        break;
      case career_code::kLeaveCareer:
        return career_code::kLeaveCareer;
      case career_code::kTransfer:
        request_transfer();
        redraw = true;
        break;
      case career_code::kRetire:
        retire();
        return 0;
      default:
        // 0x7a and 0x7c (no button returns them): the menu without its
        // picture.
        redraw = false;
        break;
    }
  }
}

void CareerFlow::retire() {
  // The RETIRE branch of `career_base_loop`: the APCM letter and the best
  // careers. The original then leaves the loop returning an uninitialised
  // value; here it goes back to CAREER MENU.
  CareerRecord& r = career_.record;
  r.status = CareerStatus::kRetired;
  r.location = ui_.exe_string(addr::kRetiredLocation);
  ++r.award(Award::kAsiaticPacific);
  save();
  ui_.palette_fade_out(1, 5);
  ui_.screen().fill(0);
  ui_.play_cinematic(ui_.exe_string(addr::kHomeCine));
  letters_.present_award(LetterCode::kAsiaticPacific, r, home_command());
  record_best_career();
}

void FrontEnd::career_menu_loop(const ResumeSnapshot* resume) {
  // career_menu_loop (0x1b4bc) with the campaign tables it reads.
  const GameTables tables = GameTables::load(install_);
  const CampaignData campaign = CampaignData::load(install_, tables);
  CareerUi ui(*this);
  CareerFlow flow(ui, tables, campaign);
  if (resume == nullptr) {
    flow.run();
  } else if (resume->career) {
    flow.resume(*resume->career);
  } else {
    flow.resume_base(resume->location.roster_slot);
  }
}

}  // namespace sh
