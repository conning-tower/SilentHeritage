// The results of a single mission (module wscreens): what the mission layer
// hands back (`MissionOutcome`), the results record 0x187254 that
// `mission_compute_results` (0xa1c87) builds from it, with its score, and
// the text of the MISSION RESULTS and BEST MISSIONS screens
// (`mission_results_summary`, 0xa4c95).
//
//   MissionOutcome outcome = mission_outcome(mission, end_code, patrol_log);
//   MissionResults results = compute_mission_results(outcome, options);
//   std::string text = mission_results_summary(results, exe, menu_small_font);
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "game/stations/cabin.h"
#include "sim/context.h"

namespace sh {

class ExeImage;
class Font;
class Mission;

// The fate `0x1872ea` of the boat after a mission (table 0x10eb00).
enum class BoatFate : std::int32_t {
  kOperational = 0,
  kKilled = 1,    // end codes 10..16
  kCaptured = 2,  // end code 6
  kRecalled = 3,  // end code 9: the war is over
};

// The status of an entry of the sunk list (table 0x10eb10).
enum class SunkStatus : std::int32_t { kEscaped = 0, kDamaged = 1, kSunk = 2 };

// One 0x53-byte record of the sunk list 0x187364 (also the career log's
// event record): a patrol log entry with its status and damage.
struct SunkListEntry {
  PatrolLogEntry log;
  SunkStatus status = SunkStatus::kSunk;  // +0x4b
  std::int32_t damage_pct = 100;          // +0x4f

};

// A hostile ship or submarine still afloat at the end of a single mission,
// as `mission_compute_results` reads it.
struct AfloatVessel {
  std::int32_t type = 0;           // SHIPS.DAT +0x19
  std::string class_name;          // SHIPS.DAT +0
  std::int32_t tonnage = 0;        // SHIPS.DAT +0xc4
  std::int32_t hull_strength = 0;  // SHIPS.DAT +0x6c
  std::int32_t hit_points = 0;     // ship +0x3a4, enemy sub +0xa2c
  // The damage not done by the player (ship +0x4dc, enemy sub +0xb3c).
  std::int32_t other_damage = 0;
  LogAngle latitude;
  LogAngle longitude;
};

// What `mission_compute_results` reads of the mission just played.
struct MissionOutcome {
  std::int32_t end_code = 0;  // 0x11a890
  // The clock at the end (0x129c10..0x129c20) and the game minutes the
  // mission lasted (0x129c58).
  std::int32_t day = 1;
  std::int32_t month = 0;
  std::int32_t year = 0;  // since 1930
  std::int32_t hour = 0;
  std::int32_t minute = 0;
  std::int32_t minutes_elapsed = 0;
  std::string boat_name;       // 0x11ac08, e.g. "(SS-305) Skate"
  std::string class_name;      // SUBS.DAT +10 (0x11b12a)
  std::string profile_sprite;  // SUBS.DAT +0x37 (0x11b157), for the port scene
  std::string patrol_area;     // the .WLD region name (0x170f4c)
  LogAngle latitude;           // the boat's position
  LogAngle longitude;
  std::int32_t fuel = 0;               // 0x11ac88
  std::int32_t crew = 0;               // 0x11ac2b, 0 green .. 2 elite
  std::int32_t difficulty = 0;         // 0x125ff0, 0..1000
  std::int32_t hull_damage_pct = 0;    // 0x11b0cc
  std::int32_t system_damage_pct = 0;  // 0x11b0d0
  // The patrol log 0x11b110, in order.
  std::vector<PatrolLogEntry> patrol_log;
  // The hostile vessels afloat, in the vehicle list's order.
  std::vector<AfloatVessel> afloat;
  // The extended variant's rules of the mission (its realism choice counts
  // in the rating).
  MissionRules rules;
};

// The results record 0x187254 (0x11c bytes) with its sunk list.
struct MissionResults {
  std::int32_t month = 0;   // +0
  std::int32_t day = 1;     // +4
  std::int32_t year = 0;    // +8, since 1930
  std::int32_t hour = 0;    // +0xc
  std::int32_t minute = 0;  // +0x10
  std::string sub;          // +0x14 char[100]
  std::string class_name;   // +0x78 char[30]
  BoatFate fate = BoatFate::kOperational;  // +0x96
  std::string patrol_area;  // +0x9a char[40]
  LogAngle latitude;        // +0xc2
  LogAngle longitude;       // +0xcf
  std::int32_t ships_sunk = 0;         // +0xdc
  std::int32_t tonnage = 0;            // +0xe0
  std::int32_t score = 0;              // +0xe4
  std::int32_t crew = 0;               // +0xe8
  std::int32_t hull_damage_pct = 0;    // +0xec
  std::int32_t system_damage_pct = 0;  // +0xf0
  std::int32_t minutes_elapsed = 0;    // +0xf4
  std::int32_t fuel = 0;               // +0xf8
  // +0xfc: -1 after end code 7 (scuttled off a friendly base: the crew is
  // saved, the boat lost); `career_apply_patrol_results` (0x1bc18) then
  // counts a boat lost and repairs the damage, `career_base_loop` offers a
  // new boat.
  std::int32_t boat_lost = 0;
  std::int32_t warships_sunk = 0;      // +0x100: types below 7
  std::int32_t sampans_sunk = 0;       // +0x104
  std::int32_t difficulty = 0;         // +0x108: 0..100
  std::int32_t realism_pct = 0;        // +0x10c
  std::int32_t pilots_rescued = 0;     // +0x114
  std::int32_t photo_recons = 0;       // +0x118
  std::vector<SunkListEntry> sunk;     // +0x110: the sunk list

};

// Returns the fate of the boat after end code `end_code`
// (`mission_compute_results`).
BoatFate fate_of_end_code(std::int32_t end_code);
// Returns true when `mission_compute_results` shows the return to port
// (`show_port_return`): end codes 4, 5, 8, 0x11 and 0x13.
bool returns_to_port(std::int32_t end_code);

// `mission_compute_results` (0xa1c87) with the options word `options`
// (0x11a85c): the record, the sunk list (the patrol log, then for a single
// mission the hostile vessels afloat, escaped or damaged), the counters
// and the score. A single mission's score is lowered by the difficulty
// and the boat's damage; every score is scaled by the realism rating.
MissionResults compute_mission_results(const MissionOutcome& outcome, std::uint32_t options);

// Returns the outcome of `mission` that ended with `end_code`, its patrol
// log being `patrol_log`: the clock, the boat, its position and damage,
// and the hostile ships not sinking. The enemy submarines have no
// simulation class yet and are not listed.
MissionOutcome mission_outcome(const Mission& mission, std::int32_t end_code,
                               std::span<const PatrolLogEntry> patrol_log);

// `mission_results_summary` (0xa4c95): the results text, laid out in
// columns with `menu_pad_text_to_width` in `font` (the text boxes' font
// 0, MENU_S.FNT); its strings come from SH.EXE.
std::string mission_results_summary(const MissionResults& results, const ExeImage& exe, const Font& font);

}  // namespace sh
