// The rules of the career (modules campaign/career, patrol, homeport and
// tcmdlist): the calendar, the patrol assignment (zone pick, transit fuel
// and time), the bookkeeping after a patrol, the awards, the relief of
// command, promotions, new boats, transfers between home ports and the
// TOP COMMANDERS table. Pure logic over a `Career`; the screens that show
// the outcomes are elsewhere. Every random draw happens in the original
// order on the generator given.
//
//   CampaignData data = CampaignData::load(install, tables);
//   std::optional<PatrolAssignment> a = assign_patrol(career, data, random);
//   ...play the patrol, compute the MissionResults...
//   PatrolBookkeeping b = apply_patrol_results(career, *a, results, random);
//   std::vector<AwardCode> letters = check_relieved(career, results);
//   std::vector<AwardCode> medals = check_awards(career, results);
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "core/random.h"
#include "game/awards.h"
#include "game/career.h"
#include "game/frontend_results.h"

namespace sh {

class Install;

// The campaign tables the rules read: HOMEPORT.DAT, PZ.DAT and the zones
// whose map is installed.
struct CampaignData {
  std::vector<HomePort> home_ports;
  std::vector<PatrolZone> zones;
  // 0x11bcc4: per zone, whether DATA\<zone world> exists (the loader in
  // `load_shpath_ini`'s caller); zones without their map are never used.
  std::vector<bool> zone_enabled;
  // 0x11a7d4: more than 12 zones enabled from zone 0 on (the full set of
  // maps): enables the command rules of the transfers.
  bool all_maps = false;

