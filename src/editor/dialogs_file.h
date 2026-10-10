// The dialogs of the File menu (module guiuser): SAVE SCENARIO (dialog
// 0xa941c) and the Load / Delete list with its briefing view (dialog
// 0xa9608), as functions over the entries the caller lists. Their commands
// (file_commands.h: `cmd_save_scenario`, `cmd_load_scenario`) do the rest:
// listing CUSTOM\*.SCN (`save_list_build`) and SCEN\HIST???.SCN
// (`scenario_list_build`, whose header reads overwrite the current
// briefing), the range and size warnings, the overwrite and delete
// confirmations, the writer and its error boxes, the password and the load
// itself.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "editor/editor_ui.h"
#include "game/frontend.h"

namespace sh::editor {

// `save_name_on_done` (SE 0x2a7c2): the name entry's text upper-cased and
// cut at its first space.
void save_name_on_done(std::string& name);

// SAVE SCENARIO: the CUSTOM scenarios `names` (without extension, in the
// caller's order: `save_list_build` sorts them) and the name entry (8
// characters) on `name`, the scenario name 0xa6bb8; clicking a name copies
// it into the entry, Enter or SAVE accepts. Returns true with the new name,
// false with `name` restored after CANCEL or with an empty name.
bool run_save_scenario_dialog(EditorUi& ui, const std::vector<std::string>& names, std::string& name);

// The modes of the Load / Delete dialog (`cmd_load_scenario`'s argument).
enum class ScenarioListMode { kLoadHistorical = 0, kLoadScenario = 1, kDeleteScenario = 2 };

// The Load / Delete dialog: the titles of `entries` (`title`, at most 49
// characters for historical ones) and the selected one's briefing
// (`description`) in a read-only FONT1 view with scroll arrows greyed when
// it cannot scroll; the title and the button by `mode`, the button greyed
// when the list is empty. Returns the selected index, or nothing after
// CANCEL (or without entries).
std::optional<std::size_t> run_scenario_list_dialog(EditorUi& ui, ScenarioListMode mode,
                                                    const std::vector<ScenarioListEntry>& entries);

}  // namespace sh::editor
