// The boats and the start of a career (modules campaign/carsub, newcarer
// and newequip): the boat classes of the career (`carsub_build_class_table`)
// and the new-boat assignment (`carsub_assign_new_boat`), the date and
// command choices of START A CAREER and the new record (`career_init_new`),
// and the equipment the boat receives by date (the table 0x10ca58 of
// `new_equipment_letter`).
//
// Campaign months here are those of `campaign_month_index_abs` (0x1e463):
// 0 for any month of 1941, then `month + (year - 1942) * 12 + 1` with
// `month` 1-based (so January 1942 is 2, and dates before 1941 are
// negative).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "core/random.h"
#include "game/career.h"

namespace sh {

class ExeImage;

// One 0x50-byte entry of the career class table 0x117708: a SUBS.DAT class
// with a non-zero career flag (+0x58).
struct CareerBoatClass {
  std::int32_t class_index = 0;  // +0x00: SUBS.DAT +0x24
  std::string title;             // +0x04: SUBS.DAT display name (+10)
  std::string name;              // +0x1d: SUBS.DAT name (+0)
  std::int32_t career_flag = 0;  // +0x28: SUBS.DAT +0x58
  // The months in which the class is assigned as a new boat
  // (`campaign_month_index_abs` of month, full year): SUBS.DAT +0x48/+0x4c
  // and +0x5c/+0x60.
  std::int32_t first_month = 0;  // +0x2c
  std::int32_t first_year = 0;   // +0x30
  std::int32_t last_month = 0;   // +0x34
  std::int32_t last_year = 0;    // +0x38
  // The CARSUB.DAT boats of the class: indices first..last (the first
  // boat's index is the boat count when there is none).
  std::int32_t first_boat = 0;  // +0x3c
  std::int32_t last_boat = 0;   // +0x40
  std::int32_t fuel_capacity = 0;  // +0x44: SUBS.DAT +0x10c
  std::int32_t fuel_use = 0;       // +0x48: SUBS.DAT +0x110
  std::int32_t transit_speed = 0;  // +0x4c: atoi(SUBS.DAT +0xbc)
};

// `carsub_build_class_table` (0x1dd90), in SUBS.DAT order.
std::vector<CareerBoatClass> career_boat_classes(const GameTables& tables);

// `carsub_assign_new_boat` (0x1e124): gives the career the first class (in
// table order) assignable on its date, newer than its current class once it
// has patrolled, and a boat of that class: class, names, fuel, fuel use,
// transit speed and the boat name "(SS-n) Name". `home_command` is the
// command of the career's home port: ComSubPac gets no S-boats; outside
// ComSubPac (by the summary's current command) a boat class comes 4 months
// later to a career already in a boat. Returns false (no change) when no
// class fits, or when a career that has patrolled has the best class (8).
//
// Original bugs kept: the boat is drawn among all the class's boats, not
// only those in service (the count of those is drawn and dropped); when the
// draw gives the career's current boat, the class table's first entry (the
// S-boats) is taken instead and a boat drawn again.
// Throws `DataError` when the chosen class has no CARSUB boat.
bool assign_new_boat(CareerRecord& record, const CareerSummary& summary, SubCommand home_command,
                     const GameTables& tables, WatcomRandom& random);

// ------------------------------------------------------------ new career

// The choices of the START A CAREER dialog (0x124948..0x124958).
struct NewCareerChoice {
  std::string name;          // 0x124958: at most 20 characters
  std::int32_t month = 11;   // 0x124948: 0-based
  std::int32_t year = 11;    // 0x12494c: since 1930
  std::int32_t day = 7;      // 0x124950: 7 on 7 December 1941, else 1
  std::int32_t command = 0;  // 0x124954: index into "Random", ComSubPac, the Asiatic command

  // `new_career_date_in_war` (0x5adee): the date lies in the war.
  bool date_in_war() const;
  // `new_career_next_year` (0x5acfe) and `new_career_next_month`
  // (0x5ad6c): the next year (1941..1945, wrapping) or month in the war,
  // the day reset to 1 (7 for December 1941).
  void next_year();
  void next_month();
  // `new_career_next_command` (0x5adcb).
  void next_command() { command = command < 2 ? command + 1 : 0; }
  // The preferred command of the choice (the table 0x10c4d4: -1 any,
  // 1 ComSubPac, 2 ComSubSoWesPac).
  std::int32_t preferred_command() const;
};

// `new_career_update_command_names` (0x5ac7f): the labels of the command
// choices, "Random", "ComSubPac" and "ComSubsAsiatic" before April 1942,
// else "ComSubSoWesPac" (SH.EXE strings).
std::array<std::string, 3> new_career_command_names(const NewCareerChoice& choice, const ExeImage& exe);

// `career_init_new` (0x5ae56): the career of `choice` with the realism
// values of the options word `options` (REALISM.SAV), in port, dated a day
// or three after the chosen day (one draw), with the next patrol's seed
// (`rand()`) and the equipment of the date (`equipment_for_new_career`
// with `stale_letter`). Fields the original leaves from the previous
// career in memory start at 0. The home port, the boat and the roster slot
// are set afterwards (`career_transfer_home_port`, `assign_new_boat`,
// `find_free_roster_slot`).
Career career_init_new(const NewCareerChoice& choice, std::uint32_t options, WatcomRandom& random,
                       std::int32_t stale_letter = 0);

// ---------------------------------------------------------- new equipment

// One record of the equipment table 0x10ca58 (dates set by the static
// initializer from the table 0x10e408).
struct EquipmentDelivery {
  std::uint32_t bit = 0;     // `.SCN` equipment bit
  std::int32_t month = 0;    // 0-based
  std::int32_t year = 0;     // since 1930
  std::uint32_t name = 0;    // SH.EXE address of the name, e.g. "SD Radar"
  std::uint32_t text = 0;    // SH.EXE address of the description
  bool supplied = false;     // +0x24: supplied by the base, else installed at refit
};

// The table 0x10ca58: SD, SJa, bathythermograph, PPI, SJ-1, then the two
// base supplies, the Mark 18-1 and 18-2 torpedoes (bits 0x40, 0x80). Read
// from SH.EXE (`game_exe`) on the first call.
const std::array<EquipmentDelivery, 7>& equipment_deliveries();

// Returns whether delivery `index` is due on the career's date: its date,
// 3 months later for a refit item outside ComSubPac (summary's current
// command), has come.
bool equipment_due(std::size_t index, const CareerRecord& record, const CareerSummary& summary);

// `new_equipment_letter` (0x5b5c7) without the letter: the first delivery
// not yet fitted that is due, added to the record's equipment; returns its
// index, or -1 when none (the original shows one letter a visit).
std::int32_t deliver_next_equipment(CareerRecord& record, const CareerSummary& summary);

// The new career's equipment (0x5b990): every delivery whose date has come,
// with the 3-month delay decided by record `stale_letter`'s supply flag
// (the original reads the index the last letter left in 0x124cbc instead of
// each record's: 0 at start-up, 7 past the table, whose flag is non-zero).
std::uint32_t equipment_for_new_career(const CareerRecord& record, const CareerSummary& summary,
                                       std::int32_t stale_letter);

// `career_commander_title` (0x1dc92): "Commander " and the name.
std::string career_commander_title(const CareerRecord& record, const ExeImage& exe);

}  // namespace sh
