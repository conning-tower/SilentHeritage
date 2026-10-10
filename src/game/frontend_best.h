// The BEST MISSIONS table `BESTMISS.SAV` (`best_missions_load` 0xa6eae,
// `best_missions_save` 0xa70a2, `best_missions_record_score` 0xa6da9): the
// ten best single-mission scores with their commander and results.
//
// File: u8 version (2), then 10 entries of 0x116f bytes: i32 used (-1),
// char[21] name, the results record (0x11c bytes, 0x114 before version 2
// without the pilots and photo recons), i32 sunk-list count and 50 sunk-list
// records of 0x53 bytes. The original writes free slots and unused records
// as the stale heap bytes it holds; they are written as zeros here.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "game/frontend_results.h"

namespace sh {

class SaveDir;

struct BestMissionEntry {
  std::string name;        // +4 char[21]: at most 20 characters
  MissionResults results;  // +0x19, with at most 50 sunk-list entries

};

struct BestMissionsTable {
  static constexpr std::uint8_t kVersion = 2;
  static constexpr std::size_t kEntries = 10;
  static constexpr std::size_t kSunkSlots = 50;
  static constexpr std::size_t kEntrySize = 0x116f;
  static constexpr std::string_view kFileName = "BESTMISS.SAV";

  // The slots in rank order; empty when free.
  std::array<std::optional<BestMissionEntry>, kEntries> entries;

  // Parses a whole file; returns nothing when it is too short (the
  // original then shows and records nothing).
  static std::optional<BestMissionsTable> parse(std::span<const std::uint8_t> data);
  // Returns the file in the current version.
  std::vector<std::uint8_t> serialize() const;

  // `best_missions_load`: reads BESTMISS.SAV, writing an empty table first
  // when it is missing; returns nothing when it cannot be read or created.
  static std::optional<BestMissionsTable> load(const SaveDir& saves);
  // `best_missions_save`; returns false when the file cannot be written.
  bool save(const SaveDir& saves) const;

  // Returns the slot a mission scoring `score` takes: before the first
  // free slot or the first one scoring at most as much; nothing past the
  // tenth.
  std::optional<std::size_t> rank_of(std::int32_t score) const;
  // Moves the slots from `rank` down by one (the last one drops) and puts
  // `entry` at `rank`, its sunk list cut to 50 entries (the original
  // copies them all, past the slot's end).
  void insert(std::size_t rank, BestMissionEntry entry);
};

// The rows of the BEST MISSIONS list (`best_missions_build_list`, 0xa6baa):
// "n name score tonnage" with the columns filled by `text_pad_to_width` in
// `font` (font 1, MENU_L.FNT), for the used slots in order.
std::vector<std::string> best_missions_rows(const BestMissionsTable& table, const Font& font);

}  // namespace sh
