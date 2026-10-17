// The career's REQUESTS at the base (not in the original; the extended
// rule `MissionRules::career_requests`): what a commander may ask of the
// command while in port, from the BASE MENU's REQUESTS button. Each
// request is an entry of the REQUESTS menu (`career_requests_menu`); what
// is granted is kept in the career (`CareerRequests`, ROSTER<n>.EXT):
//
//   TORPEDO LOAD  the load of the next patrol: the types in service and
//                 supplied (`torpedo_offers`), placed in the tubes and
//                 racks on the status page's torpedo room
//                 (`TorpedoLoadEditor`); the patrol carries it as chosen.
//   DECK GUN      a fleet boat's single gun moved fore or aft, or a second
//                 gun fitted for a commander in good standing (`BoatFit`).
//   PATROL AREA   for a commander in good standing, the zone of the next
//                 patrol among those the command may assign
//                 (`assign_requested_patrol`).
//   BOAT TRANSFER a newer class, granted or not once per port visit
//                 (`boat_transfer_granted`), then the game's own new-boat
//                 offer.
//   REFIT         for a commander in good standing, equipment fitted before
//                 its usual delivery (`early_refit_items`).
//
// Without a request the patrol is the one the game draws: the requests
// apply after its random draws and change none of them.
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/scenario.h"
#include "game/career.h"
#include "game/career_letters.h"
#include "game/career_rules.h"
#include "sim/context.h"

namespace sh {

class CareerUi;
class ExeImage;

// The BASE MENU button and the title of the requests menu.
inline constexpr std::string_view kRequestsLabel = "REQUESTS";

// Returns whether the commander is in good standing, which some requests
// need: at least one Silver Star or a higher award (Silver Star, Navy
// Cross, Distinguished Service Medal, Medal of Honor) or the highest rank
// (2), and the last patrol not a poor one (`poor_patrol_streak` 0).
bool in_good_standing(const CareerRecord& record);

// Returns whether the career keeps requests: its rules have
// `career_requests`.
bool requests_enabled(const Career& career);
// Returns whether `requests` were made on the career's current boat (its
// name and class).
bool requests_for_boat(const CareerRequests& requests, const CareerRecord& record);
// Returns the career's requests for its current boat: those made on
// another boat are dropped first.
CareerRequests& current_requests(Career& career);
// Returns the deck gun fit of the career's boat: none without the rule or
// when it was made on another boat.
BoatFit career_boat_fit(const Career& career);

// Returns the `boat.` and `request.` lines of ROSTER<n>.EXT for `requests`,
// none when nothing was asked: `boat.name`, `boat.class`, `boat.gun_mount`
// (when moved), `boat.second_gun` (when fitted), and the chosen load as
// comma-separated WEAPONS.DAT ids in `request.tubes`, `request.bow_racks`
// and `request.stern_racks`, and the zone asked in `request.patrol_zone`.
std::string career_requests_config(const CareerRequests& requests);
// Returns the requests of the lines of `text`; other lines, and a load
// whose lines are missing or malformed, are ignored.
CareerRequests parse_career_requests(std::string_view text);

// ------------------------------------------------------- torpedo load

// The months a new torpedo type is short: its supply is limited from its
// first service month for this many months.
inline constexpr std::int32_t kNewTorpedoMonths = 3;
// The extra torpedoes of a short type for a commander in good standing.
inline constexpr std::int32_t kGoodStandingSupply = 2;

// A torpedo type the command supplies for a patrol.
struct TorpedoOffer {
  std::uint32_t id = 0;  // WEAPONS.DAT id
  // The most the boat may carry, nothing for no limit but its tubes and
  // racks.
  std::optional<std::int32_t> limit;

  friend bool operator==(const TorpedoOffer&, const TorpedoOffer&) = default;
};

// Returns the types supplied to `sub` on `year` (since 1930) / `month`
// (0-based) for `command`, in increasing id order: the Mark 10 to every
// boat, and to boats with modern torpedoes the Mark 14, the Mark 18-1 and
// 18-2 from their WEAPONS.DAT months and, with `rules.mark27`, the Mark 27
// from its command's month (`mark27_in_service`). In its first
// `kNewTorpedoMonths` months a type is short: at most 4 Mark 18-1, 4 Mark
// 18-2 or 2 Mark 27, `kGoodStandingSupply` more with `good_standing`, and
// never fewer than the `generated` load carries. Throws `DataError` when a
// Mark torpedo is missing from WEAPONS.DAT.
std::vector<TorpedoOffer> torpedo_offers(const SubClass& sub, const GameTables& tables, std::int32_t year,
                                         std::int32_t month, SubCommand command, const MissionRules& rules,
                                         bool good_standing, const TorpedoLoad& generated);
// Returns whether `load` is one the boat may carry: only supplied types,
// within their limits, at most `kMaxTorpedoTypes` types, nothing outside
// the class's tubes and racks.
bool torpedo_load_allowed(const TorpedoLoad& load, const SubClass& sub, std::span<const TorpedoOffer> offers);
// Returns the load a mission of `header` starts with (`scenario_torpedo_load`
// then `tdc_upgrade_mark18_by_date` at the header's date).
TorpedoLoad generated_torpedo_load(const ScenarioHeader& header, const SubClass& sub, const GameTables& tables,
                                   const MissionRules& rules);

// A place of a torpedo load: a tube (the bow tubes first) or a rack slot.
struct TorpedoPlace {
  enum class Group : std::uint8_t { kTube, kBowRack, kSternRack };
  Group group = Group::kTube;
  std::size_t index = 0;

