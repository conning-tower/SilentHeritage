// The File menu's commands (module guiuser): Save Scenario
// (`cmd_save_scenario`, SE 0x2a291), Run (`cmd_run_scenario`, 0x2a174),
// Load Historical / Load Scenario / Delete Scenario (`cmd_load_scenario`,
// 0x2a910), New, "Save changes to scenario?" (`ask_save_changes`), the
// checks before saving and running (`check_ships_in_range`,
// `check_scenario_size`), the lists of the file dialogs
// (`save_list_build`, `scenario_list_build`) and the writer's results
// (`scen_write_ascn` through `dfile_open` / `dfile_write`).
//
// CUSTOM\ is a `SaveDir`: written in the player's directory, read there and
// then in the install's CUSTOM\ (a scenario deleted from the install is
// masked by an empty file, which lists as unreadable and is left out).
// SCEN\HIST???.SCN is read from the install. The boxes run over whatever
// palette the caller set (the menu dispatcher greys it).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "editor/dialogs_file.h"
#include "editor/editor_ui.h"
#include "editor/scenario.h"
#include "game/frontend.h"
#include "game/saves_options.h"
#include "game/shpath.h"

namespace sh::editor {

// The file Run writes (SE 0x98310) and hands to the game.
inline constexpr std::string_view kRunFileName = "AEONSCEN.TMP";

// What writing a scenario gives (`scen_write_ascn`'s codes: 0, `dfile_write`
// 1 / 2, `dfile_open` 4..6).
enum class WriteResult : std::uint8_t {
  kOk = 0,
  kWriteFailed = 1,  // write() failed
  kDiskFull = 2,     // a short write (no space left)
  kCannotOpen = 4,   // the file could not be created
};

// Writes `bytes` to `path`, creating its directory. `path` is replaced
// only once the whole file is written: on failure it keeps its content.
WriteResult write_scenario_file(const std::filesystem::path& path, std::span<const std::uint8_t> bytes);

// `save_list_build` (SE 0x2a5fb): the names of the CUSTOM\*.SCN files
// without extension, sorted; a deleted (masked) file is left out.
std::vector<std::string> save_list_build(const SaveDir& custom);

// What the File commands work on.
struct FileEnv {
  EditorUi& ui;
  Scenario& scenario;
  const Install& install;
  const ShPaths& paths;
  // CUSTOM\: the player's directory over the install's.
  const SaveDir& custom;
};

// `scenario_list_build` (SE 0x2ac55): CUSTOM\*.SCN (`historical` false) or
// SCEN\HIST???.SCN, sorted by name, each read like `scen_load_ascn` without
// its records: a custom entry's title is its name without extension and
// its description the whole briefing; a historical one's title is the
// first briefing line (with its '\n', at most 49 characters) and its
// description the rest. Files whose header cannot be read are left out.
// The current scenario is not touched (SE's reads overwrote its version and
// briefing: the original bug "scenario lists overwrite the briefing",
// fixed).
std::vector<ScenarioListEntry> scenario_list_build(const FileEnv& env, bool historical);

class FileCommands {
 public:
  explicit FileCommands(FileEnv env) : env_(env) {}

  // The scenario name 0xa6bb8 (the SAVE SCENARIO entry): empty after New,
  // a historical load or a failed load, the file's name after a custom
  // load or a save.
  std::string name;

  // `check_ships_in_range` (SE 0x2b207): with Exit On Break Contact set and
  // no object in the player's visual or radar range nor any airstrike /
  // photo recon, "NO SHIPS IN RANGE?" asks to go on (`running`: Run's
  // "continue?", else "save?"). Returns false to abort.
  bool check_ships_in_range(bool running);
  // `check_scenario_size` (SE 0x2b2b2): "SCENARIO IS TOO LARGE" when the
  // estimate exceeds the budget. Returns false to abort.
  bool check_scenario_size(bool running);

  // File / Save Scenario (`cmd_save_scenario`): the checks, SAVE SCENARIO
  // over the CUSTOM list, "OVERWRITING SCENARIO" for an existing name, the
  // write to CUSTOM\<name>.SCN and its error boxes (DISK FULL, CANNOT OPEN
  // FILE, UNABLE TO WRITE TO FILE). Returns -1 when saved (the modified
  // flag cleared), -2 when cancelled or failed (the name restored when
  // cancelled).
  int save();
  // `ask_save_changes` (SE 0x2b1c6) with `save`: -1 to go on (not
  // modified, DISCARD, or saved), -2 to stop.
  int ask_save_changes();
  // File / New (menu selection 10): "Save changes to scenario?", then a new
  // scenario and an empty name. Returns true when the scenario was
  // replaced.
  bool new_scenario();
  // File / Run (`cmd_run_scenario`): the checks with "continue?", "Save
  // changes to scenario?" (whose save skips the range check), the write of
  // CUSTOM\AEONSCEN.TMP. Returns true when the editor ends with Run.
  bool run();
  // Load Historical / Load Scenario / Delete Scenario (`cmd_load_scenario`
  // with `mode`). A load asks to save changes first, lists the files,
  // reads the chosen one (the error box and a new scenario when it cannot
  // be read), asks its password (`ask_password`), and leaves a new
  // scenario on the previous world when refused. Delete asks "DELETING
  // SCENARIO" and removes CUSTOM\<file> ("CANNOT DELETE FILE" on failure).
  // Returns true when the scenario was replaced (loaded, or new after a
  // failure).
  bool load(ScenarioListMode mode);

 private:
  // Shows the error box of a `WriteResult` for DOS path `path`; `opening`
  // is the text of the codes 4..6 (CANNOT OPEN FILE / CANNOT CREATE FILE).
  void write_error(WriteResult result, std::uint32_t opening, const std::string& path);
  // Reads the scenario at DOS path `path` ("CUSTOM\X.SCN" or
  // "SCEN\HIST01.SCN") into the scenario with the password prompt
  // (`scen_load_ascn` + `ask_password` + the spawns). Returns -1 loaded, 0
  // refused (a new scenario on the previous world), 1 unreadable (the error
  // box shown, a new scenario).
  int read_into_scenario(const std::string& path, bool custom, const std::string& file);
  std::string str(std::uint32_t address) const { return env_.ui.str(address); }

  FileEnv env_;
  // 0x9bc90: Save checks the range unless called from Run.
  bool range_check_ = true;
};

}  // namespace sh::editor
