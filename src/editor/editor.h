// The scenario editor program (`se_main`, SE 0x10010, from `sb_load_data`
// on): the title box while the data loads, the chart screen's loop
// (`sbchart_run`) until an exit code is set, the menu bar and its hotkeys
// (`editor_menu_handle_event`, 0x2491b) dispatching to the File commands and
// the Edit dialogs, and the exit code SH reads (docs/editor-design.md,
// "Hand-off with the game").
//
//   Editor editor(services, host, true);
//   EditorExit exit = editor.run();  // kRun: CUSTOM\AEONSCEN.TMP is written
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "core/install.h"
#include "editor/chart_screen.h"
#include "editor/data.h"
#include "editor/editor_ui.h"
#include "editor/file_commands.h"
#include "editor/options.h"
#include "editor/scenario.h"
#include "editor/se_strings.h"
#include "game/frontend_host.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "ui/menu_bar.h"
#include "ui/popup_menu.h"

namespace sh::editor {

// The menu selections 0xa6d38 the item callbacks store (`menu_about` ..
// `menu_file_delete_scenario`).
enum class MenuCommand : std::int32_t {
  kNone = -1,
  kAbout = 0,
  kExit = 1,
  kPassword = 2,
  kBriefingText = 3,
  kPatrolArea = 4,
  kEnvironment = 5,
  kTorpedoes = 6,
  kEquipment = 7,
  kOptions = 8,
  kRun = 9,
  kNew = 10,
  kSaveScenario = 11,
  kLoadScenario = 12,
  kLoadHistorical = 13,
  kDeleteScenario = 14,
};

// The menu bar 0x9bee8 read from SE.EXE: '?', Files and Edit with their
// items, whose callbacks set `selection`. Files starts at Run (0x9bd74)
// only when the editor was started by the game, else at New (0x9bd88).
class EditorMenu {
 public:
  // `selection` must outlive the menu. Throws `DataError` when an item's
  // callback is not one of the editor's.
  EditorMenu(const SeExe& se, bool started_by_game, MenuCommand& selection);
  EditorMenu(const EditorMenu&) = delete;
  EditorMenu& operator=(const EditorMenu&) = delete;

  const ui::MenuBar& bar() const { return bar_; }

 private:
  std::array<ui::PopupMenu, 3> menus_;
  ui::MenuBar bar_;
};

// What the editor reads and writes.
struct EditorServices {
  const Install& install;
  const ShPaths& paths;
  // SAVE\: SBOPTION.SAV.
  const SaveDir& saves;
  // CUSTOM\: the player's directory over the install's.
  const SaveDir& custom;
};

class Editor {
 public:
  // `started_by_game`: SE spawned by SH (`/GFXADR=`): File / Run offered,
  // M8.PCX in the chart palette. `clock` gives time() for the scenario
  // seeds. The references must outlive the editor.
  Editor(const EditorServices& services, FrontendHost& host, bool started_by_game, Scenario::Clock clock);
  ~Editor();
  Editor(const Editor&) = delete;
  Editor& operator=(const Editor&) = delete;

  // Runs the editor: the title box, the data (`sb_load_data`, a new
  // scenario), the chart until a command sets the exit code. Throws
  // `DataError` when a file is missing, `QuitRequested` when the window is
  // closed.
  EditorExit run();

  // The editor's state while it runs (for the tests' scripts).
  EditorUi& ui() { return ui_; }
  Scenario& scenario() { return *scenario_; }
  ChartScreen& chart() { return *chart_; }
  FileCommands& files() { return *files_; }
  const EditorOptions& options() const { return options_; }
  // Whether the chart's loop runs (the title box is gone).
  bool running() const { return running_; }

  // `editor_menu_handle_event` (0x2491b), the chart's menu hook: Esc or a
  // right click opens the menu bar over the greyed palette and runs the
  // command chosen; Alt-Q asks QUIT TO DOS?; Alt-M (the MemCheck report)
  // does nothing. Returns true when the event was taken.
  bool menu_event(const ui::Event& event);
  // Runs menu command `command` (the dispatcher's switch).
  void run_command(MenuCommand command);

 private:
  // The title box SBINTRO.SHP (`sb_intro_open` / `sb_intro_close`, dialog
  // 0xaf384), drawn into the screen.
  void open_intro();
  void close_intro();
  ChartHooks chart_hooks();

  const EditorServices services_;
  const bool started_by_game_;
  Scenario::Clock clock_;
  SeExe se_;
  EditorUi ui_;
  std::unique_ptr<ui::Dialog> intro_;
  std::optional<EditorData> data_;
  EditorOptions options_;
  std::unique_ptr<Scenario> scenario_;
  std::unique_ptr<FileCommands> files_;
  std::unique_ptr<ChartScreen> chart_;
  MenuCommand selection_ = MenuCommand::kNone;  // 0xa6d38
  std::unique_ptr<EditorMenu> menu_;
  EditorExit exit_ = EditorExit::kExit;
  bool exiting_ = false;  // the exit code 0xa50c8 is set
  bool running_ = false;
};

}  // namespace sh::editor
