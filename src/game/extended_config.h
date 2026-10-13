// EXTENDED.CFG, the extended variant's settings in the save directory (not
// in the original; the original's .SAV files are never touched): text lines
// `key = value`, grouped by the key's prefix:
//   `ui.<key>`     the improvements of `Features` the player sets
//                  (`kFeatureOptions`);
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
#include <span>
#include <string_view>
#include <type_traits>

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

// One improvement of `Features` the player sets on the EXTENDED OPTIONS
// pages: a check box (a `bool` member) or a choice (an enum member whose
// values are 0 .. `choices.size() - 1`, shown by a cycle button). Its value
// is kept as the integer `ui.<key> = <value>`.
struct FeatureOption {
  std::string_view key;
  // The check box's label (menu fonts); unused for a choice.
  std::string_view label;
  // The choice's labels by value; empty for a check box.
  std::span<const std::string_view> choices;
  int (*get)(const Features&);
  void (*set)(Features&, int);
  // Whether it can change during a mission (else greyed there).
  bool in_mission;

  // Returns the number of values.
  constexpr int values() const { return choices.empty() ? 2 : static_cast<int>(choices.size()); }
};

namespace detail {
template <auto Member>
constexpr int get_feature(const Features& f) {
  return static_cast<int>(f.*Member);
}
template <auto Member>
constexpr void set_feature(Features& f, int value) {
  f.*Member = static_cast<std::remove_cvref_t<decltype(f.*Member)>>(value);
}
}  // namespace detail

// Returns the check box of the `bool` member `Member`.
template <auto Member>
constexpr FeatureOption feature_check_box(std::string_view key, std::string_view label, bool in_mission) {
  return {key, label, {}, &detail::get_feature<Member>, &detail::set_feature<Member>, in_mission};
}
// Returns the choice of the enum member `Member`, `choices` by value.
template <auto Member>
constexpr FeatureOption feature_choice(std::string_view key, std::span<const std::string_view> choices,
                                       bool in_mission) {
  return {key, {}, choices, &detail::get_feature<Member>, &detail::set_feature<Member>, in_mission};
}

// The command bar's layouts by `CommandBarStyle`.
inline constexpr std::array<std::string_view, 4> kCommandBarChoices{
    command_bar_label(CommandBarStyle::kOriginal), command_bar_label(CommandBarStyle::kOrdersButton),
    command_bar_label(CommandBarStyle::kBarOrders), command_bar_label(CommandBarStyle::kBarOrdersTime)};

// The improvements the player sets on the EXTENDED OPTIONS pages, in page
// order (two per row on the main menu's); the extended variant's others are
// always on. Both apply at once during a mission: the window's shape and
// the stations' layout follow the wide screen, the bar its layout.
inline constexpr std::array kFeatureOptions{
    feature_check_box<&Features::wide_screen>("wide_screen", "WIDE SCREEN", true),
    feature_choice<&Features::command_bar>("command_bar", kCommandBarChoices, true),
};

// Returns `defaults` with the `ui.` lines of `dir`'s EXTENDED.CFG over them.
Features load_features(const SaveDir& dir, const Features& defaults);
// Writes the `ui.` lines of `features` into `dir`'s EXTENDED.CFG.
bool save_features(const SaveDir& dir, const Features& features);

// The SUB CLASS screen's last chosen class (`state.last_boat`), or nothing.
std::optional<std::string> load_last_boat(const SaveDir& dir);
bool save_last_boat(const SaveDir& dir, std::string_view class_name);

}  // namespace sh
