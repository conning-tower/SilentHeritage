// EXTENDED.CFG, the extended variant's settings in the save directory (not
// in the original; the original's .SAV files are never touched): text lines
// `key = value`, grouped by the key's prefix:
//   `ui.<key>`     the improvements of `Features` the player switches;
//   `rule.<key>`   the rules of new games (`MissionRules`, game/mission_rules.h);
//   `state.<key>`  what the extended screens remember (the last boat chosen).
// Unknown keys are skipped (and dropped when their section is written
// again); a missing key keeps its default, so the file survives settings
// being added or removed. Lines of other prefixes are kept as they are.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/features.h"

namespace sh {

class SaveDir;

inline constexpr std::string_view kExtendedConfigName = "EXTENDED.CFG";

// Returns the value of each `key = value` line of `text`, keys and values
// trimmed (the last line of a key wins); other lines are skipped.
std::map<std::string, std::string, std::less<>> parse_config(std::string_view text);
// Returns `text` without its lines whose key starts with `prefix` (and its
// empty lines), then `lines` (whole lines, each ending in '\n').
std::string merge_config(std::string_view text, std::string_view prefix, std::string_view lines);
// Returns the text of `dir`'s EXTENDED.CFG, empty without one.
std::string read_config(const SaveDir& dir);
// Replaces the lines of `prefix` of `dir`'s EXTENDED.CFG by `lines`. Returns
// false when it cannot be written.
bool write_config(const SaveDir& dir, std::string_view prefix, std::string_view lines);

// One improvement of `Features` the player switches on the EXTENDED
// OPTIONS page: its key (`ui.<key>`), its label in the menu fonts, and
// whether it can change during a mission.
struct FeatureOption {
  std::string_view key;
  std::string_view label;
  bool Features::* member;
  bool in_mission;
};

// The improvements of the EXTENDED OPTIONS page, in page order. The wide
// screen changes the window's layout and waits for the next mission.
inline constexpr std::array<FeatureOption, 4> kFeatureOptions{{
    {"wide_screen", "WIDE SCREEN", &Features::wide_screen, false},
    {"crew_log", "CREW MESSAGE LOG", &Features::crew_log, true},
    {"chart_tools", "CHART TOOLS", &Features::chart_tools, true},
    {"last_boat", "REMEMBER LAST BOAT", &Features::last_boat, false},
    // Reserved: the improved command bar (ORDERS and -/+ in the bar, else
    // the floating ORDERS button), `{"command_bar", "IMPROVED COMMAND BAR",
    // ...}`, added with its `Features` flag.
}};

// Returns `defaults` with the `ui.` lines of `dir`'s EXTENDED.CFG over them.
Features load_features(const SaveDir& dir, const Features& defaults);
// Writes the `ui.` lines of `features` into `dir`'s EXTENDED.CFG.
bool save_features(const SaveDir& dir, const Features& features);

// The SUB CLASS screen's last chosen class (`state.last_boat`), or nothing.
std::optional<std::string> load_last_boat(const SaveDir& dir);
bool save_last_boat(const SaveDir& dir, std::string_view class_name);

}  // namespace sh
