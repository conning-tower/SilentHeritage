// The patrol and transfer branches of `career_base_loop` (0x1b556): START
// WAR PATROL from the orders to the time in port, and REQUEST TRANSFER.

#include <optional>

#include "core/error.h"
#include "game/awards.h"
#include "game/career_boats.h"
#include "game/career_flow.h"
#include "game/career_save.h"
#include "game/career_ui.h"
#include "game/mission.h"
#include "game/patrol.h"
#include "game/saves_options.h"

namespace sh {

namespace {

// SH.EXE addresses.
namespace addr {
constexpr std::uint32_t kHomeCine = 0x10333c;  // "HOME"
}  // namespace addr

// The end codes `career_base_loop` reacts to before the results.
constexpr std::int32_t kEndAbort = 1;
constexpr std::int32_t kEndQuitToDos = 3;

}  // namespace

bool CareerFlow::start_patrol() {
  // career_base_loop, code 0x78.
  CareerRecord& r = career_.record;
  WatcomRandom& random = ui_.random();
  random.srand(r.seed);

  // career_assign_patrol (0x62df9): the zone, then the orders until BEGIN
  // PATROL or CANCEL.
  std::optional<PatrolAssignment> assigned = assign_patrol(career_, campaign_, random);
  if (!assigned) {
    return false;
  }
  // The patrol globals the original keeps: the repair, refit and travel
  // days stay those of the last patrol until rolled again.
  assigned->repair_days = assignment_.repair_days;
  assigned->refit_days = assignment_.refit_days;
  assigned->travel_days = assignment_.travel_days;
  assignment_ = *assigned;
  std::uint32_t& options = ui_.options();
  options = career_realism_options(options, r);
  const std::optional<HomePort> home = find_home_port(campaign_, r, r.home_port);
  const SubCommand command = home ? home->command : SubCommand::kComSubsAsiatic;
  while (true) {
    const PatrolOrdersResult choice = letters_.patrol_orders(
        r, PatrolOrders{assignment_.zone, command, assignment_.transit_hours, assignment_.transit_fuel},
        home ? &*home : nullptr);
    if (choice == PatrolOrdersResult::kCancel) {
      return false;
    }
    if (choice == PatrolOrdersResult::kBegin) {
      break;
    }
    if (letters_.patrol_orders_realism(r, career_.rules)) {
      options = career_realism_options(options, r);
      save();
    }
  }
  accept_patrol_zone(career_, assignment_);

  // The outbound transit (unless resumed on patrol).
  if (r.on_patrol == 0) {
    r.transit_hours = 0;
    if (assignment_.transit_hours != 0) {
      const std::int32_t hours = jitter_transit_hours(assignment_.transit_hours, random);
      career_advance_hours(r, hours);
      r.transit_hours = hours;
    }
  }

  // career_build_patrol_scenario (0x1c1e2), whose port departure ends it.
  const bool departing = r.on_patrol == 0;
  options = (options & ~option::kModeMask) | option::kModeCareer;
  PatrolRequest request;
  request.career = &career_;
  request.plan = build_patrol_scenario(career_, assignment_.zone, assignment_.zone_index, assignment_.transit_fuel,
                                       assignment_.transit_hours, tables_, random);
  if (departing) {
    const SubClass* boat = tables_.find_sub(r.class_name);
    if (boat == nullptr) {
      throw DataError("SUBS.DAT: no class " + r.class_name);
    }
    ui_.show_port_departure(boat->sprite_profile);
  }

  // run_mission (0xa1b10), with GOPTION.SAV reloaded by init_simulation.
  ui_.show_mission_loading();
  request.options = options;
  request.game_options = GameOptions::load(ui_.saves());
  request.random = random;
  request.rules = rules();
  PatrolResult played;
  const std::int32_t end = ui_.runner().run_patrol(request, played);
  if (end == kEndQuitToDos) {
    // fatal_error(0, 0): the game ends.
    throw QuitRequested();
  }
  if (end == kEndAbort) {
    // EXIT WITHOUT SAVING: the career is left as last saved.
    return true;
  }
  MissionOutcome outcome = played.outcome;
  outcome.end_code = end;
  MissionResults results = ui_.mission_end_screens(outcome, options);

  // career_load_log_entries(-1): the patrol's sunk list joins the log.
  for (const SunkListEntry& e : results.sunk) {
    career_.log.push_back(CareerLogEntry::from(e));
  }
  const PatrolBookkeeping bookkeeping = apply_patrol_results(career_, assignment_, results, random);
  bool over = bookkeeping.career_over;
  if (bookkeeping.war_ended) {
    letters_.end_newspaper();
    ui_.play_cinematic(ui_.exe_string(addr::kHomeCine));
  }
  // The return transit.
  if (played.end.return_hours != 0 && !over) {
    const std::int32_t hours = jitter_transit_hours(played.end.return_hours, random);
    career_advance_hours(r, hours);
    r.transit_hours += hours;
  }
  save();
  const SubClass* boat = tables_.find_sub(r.class_name);
  letters_.patrol_results(r, results,
                          PatrolResultsInfo{.transit_hours = r.transit_hours,
                                            .repair_days = assignment_.repair_days,
                                            .fuel_capacity = boat != nullptr ? static_cast<std::int32_t>(
                                                                                   boat->fuel_capacity)
                                                                             : 0});

  // career_check_relieved, then career_check_awards.
  for (const AwardCode code : check_relieved(career_, results)) {
    letters_.present_award(static_cast<LetterCode>(code), r, home_command());
  }
  if (r.status == CareerStatus::kRelieved) {
    over = true;
  }
  for (const AwardCode code : check_awards(career_, results)) {
    letters_.present_award(static_cast<LetterCode>(code), r, home_command());
  }
  // A boat lost (0x187350): carsub_assign_new_boat, then NEW SUBMARINE.
  if (!over && results.boat_lost != 0) {
    assign_new_boat(r, career_.summary, home_command(), tables_, random, rules());
    new_submarine(false);
  }
  save();
  if (over) {
    record_best_career();
  } else {
    assignment_.travel_days = 0;
    career_advance_hours(r, (assignment_.repair_days + assignment_.refit_days) * 24);
    if (const std::optional<HomePort> port = check_transfer(career_, assignment_, campaign_, random)) {
      letters_.homeport(r, *port, true);
    }
    const NewCommand offer = check_new_command(career_, campaign_);
    if (offer.offer_boat) {
      new_submarine(offer.from_fleet_boat);
    }
    finish_port_time();
  }
  r.seed = static_cast<std::uint32_t>(random.rand());
  return over;
}

void CareerFlow::request_transfer() {
  // career_base_loop, code 0x7e.
  CareerRecord& r = career_.record;
  WatcomRandom& random = ui_.random();
  assignment_.travel_days = 0;
  assignment_.refit_days = roll_refit_days(random);
  career_advance_hours(r, assignment_.refit_days * 24);
  if (const std::optional<HomePort> port = transfer_home_port(career_, assignment_, campaign_, true, false, random)) {
    letters_.homeport(r, *port, true);
  }
  finish_port_time();
  r.seed = static_cast<std::uint32_t>(random.rand());
}

void CareerFlow::finish_port_time() {
  // new_equipment_run, career_save, career_show_transit_calendar.
  CareerRecord& r = career_.record;
  letters_.new_equipment(r, career_.summary.current_command, rules());
  save();
  letters_.transit_calendar(r, assignment_.travel_days, assignment_.repair_days + assignment_.refit_days);
}

}  // namespace sh