  // Reads PZ.DAT and checks the maps; the home ports come from `tables`.
  // Throws `DataError` when PZ.DAT is missing or malformed.
  static CampaignData load(const Install& install, const GameTables& tables);
  // Sets `zone_enabled` and `all_maps` from the enabled flags.
  void set_enabled(std::vector<bool> enabled);
};

// ------------------------------------------------------------ calendar

// `career_advance_hours` (0x1c174): moves the career's date on by `hours`
// whole days (24 hours each, a remainder is dropped), keeping the time.
void career_advance_hours(CareerRecord& record, std::int32_t hours);
// `career_date_after_war_end` (0x1c514): true on or after 15 August 1945
// (day 1-based, month 0-based, year since 1930).
bool date_after_war_end(std::int32_t day, std::int32_t month, std::int32_t year);
// `campaign_month_index_abs` (0x1e463): the campaign month of `month`
// (0-based) of the full `year`; every month of 1941 gives 0.
std::int32_t campaign_month_index_abs(std::int32_t month, std::int32_t full_year);
// Returns `hours` give or take 2.5 % (`career_base_loop`'s transits: one
// draw of hours x 5 % around the middle).
std::int32_t jitter_transit_hours(std::int32_t hours, WatcomRandom& random);

// ----------------------------------------------------------- home ports

// `homeport_next_open` (0x4d5cc) over all ports: the indexes of the ports
// open on the career's date (open <= today < close, by campaign month x 31
// + day) with a weight above 0 this month for an enabled zone, in file
// order.
std::vector<std::size_t> open_home_ports(const CampaignData& data, const CareerRecord& record);
// `homeport_find` (0x4db8d): the open port named `name` (case-insensitive),
// with its command 2 (ComSubSoWesPac) turned into 0 before 1 April 1942
// (`homeport_load_dat(-1)`); nothing when it is not open.
std::optional<HomePort> find_home_port(const CampaignData& data, const CareerRecord& record, std::string_view name);

// ----------------------------------------------------- patrol assignment

// What the career carries from the patrol orders to the return to port.
struct PatrolAssignment {
  std::int32_t zone_index = -1;  // 0x11689c: PZ.DAT index
  PatrolZone zone;               // 0x11b808
  std::int32_t previous_zone = -1;  // 0x125098: the zone before the pick
  std::int32_t transit_fuel = 0;    // 0x125070: one way
  std::int32_t transit_hours = 0;   // 0x125074: one way (the return grows when fuel ran short)
  std::int32_t repair_days = 0;     // 0x125078
  std::int32_t refit_days = 0;      // 0x116cdc
  // 0x12609c: the map edges the boat may start from (`MapEdge` order).
  std::array<std::int32_t, 4> start_edges{};
  // 0x121ffc: the days of the last transfer trip (0 when none).
  std::int32_t travel_days = 0;
};

// `patrol_pick_zone` (0x63724): a zone drawn by the home port's weights
// for the career's month among the enabled zones, leaving out the last
// zone after a repeat (a second pass allows it when no other zone has a
// weight); on patrol (`record.on_patrol`) the last zone. Sets
// `record.last_patrol_zone` to the pick, -1 when none (an original bug:
// the last zone is then lost). Throws `DataError` when the home port is
// not open (the original reads a null pointer).
std::optional<std::int32_t> pick_patrol_zone(Career& career, const CampaignData& data, WatcomRandom& random);
// `patrol_compute_transit` (0x638d0): the transit hours (zone distance /
// two thirds of the boat's speed) and fuel (35 % of the hourly rate from
// `fuel_use` x 3600 in 16.16, times the hours) to zone `zone` from `port`.
struct Transit {
  std::int32_t hours = 0;
  std::int32_t fuel = 0;
};
Transit compute_transit(const HomePort& port, std::int32_t zone, const CareerRecord& record);
// Returns the options word `options` with its realism bits replaced by the
// career's realism values (`career_assign_patrol`); the mode bits and the
// bits above 16 are kept.
std::uint32_t career_realism_options(std::uint32_t options, const CareerRecord& record);
// The automatic part of `career_assign_patrol` (0x62df9): zones are picked
// until twice the transit fuel fits in 90 % of the tank (zero fuel always
// fits). Returns nothing when no zone can be picked. Throws `DataError`
// after 100000 picks (the original loops forever).
std::optional<PatrolAssignment> assign_patrol(Career& career, const CampaignData& data, WatcomRandom& random);
// `patrol_accept_zone` (0x636b8): the open map edges, and the count of
// patrols in the same zone in a row.
void accept_patrol_zone(Career& career, PatrolAssignment& assignment);

// ---------------------------------------------------------- port time

// `patrol_compute_repair_days` (0x63971): a quarter of the hull damage and
// a fifth of the system damage in days, each give or take 2.5 % (two
// draws).
std::int32_t compute_repair_days(const MissionResults& results, WatcomRandom& random);
// `career_roll_refit_days` (0x1c161): 5..11 days.
std::int32_t roll_refit_days(WatcomRandom& random);

// What `career_apply_patrol_results` decided.
struct PatrolBookkeeping {
  // The career is over: dead, captured or retired at the war's end
  // (`career_base_loop` then records the best careers).
  bool career_over = false;
  // The war ended during the patrol or the time in port: the caller shows
  // the end newspaper (`career_show_end_newspaper`) and plays the HOME
  // cinematic.
  bool war_ended = false;
};

// `career_apply_patrol_results` (0x1bc18): adds the patrol's counters,
// score, tonnage and ships to the career, rolls the repair and refit days
// (into `assignment`), checks the war's end after the time in port
// (`career_advance_port_time`, which may set the fate to kRecalled and the
// results' date to the war's end), sets the status from the fate and the
// career's date to the results' date, and updates the streaks. As in the
// original, a patrol counts as high-realism when it sank at least 2 ships
// besides sampans (the realism rating it computes is overwritten), and the
// poor-patrol test reads the career's total pilots and recons.
PatrolBookkeeping apply_patrol_results(Career& career, PatrolAssignment& assignment, MissionResults& results,
                                       WatcomRandom& random);

// `career_check_relieved` (0x1c01b), in port only: the POOR FUEL
// MANAGEMENT letter for a patrol ending without fuel (relieved after the
// fourth), POOR PATROL PERFORMANCE during a poor-patrol streak (relieved
// above 2), relieved after a second boat lost; when relieved, LOSS OF
// COMMAND and the status kRelieved. Returns the letters in order.
std::vector<AwardCode> check_relieved(Career& career, const MissionResults& results);
// `career_check_awards` (0x1be59), only without a poor-patrol streak:
// returns the medals and citations won, in the order presented, with the
// career's counters updated.
std::vector<AwardCode> check_awards(Career& career, const MissionResults& results);

// What `career_check_new_command` (0x1c0c6) decided.
struct NewCommand {
  bool promoted = false;
  // The NEW SUBMARINE screen is offered (`new_submarine_screen`), with
  // its first argument: the boat is not an S-boat (class index > 0).
  bool offer_boat = false;
  bool from_fleet_boat = false;
};
// Promotes past 2 and 5 patrols counted for promotion (table 0x103360),
// then offers a new boat in ComSubPac in an S-boat, in an S-boat after 2
// patrols, or in a class below 8 sinking more ships than patrols after
// more than 3 patrols in this boat (that count is then reset). Throws
// `DataError` when the home port is not open.
NewCommand check_new_command(Career& career, const CampaignData& data);

// ------------------------------------------------------------ transfers

// `homeport_pick_transfer_port` (0x4dbe0): a transfer port drawn by the
// month's transfer weights among the open ports that can be destinations;
// with all maps, `change_command` keeps to the other commands, else to the
// current (or preferred) one. Nothing when no port has a weight, or when
// the draw is the home port itself.
std::optional<HomePort> pick_transfer_port(const Career& career, const CampaignData& data, bool change_command,
                                           WatcomRandom& random);
// `career_transfer_home_port` (0x4de43): unless `no_travel`, 4..7 days of
// travel (10..19 when changing command) are added to the date; then the
// new port becomes the home port and location, with its command and the
// port counters reset. Without a port the date is put back. Returns the
// new port (the caller shows `homeport_screen`, with its travel days when
// `no_travel` is false).
std::optional<HomePort> transfer_home_port(Career& career, PatrolAssignment& assignment, const CampaignData& data,
                                           bool change_command, bool no_travel, WatcomRandom& random);
// `career_check_transfer` (0x4dd7e): transfers when the home port closed,
// with (patrols here - 2) x 40 % chance past 2 patrols, or after the same
// zone twice in a row; with all maps, after 4 patrols since the last
// transfer, a command change (no preferred command) or an offer
// (`CareerSummary::transfer_offered`). Returns the new port when transferred.
std::optional<HomePort> check_transfer(Career& career, PatrolAssignment& assignment, const CampaignData& data,
                                       WatcomRandom& random);

// ------------------------------------------------------- top commanders

// One row of TOP COMMANDERS: a CMDLIST.DAT commander or the player.
struct TopCommanderRow {
  bool player = false;
  std::int32_t commander = -1;  // index into the commanders when not the player
  std::int32_t tonnage = 0;
};
// `cmdlist_build_top_ten` (0x78e30): the 9 commanders with the highest
// tonnage (above 0) over the months before the career's, then the player
// inserted before the first one below the career's tonnage. At most 10
// rows.
std::vector<TopCommanderRow> top_commanders(const std::vector<Commander>& commanders, const CareerRecord& record);

}  // namespace sh
