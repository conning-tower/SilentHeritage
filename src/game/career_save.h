// The career files of the SAVE directory (module campaign/carldsv and
// croster, career.cpp): the rosters `ROSTER<n>.SAV` (one per career, n
// 0..99) and the BEST CAREERS table `BESTCARS.SAV`.
//
// ROSTER<n>.SAV (`career_save` 0x18e52): u8 version (7), the career record
// (0x451 bytes), the summary block (0x74, version 5 and up), the career log
// (`CareerRecord::log_count` records of 0x53 bytes) and, for a career saved
// on patrol, the `sim_write_state` block (kept as read here).
//
// BESTCARS.SAV (`best_careers_load` 0x1d896): u8 version (3), then 10 slots
// of 0x8609 bytes: i32 used, the career record, i32 log count and 400 log
// records (50 before version 3). The original writes the stale bytes of the
// free slots and of the unused records; they are kept as read.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/random.h"
#include "game/career.h"

namespace sh {

class ExeImage;
class Font;
class SaveDir;
struct GameTables;

inline constexpr std::uint8_t kRosterVersion = 7;
inline constexpr std::int32_t kRosterSlots = 100;

// Returns "ROSTER<slot>.SAV".
std::string roster_file_name(std::int32_t slot);
// Returns "ROSTER<slot>.EXT", the extended variant's file of the career's
// rules (`Career::rules`, `rule.<key> = <value>` lines, game/mission_rules.h)
// and requests (`Career::requests`, game/career_requests.h); not in the
// original.
std::string career_rules_file_name(std::int32_t slot);
// Returns the rules of ROSTER<slot>.EXT over the extended defaults, or
// nothing without the file.
std::optional<MissionRules> load_career_rules(const SaveDir& saves, std::int32_t slot);
// Returns the requests of ROSTER<slot>.EXT, none without the file.
CareerRequests load_career_requests(const SaveDir& saves, std::int32_t slot);

// A parsed roster file.
struct RosterFile {
  std::uint8_t version = kRosterVersion;
  Career career;
};

// Parses a whole ROSTER file with the rules of `career_list_rosters`: before
// version 5 there is no summary (it becomes -1, -1, the record's patrols
// from port, 0...), before version 6 its fog is 0, before version 2 the
// record's +0x120..+0x12b are 0. The log stops at a short read like
// `career_load_log_entries`; what follows a career saved on patrol is kept
// raw. Throws `DataError` when the record or summary is short.
RosterFile parse_roster(std::span<const std::uint8_t> data);
// Returns the file `career_save` writes: version 7, the record with its log
// count set to the sunk entries, the summary, the sunk entries of the log,
// then the patrol state when `on_patrol` is set.
std::vector<std::uint8_t> serialize_roster(const Career& career);

// `career_save` (0x18e52): writes the career to ROSTER<roster_slot>.SAV,
// first setting the record's log count, and its rules, when it has some, to
// ROSTER<roster_slot>.EXT with its requests. Returns false when it cannot be written; the file
// then keeps its previous content (the original deletes it).
bool save_career(const SaveDir& saves, Career& career);
// Reads ROSTER<slot>.SAV whole, or nothing when it is missing, empty or
// malformed.
std::optional<RosterFile> load_roster(const SaveDir& saves, std::int32_t slot);
// `career_delete_roster_file` (0x19252), with the career's ROSTER<slot>.EXT.
void delete_roster(const SaveDir& saves, std::int32_t slot);
// `career_find_free_roster_slot` (0x18054): the first slot from 0 without a
// file (a deleted install file counts as free), 99 when all are taken.
std::int32_t find_free_roster_slot(const SaveDir& saves);

// One career of the roster list.
struct RosterEntry {
  std::string file;  // e.g. "ROSTER0.SAV"
  CareerRecord record;
  CareerSummary summary;
};

// `career_list_rosters` (0x1810b): the record and summary of every readable
// ROSTER*.SAV, in file-name order (the original lists them in directory
// order), with the version rules of `parse_roster`.
std::vector<RosterEntry> list_rosters(const SaveDir& saves);

// `croster_load_entry` (0x35603): the career of `entry` without its log;
// a current command below 0 becomes the home port's (HOMEPORT.DAT, with
// ComSubSoWesPac read as ComSubsAsiatic before April 1942; the original
// only finds the ports open on the career's date).
Career career_from_roster_entry(const RosterEntry& entry, const GameTables& tables);

// `career_load_log_entries` (0x18398) without the sunk list: the
// `log_count` records of the career's file (none for a slot below 0 or a
// count below 1), up to a short read.
std::vector<CareerLogEntry> load_career_log(const SaveDir& saves, const CareerRecord& record);

// `career_load_roster_lists` (0x185f8): for a career on patrol, sets its
// `patrol_state` and `patrol_state_version` from ROSTER<roster_slot>.SAV;
// leaves them unchanged in port or when the file cannot be read.
void load_roster_patrol_state(const SaveDir& saves, Career& career);

// `croster_save_career_as` (0x35669): draws the career's next patrol seed
// (`rand()`, kept in `career`), then writes a copy named `name` (with the
// log of the career's file) into a free slot; the career itself keeps its
// slot. Returns the slot written.
std::int32_t save_career_as(const SaveDir& saves, Career& career, std::string_view name, WatcomRandom& random);

// --------------------------------------------------------- best careers

struct BestCareerEntry {
  CareerRecord record;
  std::vector<CareerLogEntry> log;  // at most 400
};

struct BestCareersTable {
  static constexpr std::uint8_t kVersion = 3;
  static constexpr std::size_t kEntries = 10;
  static constexpr std::size_t kLogSlots = 400;
  static constexpr std::size_t kLogSlotsV2 = 50;
  static constexpr std::size_t kEntrySize = 0x8609;
  static constexpr std::string_view kFileName = "BESTCARS.SAV";

