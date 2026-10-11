// The single-mission scenario generator (module scengen, 0x68453..0x6c41f):
// turns the encounter parameters of the front end into a `Scenario`, the
// way `scengen_build_single_mission` fills the `.SCN` globals in memory
// (the original writes no temporary `.SCN`: it fills the header and unit
// arrays that `scen_load_ascn` would have filled, and `init_simulation`
// goes on from there).
//
// The group builders (`ScenarioGenerator::build_*`) and the zone data are
// the parts the career's contact groups share (`try_materialize` writes
// the same parameters and calls the same builders); the mission kinds
// (`scengen_mission_*`) are single-mission only.
//
// Every random draw happens in the original order on the generator given;
// `init_simulation` runs the generator right after its two first draws
// (`rand()` for the restore seed, `random_int(30)` for the fog density),
// in place of `scen_load_ascn` (and without its `random_int(100)`).
//
// Two results of the original live outside the `.SCN` record:
// `GeneratedMission::start_bearing` (0x126098, read by
// `init_place_player_sub`) and the start time: the header's hour is -1
// and its minute the time-of-day setting, resolved by
// `clock_init_time_of_day` at the player's position.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "assets/bases.h"
#include "assets/dat_tables.h"
#include "assets/patrol_zones.h"
#include "assets/routes.h"
#include "assets/scenario.h"
#include "core/fixed.h"
#include "core/install.h"
#include "core/random.h"
#include "game/frontend.h"

namespace sh {

// ------------------------------------------------------------- calendar

// Returns the days from 7 December 1941 to `day` (1-based) / `month`
// (0-based) / `year` (since 1930), 100000 when `year` < 0
// (`campaign_day_number`, 0x173a4). Throws `DataError` for a date before
// the start (the original loops forever).
std::int32_t campaign_day_number(std::int32_t day, std::int32_t month, std::int32_t year);

// Returns the `ScenarioHeader::equipment` bits fitted on `month` (0-based)
// of `year` (since 1930) by the date table 0x10e408; day ignored.
std::uint32_t equipment_on_date(std::int32_t month, std::int32_t year);

// ------------------------------------------------------------ zone data

// A base's entry in the owner table 0x116248.
struct BaseOwner {
  BaseSide side = BaseSide::kNone;
  // Heading from the station point to the harbour point (active bases).
  Angle10 harbor_heading;
};

// The data of the patrol zone a scenario is generated in: the `PZ.DAT`
// record (0x11b808), its convoy routes (`load_convoy_routes`) and bases
// (`bases_load`).
struct ZoneData {
  PatrolZone zone;
  std::vector<ConvoyRoute> routes;
  std::vector<BaseRecord> bases;
  std::vector<BaseOwner> owners;

  // Loads the routes and bases of `zone` and the harbour headings
  // (`bases_init_owner_table`); the sides are left kNone. Throws
  // `DataError` when a file is missing or malformed.
  static ZoneData load(const Install& install, const PatrolZone& zone);

  // Sets the owner sides on a date (`bases_update_owners`, 0x16852).
  void update_owners(std::int32_t day, std::int32_t month, std::int32_t year);
};

// A base pick: the index into `ZoneData::bases` (-1 when none) and its
// size class minus 1 (0 when none).
struct BasePick {
  std::int32_t base = -1;
  std::int32_t size = 0;
};

// `bases_pick_random_japanese_base` (0x1720b): a random active Japanese
// base with an anchorage, weighted by `pick_weight`; `no_recon` skips the
// bases flagged kNoRecon. One draw.
BasePick pick_random_japanese_base(const ZoneData& zone, bool no_recon, WatcomRandom& random);

// ---------------------------------------------------------- the generator

// A point of the world plane (world units: x east, z north).
struct WorldPoint {
  std::int32_t x = 0;
  std::int32_t z = 0;
};

// A path of world points (the route part a group follows).
using WorldPath = std::vector<WorldPoint>;

// The box a patrol covers (`scengen_build_patrol`'s outputs).
struct PatrolBox {
  std::int32_t width = 0;
  std::int32_t height = 0;
};

// The group builders over one scenario's unit lists, with the generator
// parameters 0x1265cc..0x126604 and the per-scenario used-class flags
// (0x126608, 0x126614).
class ScenarioGenerator {
 public:
  // `options` is the options word (realism bits) the aircraft builder
  // reads; `limit_classes` is the low-memory flag 0x1242d0 (at most 3
  // merchant/escort classes). All references must outlive the generator.
  ScenarioGenerator(const GameTables& tables, GeneratorParams& params, Scenario& scenario, WatcomRandom& random,
                    std::uint32_t options, bool limit_classes = false);