  friend bool operator==(const TorpedoPlace&, const TorpedoPlace&) = default;
};

// The TORPEDO LOAD request's working load: torpedoes taken from the
// supply, moved between the places by hand (one held at a time, counted
// aboard) or returned, within `torpedo_load_allowed`.
class TorpedoLoadEditor {
 public:
  // Why a torpedo cannot be taken from the supply.
  enum class Refusal : std::uint8_t {
    kNone,
    kNotOffered,  // not supplied, or a torpedo is held
    kSupply,      // its limit is reached
    kTypes,       // it would be a fourth type
    kFull,        // no empty place
  };

  // Starts from `chosen` when it is allowed, else from `generated` (the
  // patrol's load without request), for `sub` (which must outlive the
  // editor) with the `offers` of the patrol.
  TorpedoLoadEditor(const SubClass& sub, const TorpedoLoad& generated, std::vector<TorpedoOffer> offers,
                    const std::optional<TorpedoLoad>& chosen);

  const TorpedoLoad& load() const { return load_; }
  const TorpedoLoad& generated() const { return generated_; }
  const std::vector<TorpedoOffer>& offers() const { return offers_; }
  // The number of places of `group` on the class.
  std::size_t places(TorpedoPlace::Group group) const;
  // The torpedo at `place` (0: empty).
  std::int32_t at(TorpedoPlace place) const;
  // The torpedo held, 0 for none.
  std::uint32_t held() const { return held_; }
  // The torpedoes of `id` aboard, the one held included.
  std::int32_t count(std::uint32_t id) const;
  // The limit of `id` (nothing: none), as offered.
  std::optional<std::int32_t> limit(std::uint32_t id) const;
  // The types aboard, the one held included, in increasing order.
  std::vector<std::uint32_t> types() const;

  // Returns why a torpedo of `id` cannot be taken, `kNone` when it can.
  Refusal can_take(std::uint32_t id) const;
  // Takes a torpedo of `id` from the supply into the hand; false when
  // `can_take` refuses.
  bool take(std::uint32_t id);
  // Picks up the torpedo at `place`; false when one is held or the place
  // is empty.
  bool pick(TorpedoPlace place);
  // Puts the held torpedo at `place`, taking up the one there if any;
  // false without a torpedo held.
  bool put(TorpedoPlace place);
  // Returns the held torpedo to the supply.
  void drop() { held_ = 0; }
  // Loads a torpedo of `id` into the first empty place (tubes, bow racks,
  // stern racks); false when `can_take` refuses.
  bool add(std::uint32_t id);
  // Unloads the last torpedo of `id` (stern racks, bow racks, tubes, from
  // the end); false when none is aboard or one is held.
  bool remove(std::uint32_t id);
  // Goes back to the generated load, the held torpedo returned.
  void reset();
  // The load to request, the held torpedo returned: nothing when it is the
  // generated one.
  std::optional<TorpedoLoad> chosen() const;

 private:
  std::int32_t& slot(TorpedoPlace place);
  std::optional<TorpedoPlace> first_empty() const;

