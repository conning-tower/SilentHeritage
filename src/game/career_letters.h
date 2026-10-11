// The career's letters and patrol screens: the patrol orders
// (`patrol_orders_screen` 0x630cc), the patrol results (`patrol_results_screen`
// 0x639d8), the home-base orders (`homeport_screen` 0x4d0e3), the new
// equipment letter (`new_equipment_run` 0x5b501), the medals, citations and
// command letters (`awards_present` 0x55ecd), the transit calendar
// (`career_show_transit_calendar` 0x1c539), the end-of-war newspaper
// (`career_show_end_newspaper` 0x1ca30) and the new submarine orders
// (`new_submarine_screen` 0x7131c). The texts are built by free functions
// (testable without a screen); `CareerLetters` runs the screens and keeps
// the state the original keeps between them.
//
// The career date is the record's (month 0-based, year since 1930).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "game/career.h"
#include "game/frontend_results.h"
#include "sim/context.h"

namespace sh {

class CareerUi;
class ExeImage;
class Font;

// The codes of `awards_present`: the medals 0..5 and unit citations 6..7
// (the `Award` counters), and the letters from the command 100..102.
enum class LetterCode : std::int32_t {
  kBronzeStar = 0,
  kSilverStar = 1,
  kNavyCross = 2,
  kDsm = 3,
  kMedalOfHonor = 4,
  kAsiaticPacific = 5,
  kNuc = 6,
  kPuc = 7,
  kPoorPatrol = 100,  // POOR PATROL PERFORMANCE
  kPoorFuel = 101,    // POOR FUEL MANAGEMENT
  kRelieved = 102,    // LOSS OF COMMAND
};

// The results of the PATROL ORDERS dialog (`patrol_orders_*_cb`).
enum class PatrolOrdersResult : int { kCancel = 1, kBegin = 2, kRealism = 3 };

// The patrol the orders send the boat to (`career_assign_patrol`).
struct PatrolOrders {
  const PatrolZone& zone;     // 0x125084: name and map box
  SubCommand command;         // the home port's command (the letter's "From:")
  std::int32_t transit_hours;  // 0x125074
  std::int32_t transit_fuel;   // 0x125070, gallons
};

// What the patrol results show besides the results record 0x187254.
struct PatrolResultsInfo {
  std::int32_t transit_hours = 0;  // career +0x108 (0x116918): outbound + return
  std::int32_t repair_days = 0;     // 0x125078
  std::int32_t fuel_capacity = 0;   // SUBS.DAT fuel (0x11b22c)
};

// Returns the `gcvt(value, 3)` text: 3 significant digits, trailing zeros
// and point removed, E notation below 1e-4 or from 1e3 (the original's
// travel times in days).
std::string gcvt3(double value);

// Returns the day of the week name of the date (the calendar's Zeller sum
// over the table 0x10e970, "Monday".."Sunday").
std::string calendar_day_name(const ExeImage& exe, std::int32_t day, std::int32_t month, std::int32_t year);

// `homeport_load_base_art` (0x4d346): the SPAC_<x>.SHP map of the date
// (the table 0x10eb6c of names and first campaign months).
std::string base_art_file(const ExeImage& exe, std::int32_t month, std::int32_t year);

// `patrol_orders_letter` (0x632f7): the orders text, padded in `font`
// (MENU_S.FNT, font 0 of the menu screens).
std::string patrol_orders_letter(const CareerRecord& record, const PatrolOrders& orders, const ExeImage& exe,
                                 const Font& font);

// `patrol_orders_realism` (0x62fdc), once the REALISM dialog was accepted:
// stores the realism bits of the options word into the career's values.
void career_realism_from_options(std::uint32_t options, CareerRecord& record);

// `patrol_results_summary` (0x63a69): the PATROL RESULTS text of `results`
// at the career's date.
std::string patrol_results_summary(const CareerRecord& record, const MissionResults& results,
                                   const PatrolResultsInfo& info, const ExeImage& exe, const Font& font);

// `homeport_transfer_orders` (0x4d722): the HOME BASE text for `port`, the
// boat's transfer when `transfer`, else the report of a new commander.
std::string homeport_transfer_orders(const CareerRecord& record, const HomePort& port, bool transfer,
                                     const ExeImage& exe, const Font& font);

// One entry of the new-equipment table 0x10ca58 (dates set by
// `new_equipment_init_dialogs` from the equipment dates 0x10e408).
struct EquipmentEntry {
  std::uint32_t bit = 0;    // `.SCN` equipment bit
  std::int32_t month = 0;   // 0-based
  std::int32_t year = 0;    // since 1930
  std::string name;
  std::string text;
  std::int32_t sprite = 0;  // EQUIP.SHP frame
  std::int32_t x = 0;       // where the picture is shown
  std::int32_t y = 0;
  bool supply = false;      // a torpedo from the Supply Officer (else a refit)
};
inline constexpr int kEquipmentEntries = 7;

// Returns entry `i` (0..6) of the table.
EquipmentEntry equipment_entry(const ExeImage& exe, int i);

// The search of `new_equipment_letter` (0x5b5c7): the first entry not fitted
// whose date has come (refits three months later outside ComSubPac,
// `current_command` != 1). Returns its index, or nothing.
std::optional<int> new_equipment_due(const ExeImage& exe, const CareerRecord& record, std::int32_t current_command);

// Not in the original: the extended variant's deliveries after the table's,
// each with its rule: the APR-1 radar detector (5/44, EQUIP.SHP's picture 5,
// which the original never shows) and the ST periscope radar (11/44, its
// picture `kStRadarPicture`).
std::vector<EquipmentEntry> extended_equipment_entries(const MissionRules& rules);
// The sprite value of the ST's picture, composed by `st_radar_picture`
// (game/equipment_art.h).
inline constexpr std::int32_t kStRadarPicture = -1;

// Not in the original: after `new_equipment_due` found nothing, the first
// extended entry not fitted whose date has come (refits three months later
// outside ComSubPac), or nothing.
std::optional<EquipmentEntry> extended_equipment_due(const CareerRecord& record, std::int32_t current_command,
                                                     const MissionRules& rules);
// Not in the original: the extended entries a new career dated `record`
// starts with (those whose date has come).
std::uint32_t extended_equipment_on_date(const CareerRecord& record, const MissionRules& rules);

// The letter `new_equipment_letter` builds for entry `entry`.
std::string new_equipment_text(const CareerRecord& record, const EquipmentEntry& entry, const ExeImage& exe,
                               const Font& font);

// `awards_build_citation` (0x56185): the text of `code`. The headings are
// centred on `centre_width` (the text box width 0x124250 left by the
// previous call: the original centres before setting the new box) in
// `typewriter` (TYPEX.FNT) and padded in `pad_font` (MENU_S.FNT).
std::string award_citation_text(LetterCode code, const CareerRecord& record, SubCommand command,
                                std::int32_t centre_width, const ExeImage& exe, const Font& typewriter,
                                const Font& pad_font);

// `subscrn_compose_orders` (0x72441): the NEW SUBMARINE orders, an offer
// that can be rejected when `offer`.
std::string new_submarine_orders(const CareerRecord& record, SubCommand command, bool offer, const ExeImage& exe,
                                 const Font& font);

class CareerLetters {
 public:
  // `ui` must outlive the screens.
  explicit CareerLetters(CareerUi& ui) : ui_(ui) {}

