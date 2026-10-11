// The storage of the extended variant's mission rules (`MissionRules`,
// sim/context.h; not in the original): the "RULE" section of a saved
// mission's state extension (game/sim_state.h), and the `rule.<key> =
// <value>` lines of EXTENDED.CFG, the extended variant's settings file in
// the save directory (game/extended_config.h), from which single missions
// take their rules.
//
// Both list the fields of `kMissionRuleFields` by key: an unknown key is
// skipped and a missing one keeps its default, so files stay readable when
// rules are added or removed.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "game/extended_config.h"
#include "sim/context.h"

namespace sh {

class SaveDir;

// Returns the "RULE" section of `rules`: u32 count, then per field a u8 key
// length, the key and its i32 value.
std::vector<std::uint8_t> encode_mission_rules(const MissionRules& rules);
// Returns the rules of a "RULE" section. Throws `DataError` when it is
// truncated.
MissionRules decode_mission_rules(std::span<const std::uint8_t> data);

// Returns the `rule.<key> = <value>` lines of `rules`, one per field.
std::string mission_rules_config(const MissionRules& rules);
// Returns the rules of the `rule.` lines of `text` (EXTENDED.CFG), those of
// `defaults` for the fields without a valid line; other lines are ignored.
MissionRules parse_mission_rules_config(std::string_view text, const MissionRules& defaults = {});
// Returns `text` with its `rule.` lines replaced by those of `rules`, the
// other lines kept in order.
std::string merge_mission_rules_config(std::string_view text, const MissionRules& rules);
// Returns the rules of `dir`'s EXTENDED.CFG over the extended defaults
// (`MissionRules::extended`).
MissionRules load_mission_rules(const SaveDir& dir);
// Writes `rules` into `dir`'s EXTENDED.CFG, keeping its other lines.
// Returns false when it cannot be written.
bool save_mission_rules(const SaveDir& dir, const MissionRules& rules);

}  // namespace sh