  // The slots in rank order; empty when free (`used` 0).
  std::array<std::optional<BestCareerEntry>, kEntries> entries;
  // The bytes of each slot (kEntrySize each), the fields written over them.
  std::array<std::vector<std::uint8_t>, kEntries> raw;

  BestCareersTable();

  // Parses a whole file; nothing when it is short (the original then shows
  // and records nothing). Before version 2 the records' +0x120..+0x127 are
  // cleared.
  static std::optional<BestCareersTable> parse(std::span<const std::uint8_t> data);
  // Returns the file in the current version.
  std::vector<std::uint8_t> serialize() const;

  // `best_careers_load`: reads BESTCARS.SAV, writing an empty table first
  // when it is missing; nothing when it cannot be read or created.
  static std::optional<BestCareersTable> load(const SaveDir& saves);
  // `best_careers_save` (0x1da68); returns false when it cannot be written.
  bool save(const SaveDir& saves) const;

  // `best_careers_record_score`: the slot a career scoring `score` takes,
  // before the first free slot or the first scoring at most as much; nothing
  // for a score of 0 or past the tenth.
  std::optional<std::size_t> rank_of(std::int32_t score) const;
  // Moves the slots from `rank` down by one (the last drops) and puts
  // `entry` at `rank`, its log cut to 400 records.
  void insert(std::size_t rank, BestCareerEntry entry);
};

// The rows of the BEST CAREERS list (`best_careers_build_list` 0x1ccf5):
// "n name score tonnage", the columns filled with `career_pad_text_to_width`
// in `font` (MENU_L.FNT), for the used slots in order.
std::vector<std::string> best_careers_rows(const BestCareersTable& table, const Font& font);

// `career_end_summary` (0x1ce8d): the summary text of a best career (name,
// status, dates, boat, counters, awards, the ships of its log), laid out
// with `menu_pad_text_to_width` in `font`; strings from SH.EXE.
std::string career_end_summary(const BestCareerEntry& entry, const ExeImage& exe, const Font& font);

}  // namespace sh