  // `scengen_build_convoy` (0x69f08): a merchant convoy at `at` heading
  // `heading`, its leader following `path`, plus escorts
  // (`scengen_add_convoy_escorts`, alerted when `alerted`); replaces the
  // ships. `fast`: every merchant makes 15 kn.
  void build_convoy(WorldPoint at, Angle10 heading, const WorldPath& path, bool alerted, bool fast,
                    bool allow_coastal_tankers, std::int32_t group);
  // `scengen_build_task_force` (0x6a31f): IJN warships of the warship
  // type parameter in convoy formation, plus escorts; replaces the ships.
  // Throws `DataError` when a tier has no ship left (the original hangs).
  void build_task_force(WorldPoint at, Angle10 heading, const WorldPath& path, bool alerted, std::int32_t group);
  // `scengen_build_patrol` (0x6a81a): patrol ships around `at`; `append`
  // keeps the ships already there. Returns the patrol box.
  PatrolBox build_patrol(WorldPoint at, Angle10 heading, bool tight, bool append);
  // `scengen_build_anchored_ships` (0x6ad25): ships anchored at `at`
  // (unless `no_ships`) with `merchant_pct` % merchants, plus escort-level
  // guard ships; decrements the size parameter like the original.
  void build_anchored_ships(WorldPoint at, std::int32_t merchant_pct, Angle10 heading, bool no_ships, bool append);
  // `scengen_build_enemy_subs` (0x6b5e3): `count` enemy submarines around
  // `at`, following `path` (offset by each boat's place) or intercepting
  // `target` ((-1, -1) = the player); replaces the enemy subs.
  void build_enemy_subs(WorldPoint at, Angle10 heading, const WorldPath& path, bool submerged, std::int32_t count,
                        bool intercept, WorldPoint target);
  // `scengen_build_aircraft` (0x6c41f): `count` aircraft orbiting `at`,
  // covering `escort_group` (0: none); replaces the aircraft.
  void build_aircraft(WorldPoint at, std::int32_t count, std::int32_t escort_group);

  // The class pickers (the records come from `tables`).
  // `scengen_pick_merchant_class` (0x6bc36).
  const ShipClass& pick_merchant_class(bool allow_coastal_tanker);
  // `scengen_pick_escort_class` (0x6bdda).
  const ShipClass& pick_escort_class();
  // `scengen_pick_ijn_warship` (0x6bf85): an unused IJN ship in
  // commission of a class with role warship and type in [lo, hi]; writes
  // its name to `name` and the candidate count to `candidates`. Returns
  // nullptr when there is none.
  const ShipClass* pick_ijn_warship(std::string& name, std::int32_t type_lo, std::int32_t type_hi,
                                    std::int32_t& candidates);
  // `scengen_pick_sub_class` (0x6c245).
  const ShipClass& pick_sub_class();
  // `scengen_pick_small_craft_class` (0x6c353).
  const ShipClass& pick_small_craft_class();

 private:
  struct UsedClasses {
    std::vector<bool> used;
    std::int32_t distinct = 0;  // 0x126610
  };

  void add_convoy_escorts(Angle10 heading, std::size_t first, std::int32_t extent_x0, std::int32_t extent_z0,
                          std::int32_t extent_x1, std::int32_t extent_z1, std::int32_t speed, bool alerted,
                          std::int32_t group, std::size_t count);
  // Turns the ships from `first` on by `heading` and moves them to `at`,
  // with the waypoints of the ships with `orders`.
  void place_ships(std::size_t first, WorldPoint at, Angle10 heading, ShipOrders orders);
  bool in_service(std::uint32_t start_year, std::uint32_t start_month, std::uint32_t end_year,
                  std::uint32_t end_month) const;
  std::int32_t full_year() const { return params_.year + kScenarioYearBase; }

  const GameTables& tables_;
  GeneratorParams& params_;
  Scenario& scenario_;
  WatcomRandom& random_;
  std::uint32_t options_;
  bool limit_classes_;
  std::optional<UsedClasses> ship_used_;  // 0x126614
  std::optional<std::vector<bool>> ijn_used_;  // 0x126608
};

// `scen_fill_torpedo_load` (0x69a67): fills the header's tubes and reloads
// for `sub` on the header's date: Mark 10 on old boats, else Mark 14,
// replaced by or mixed with the "MARK18-1" after its date (both string
// table entries are "MARK18-1", so the intended Mark 18-2 mix never
// happens). Throws `DataError` if a torpedo is missing from WEAPONS.DAT.
//
// The extended variant's rules (not in the original), with the same draws:
// `rules.mixed_mark18` mixes the real Mark 18-2 once it is in service;
// `rules.mark27` puts two Mark 27 in the stern racks (in place of two of
// their torpedoes) of a boat with modern torpedoes once it is supplied to
// `command`, when `tables` has its record (game/torpedo_load.h).
void fill_torpedo_load(ScenarioHeader& header, const SubClass& sub, const GameTables& tables, WatcomRandom& random,
                       const MissionRules& rules = {}, SubCommand command = SubCommand::kComSubPac);

// Returns the SUBS.DAT classes the submarine selection offers on `month`
// (0-based) of `year` (since 1930) (`encounter_sub_class_in_service`,
// 0xa1902), in file order.
std::vector<const SubClass*> sub_classes_in_service(const GameTables& tables, std::int32_t month, std::int32_t year);

// A generated single mission.
struct GeneratedMission {
  Scenario scenario;
  // The patrol zone (0x11b808) with its routes and bases.
  ZoneData zone;
  // 0x126098: the bearing from the target at which `init_place_player_sub`
  // moves the boat out, -1 for no move.
  std::int32_t start_bearing = -1;
};

// `scengen_build_single_mission` (0x68453): the scenario of an encounter
// mission of `type` for the parameters (with the boat class of the
// submarine selection), drawing from `random`. `options` is the options
// word. Throws `DataError` when the type is not an encounter, a file or
// class is missing, or no zone fits the date (a fatal error 0x82 in the
// original).
// `rules` are the extended variant's (`fill_torpedo_load`).
GeneratedMission generate_single_mission(const Install& install, const GameTables& tables, MissionType type,
                                         const GeneratorParams& params, std::uint32_t options,
                                         WatcomRandom& random, const MissionRules& rules = {});

}  // namespace sh