  const SubClass* sub_;
  TorpedoLoad generated_;
  std::vector<TorpedoOffer> offers_;
  TorpedoLoad load_;
  std::uint32_t held_ = 0;
};

// --------------------------------------------------------- patrol area

// Returns whether the career may ask for its next patrol's zone: requests
// on, a commander in good standing, zones to choose from
// (`patrol_zone_candidates`).
bool can_request_patrol_area(const Career& career, const CampaignData& data);
// `assign_patrol` (with all its draws), then the zone the career asked for
// in place of the one drawn, with its transit, when it may still ask
// (`can_request_patrol_area`) and the zone is still a candidate; the
// career's last zone becomes it.
std::optional<PatrolAssignment> assign_requested_patrol(Career& career, const CampaignData& data,
                                                        WatcomRandom& random);

// ------------------------------------------------------- boat transfer

// The chance of a granted transfer at most, in percent.
inline constexpr std::int32_t kMaxTransferChance = 75;

// Returns whether the career may ask for a boat transfer: requests on, at
// least a patrol made, and a newer class to give (`next_boat_class`).
bool can_request_boat_transfer(const Career& career, SubCommand home_command, const GameTables& tables);
// Returns the chance in percent of a granted transfer: 10 per level of the
// highest award held (Bronze Star 1, Silver Star 2, Navy Cross or
// Distinguished Service Medal 3, Medal of Honor 4), 10 per rank above the
// first, 5 per patrol in the boat beyond the second, at most
// `kMaxTransferChance`; 0 after a poor patrol.
std::int32_t boat_transfer_chance(const CareerRecord& record);
// Returns whether a transfer asked now is granted: a draw against
// `boat_transfer_chance` fixed by the career's next patrol seed and its
// patrols, so the same in a whole port visit.
bool boat_transfer_granted(const CareerRecord& record);

// --------------------------------------------------------------- refit

// The months a REFIT may come before an item's usual delivery.
inline constexpr std::int32_t kRefitAdvance = 3;

// Returns the equipment a REFIT can fit now, in delivery order (the
// table's entries, `equipment_entry`, then the extended ones with their
// rules, `extended_equipment_entries`): the items installed at a refit
// (not the base's torpedo supplies), not fitted, whose usual delivery (their
// date, 3 months later outside ComSubPac: `current_command` != 1) is at
// most `kRefitAdvance` months away and whose date has come.
std::vector<EquipmentEntry> early_refit_items(const CareerRecord& record, std::int32_t current_command,
                                              const MissionRules& rules, const ExeImage& exe);
// Returns whether the career may ask for a refit: requests on, a commander
// in good standing, and an item to fit.
bool can_request_refit(const Career& career, const ExeImage& exe);

// ----------------------------------------------------------- deck gun

// Returns whether the gun of the career's boat can be moved: a fleet boat
// with one gun (`fleet_boat_with_one_gun`) not fitted with a second.
bool can_move_deck_gun(const SubClass& sub, const BoatFit& fit);
// Returns whether a second gun can be fitted: a fleet boat with one gun,
// none fitted yet, for a commander in good standing.
bool can_fit_second_gun(const SubClass& sub, const BoatFit& fit, const CareerRecord& record);

// ------------------------------------------------------------ screens

// The labels of the REQUESTS menu's entries.
inline constexpr std::string_view kTorpedoLoadLabel = "TORPEDO LOAD";
inline constexpr std::string_view kDeckGunLabel = "DECK GUN";
inline constexpr std::string_view kPatrolAreaLabel = "PATROL AREA";
// PATROL AREA's choice of the zone the command assigns.
inline constexpr std::string_view kAsAssignedLabel = "AS ASSIGNED";
inline constexpr std::string_view kBoatTransferLabel = "BOAT TRANSFER";
inline constexpr std::string_view kRefitLabel = "REFIT";

// One entry of the REQUESTS menu.
struct RequestItem {
  std::string label;
  // A disabled entry is greyed.
  bool enabled = true;
};

// The REQUESTS menu over RTNPORT.PCX (loaded and faded in when `redraw`),
// laid out as BASE MENU: one button per item, then BASE MENU. Returns the
// index of the item chosen, nothing for BASE MENU.
std::optional<std::size_t> requests_menu_screen(CareerUi& ui, std::span<const RequestItem> items, bool redraw);

// A choice among `rows` over the screen as it is: `title`, the list
// starting on row `selection`, CANCEL and SELECT (or a double click).
// Returns the row selected, nothing for CANCEL.
std::optional<std::size_t> list_choice_screen(CareerUi& ui, std::string_view title,
                                              std::span<const std::string> rows, std::size_t selection);

// TORPEDO LOAD over STATUSBG.PCX: the supply of each offered type above
// (its torpedo, the number aboard and its limit, - and + buttons), the
// boat's torpedo rooms of the status page and the cards of the types
// aboard; a click picks up a torpedo from the supply or a place and puts it
// in a place (taking up the one there) or back on the supply. DEFAULT LOAD
// goes back to the generated load. `patrol_month` / `patrol_year` (0-based,
// since 1930) name the patrol. Returns true on ACCEPT (the editor holds the
// load), false on CANCEL.
bool torpedo_load_screen(CareerUi& ui, const GameTables& tables, const SubClass& sub, TorpedoLoadEditor& editor,
                         std::int32_t patrol_month, std::int32_t patrol_year);

// DECK GUN over RTNPORT.PCX: the boat's guns, MOVE GUN FORWARD / AFT while
// it has one, FIT SECOND GUN (with a confirmation; greyed unless
// `can_fit_second_gun`), DONE. Changes `fit` as asked.
void deck_gun_screen(CareerUi& ui, const SubClass& sub, const CareerRecord& record, BoatFit& fit);

}  // namespace sh