  // `patrol_orders_screen` (0x630cc): the orders over the zone's map, the
  // home port marked (`home`, null when not open). On patrol it returns
  // kBegin at once.
  PatrolOrdersResult patrol_orders(const CareerRecord& record, const PatrolOrders& orders, const HomePort* home);
  // `patrol_orders_realism` (0x62fdc): the REALISM dialog over the options
  // word; on OK the career's values are updated and true returned (the
  // original then saves the career). The career's `rules`, when it has
  // some, give and take the dialog's extended items (UNDEAFENED ESCORTS,
  // DETECTABLE RADAR; not in the original).
  bool patrol_orders_realism(CareerRecord& record, std::optional<MissionRules>& rules);

  // `patrol_results_screen` (0x639d8).
  void patrol_results(const CareerRecord& record, const MissionResults& results, const PatrolResultsInfo& info);

  // `homeport_screen` (0x4d0e3): the orders to `port` (the career's new
  // home port), marked on the map. Returns the dialog result (-1).
  int homeport(const CareerRecord& record, const HomePort& port, bool transfer);

  // `new_equipment_run` (0x5b501): when equipment is due, fits it on the
  // record and shows its letter. Returns 0 when none was due, else -1
  // (-2 when the dialog was cancelled). Not in the original: when none of
  // the original's is due, the career's `rules` may deliver an extended one.
  int new_equipment(CareerRecord& record, std::int32_t current_command, const MissionRules& rules = {});

  // `awards_present` (0x55ecd): the citation or letter of `code` until a
  // key or click. The speech the original reads with it is not played.
  void present_award(LetterCode code, const CareerRecord& record, SubCommand command);

  // `career_show_transit_calendar` (0x1c539): the date with the days in
  // transit (0x121ffc, not shown when 0) and in port.
  void transit_calendar(const CareerRecord& record, std::int32_t transit_days, std::int32_t port_days);

  // `career_show_end_newspaper` (0x1ca30): PAPER-1.PCX until a key or click.
  void end_newspaper();

  // `new_submarine_screen` (0x7131c): `assign_new_boat` (`carsub_assign_new_boat`)
  // puts a new boat on the record; when it returns false nothing is shown.
  // Returns -1 when the boat was accepted (the counters of a new command
  // reset), -2 when rejected or none was assigned (an offer's boat is then
  // restored).
  int new_submarine(Career& career, const GameTables& tables, SubCommand command, bool offer,
                    const std::function<bool(Career&)>& assign_new_boat);

 private:
  CareerUi& ui_;
  // 0x124250: the citation box width, kept from one citation to the next.
  std::int32_t citation_width_ = 0x106;
  // 0x10e214: the NEW SUBMARINE panel shows the statistics.
  bool subscrn_statistics_ = true;
};

}  // namespace sh
