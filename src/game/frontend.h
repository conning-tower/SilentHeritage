// The game's front end (module wscreens and the start-up of module game):
// from the intro videos (`check_cd_and_play_intro`, `play_intro_logos`) to
// the menus of `front_end_loop` (0xa1232) with the MENU skin, up to the
// launch of a mission, which is handed to the mission layer as a
// `MissionRequest` where the original calls `run_mission`. The front end
// resumes in the menu the mission was started from, like the original.
//
// The screens run modally like the original's: every dialog and fade loops
// on `FrontendHost::present` / `poll_input` until it ends.
//
//   FrontEnd front_end(services, host, runner);
//   front_end.run();  // returns when the player picks EXIT TO DOS
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/pal.h"
#include "assets/shp.h"
#include "core/features.h"
#include "core/image.h"
#include "core/random.h"
#include "game/frontend_host.h"
#include "game/frontend_results.h"
#include "game/frontend_subsel.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"
#include "ui/menu_skin.h"

namespace sh {

class ExeImage;
class Install;
class Mission;
struct ResumeSnapshot;

// The mission types of `0x12657c`.
enum class MissionType : std::int32_t {
  kScenario = 1,  // a historical or custom `.SCN`
  kConvoy = 2,
  kWarship = 4,
  kPatrol = 8,
  kEnemySub = 0x10,
  kSpecial = 0x20,
};

// The scenario generator parameters 0x1265cc..0x126604 that
// `encounter_setup_mission` (0xa1960) copies from the encounter settings.
struct GeneratorParams {
  // 0x1265c0: the SUBS.DAT class picked on the SUB CLASS screen, e.g.
  // "GATO".
  std::string sub_class;
  std::int32_t day = 1;          // 0x1265cc: 1-based, drawn by the front end
  std::int32_t month = 0;        // 0x1265d0: 0-based
  std::int32_t year = 0;         // 0x1265d4: since 1930
  std::int32_t time = 0;         // 0x1265d8
  std::int32_t weather = 0;      // 0x1265dc
  std::int32_t sea = 0;          // 0x1265e0
  std::int32_t convoy_size = 0;  // 0x1265e4
  std::int32_t escort = 0;       // 0x1265e8
  std::int32_t warship_type = 0;  // 0x1265ec
  std::int32_t enemy = 0;        // 0x1265f0
  std::int32_t air_cover = 0;    // 0x1265f4
  std::int32_t fog = 0;          // 0x1265f8
  std::int32_t patrol_size = 0;  // 0x1265fc
  std::int32_t crew = 0;         // 0x126600
  std::int32_t special_mission = 0;  // 0x126604

  bool operator==(const GeneratorParams&) const = default;
};

// `encounter_setup_mission`'s month length (0xa1960, the table 0x10e46c of
// SH.EXE, by year since 1930 and 0-based month), as the original reads it,
// or as laid out with `rules.fix_obvious_errors` (not in the original).
std::int32_t encounter_month_days(const ExeImage& exe, std::int32_t month, std::int32_t year,
                                  const MissionRules& rules);

// A mission the player launched from the front end.
struct MissionRequest {
  MissionType type = MissionType::kScenario;
  // `kScenario`: the DOS path of the `.SCN` (0x126580), e.g.
  // "SCEN\HIST01.SCN" or "CUSTOM\SITDUCK.SCN".
  std::string scenario_path;
  // The encounter types: the generator's parameters, with the boat chosen
  // on the submarine selection screen.
  GeneratorParams generator;
  // The options word `0x11a85c`: mode bits 0 (single mission) and the
  // realism bits of REALISM.SAV, as `init_simulation` reloads them.
  std::uint32_t options = 0;
  // GOPTION.SAV, also reloaded by `init_simulation`.
  GameOptions game_options;
  // The game's random generator as the front end leaves it.
  WatcomRandom random;
  // The extended variant's rules of the mission (sim/context.h); a saved or
  // resumed mission keeps those of its save when it has them.
  MissionRules rules;
  // A mission restarted from `SAVE\MISS<n>.SAV` (`continue_mission_loop`,
  // the options word in saved-mission mode with the save's realism): its
  // number, -1 for a new mission.
  std::int32_t saved_mission = -1;
  // A mission resumed from the resume snapshot (game/resume.h, in
  // saved-mission mode like a continued one), which must outlive the run;
  // null for the others.
  const ResumeSnapshot* resume = nullptr;
};

// The places of the front end a resume snapshot returns to.
enum class FrontEndPlace : std::uint8_t {
  kMainMenu = 0,
  kSingleMission = 1,  // the single-mission menu (and the screens below it)
  kCareerMenu = 2,
  kCareerBase = 3,  // the BASE MENU of a career
};

struct FrontEndLocation {
  FrontEndPlace place = FrontEndPlace::kMainMenu;
  // `kCareerBase`: the career's roster slot (ROSTER<n>.SAV).
  std::int32_t roster_slot = -1;
  bool operator==(const FrontEndLocation&) const = default;
};

// One `.SCN` of a scenario list.
struct ScenarioListEntry {
  std::string file;         // e.g. "HIST01.SCN"
  std::string title;        // the list text
  std::string description;  // the text box text
};

// `historic_scan_scenarios` (0xa3254) / `custom_scan_scenarios` (0xa4039):
// the `.SCN` files of `dir` matching the DOS wildcard `pattern`, sorted by
// name, without those that fail to load. A historical scenario's title is
// the first line of its briefing (with its '\n', at most 49 characters)
// and its description the rest; a custom one's are its file name without
// extension and its whole briefing. `user_dir`, when given, holds the
// files of `dir` instead of the install alone (the CUSTOM directory: the
// user's over the install's).
std::vector<ScenarioListEntry> scan_scenarios(const Install& install, const ShPaths& paths, GameDir dir,
                                              std::string_view pattern, bool custom,
                                              const SaveDir* user_dir = nullptr);

// Returns the bytes of the scenario at DOS path `dos_path` (e.g.
// "SCEN\HIST01.SCN"): a path in CUSTOM\ is read from `custom` when given
// (the player's CUSTOM directory over the install's), any other from the
// install. Throws `DataError` when the file is missing (or deleted).
std::vector<std::uint8_t> read_scenario_file(const Install& install, const ShPaths& paths, const SaveDir* custom,
                                             std::string_view dos_path);

// The files, data and services the front end reads from.
struct FrontendServices {
  const Install& install;
  const ShPaths& paths;
  const ExeImage& exe;  // SH.EXE: the strings and tables of the screens
  const SaveDir& saves;
  // The CUSTOM directory the scenario editor writes (the player's over the
  // install's); null: the install's CUSTOM\ only.
  const SaveDir* custom = nullptr;
  // The improvements beyond the original the screens run with.
  Features features{};
  // The extended variant's settings file EXTENDED.CFG (game/extended_config.h)
  // is read and written: its improvements over `features`, the rules of new
  // games, the last boat chosen. False: the original's rules, `features`
  // alone, no file.
  bool extended_config = false;
};

// The MENU skin with the list metrics the scenario screens change.
class FrontendMenuSkin : public ui::MenuSkin {
 public:
  using ui::MenuSkin::MenuSkin;
  ui::SkinMetrics& mutable_metrics() { return metrics_; }
};

class FrontEnd {
 public:
  // Loads the GUI resources (`load_cursors`, `load_gui_fonts`,
  // `menu_load_fonts`) and seeds the generator with `seed` (`seed_random`
  // uses time()). Throws `DataError` when a file is missing.
  FrontEnd(const FrontendServices& services, FrontendHost& host, MissionRunner& runner, std::uint32_t seed);
  FrontEnd(const FrontEnd&) = delete;
  FrontEnd& operator=(const FrontEnd&) = delete;
  ~FrontEnd();

  // `/nointro`: skips the intro videos.
  void set_play_intro(bool play) { play_intro_ = play; }
  // Makes `run` start from the resume snapshot SAVE\RESUME.SAV when there
  // is one (game/resume.h), without the intro.
  void set_resume(bool resume) { resume_ = resume; }
  // The scenario editor of BUILD CUSTOM MISSION (not owned); without one
  // the button reports the editor as missing.
  void set_editor(EditorRunner* editor) { editor_ = editor; }

  // Runs `check_cd_and_play_intro` (without the CD check) and
  // `front_end_loop`: returns when the player picks EXIT TO DOS; throws
  // `QuitRequested` when the game must end at once. With `set_resume`, a
  // resume snapshot is consumed first: its mission or patrol resumed (then
  // its results and the menu it was started from), or its place of the
  // front end shown; a damaged one is reported and the game starts anew.
  void run();

  // The state the screens share, for the host and the tests.
  ui::Gui& gui() { return gui_; }
  const VgaPalette& dac() const { return dac_; }
  // Where the player is in the menus.
  const FrontEndLocation& location() const { return location_; }
  // Whether `run` is still to take the launch's resume snapshot (with
  // `set_resume`): until then, RESUME.SAV stands for the game.
  bool launch_resume_pending() const { return resume_ && !launch_taken_; }
  // The snapshot `run` took while its game is being restored, which stands
  // for the game until it runs again (`end_resuming`); null otherwise.
  const ResumeSnapshot* resuming() const { return resuming_ ? launch_snapshot_.get() : nullptr; }
  // Ends the restoring of the launch's snapshot: its game runs (a mission
  // in play), or its restoring is over.
  void end_resuming() { resuming_ = false; }
  std::uint32_t options() const { return options_; }
  const EncounterSettings& encounter_settings() const { return encounter_; }
  const GameOptions& game_options() const { return game_options_; }
  WatcomRandom& random() { return random_; }

  // `play_cinematic` (0x2c764): plays CINES\<name>.SMK with its corner at
  // (x, y) (-1 centres it) until its end or a key or button press; returns
  // false when the file is missing.
  bool play_cinematic(std::string_view name, int x = -1, int y = -1);

  // `realism_menu` (0xa2863) with `load_and_save` (-1 in the front end):
  // the REALISM dialog over the options word, loaded from REALISM.SAV first
  // and saved after OK when `load_and_save`; `cancel_disabled` greys
  // CANCEL. Returns the dialog result (-1 OK, -2 cancel). With
  // `extended_config`, the dialog also edits the extended items of
  // `realism_rules` (UNDEAFENED ESCORTS, DETECTABLE RADAR; not in the
  // original), loaded from and saved to EXTENDED.CFG with REALISM.SAV.
  int realism_menu(bool load_and_save, bool cancel_disabled);
  MissionRules& realism_rules() { return realism_rules_; }

  // `mission_briefing_screen` (0xa5a07): the MISSION BRIEFING (PATROL
  // BRIEFING in a career) dialog over the screen as it is (the loading
  // picture), with the text of `mission`, until OK. The mission runner
  // shows it once a new mission is built, before its first frame (not for
  // a saved mission or a career resumed on patrol).
  void mission_briefing_screen(const Mission& mission);

  // `show_message_box` (0x4c38d): the OK box with a title and two lines.
  void show_message_box(std::string_view title, std::string_view line1, std::string_view line2);
  // `ask_yes_no` (0x4c670): returns -1 for `yes`, -2 for `no` or Esc.
  int ask_yes_no(std::string_view title, std::string_view line1, std::string_view line2, std::string_view yes,
                 std::string_view no);

 private:
  // The career screens use the helpers below (game/career_ui.h).
  friend class CareerUi;

  // One displayed frame of the modal loops: draws the cursor, presents
  // the screen, reads the input.
  void frame();
  // Waits `ticks` clock ticks, presenting frames (the busy waits on
  // `clock()`).
  void wait_clock(std::uint32_t ticks);
  // Palette helpers of module misc (0x58bd0..0x58f28) on the DAC.
  void palette_fade_in(const VgaPalette& target, std::uint32_t delay, int step);
  void palette_fade_out(std::uint32_t delay, int step);
  void palette_grey_out(int percent, int last);
  void palette_restore_saved(int last);
  // `load_pcx(name, screen, palette)` for ART\<name>: the picture into the
  // screen and its palette into the screen palette (not the DAC).
  void load_screen_pcx(std::string_view name);
  // Returns the rules of a mission begun now: EXTENDED.CFG's with
  // `extended_config`, else the original's (not in the original).
  MissionRules mission_rules() const;
  // Returns the string of SH.EXE at `address`.
  std::string exe_string(std::uint32_t address) const;
  // Returns the `count` strings of the `char*` table at `address`.
  std::vector<std::string> exe_strings(std::uint32_t address, int count) const;
  // Adds the Alt-Q area of every front-end dialog (`exit_to_dos_prompt`).
  void add_exit_area(ui::Dialog& dialog);
  // `exit_to_dos_prompt` (0x5894f): EXIT TO DOS? with the default skin;
  // YES ends the game.
  void exit_to_dos_prompt();
  // Runs a front-end dialog with the MENU skin (`menu_load_fonts` ..
  // `menu_free_fonts`).
  int run_menu_dialog(ui::Dialog& dialog);

  // The screens (frontend.cpp).
  void play_intro_logos();
  // `front_end_loop`, from `resume` when given instead of the intro.
  void front_end_loop(const ResumeSnapshot* resume);
  // Goes back to where `resume` was taken (see `run`).
  void resume_from(const ResumeSnapshot& resume);
  // Resumes the single mission of `resume`, then shows its results.
  void resume_mission(const ResumeSnapshot& resume);
  int main_menu_screen();
  void single_mission_menu_loop();
  int single_mission_menu(bool show_background);
  // `continue_mission_loop` (0xa1486, frontend_continue.cpp): returns
  // whether the single-mission menu redraws its background.
  bool continue_mission_loop();
  // `list_saved_missions` (0xa42a4): the CONTINUE MISSION dialog; returns
  // -1 BEGIN (the save chosen in `continue_save_`), -2 CANCEL, 0x51 after
  // a DELETE, 0x17 when there is no save.
  int list_saved_missions(bool first_call, bool show_background);
  std::int32_t historic_mission_loop(bool custom);
  int list_scenarios(bool custom, bool first_call);
  // `run_scenario_editor` (0xa22d2, BUILD CUSTOM MISSION): the editor,
  // then for File / Run the realism menu, the mission of
  // CUSTOM\AEONSCEN.TMP, its results, and the file deleted.
  void run_scenario_editor();
  // `show_status_popup` (0x4c4aa) centred at `y` / `close_status_popup`
  // (0x4c518): the one-line box of "LOADING SCENARIO EDITOR...".
  void show_status_popup(int y, std::string_view text);
  void close_status_popup();
  void encounter_mission_loop(int code);
  int encounter_menu(int code);
  bool encounter_setup_mission(GeneratorParams& params);
  std::int32_t run_mission(const MissionRequest& request);
  // `run_mission`'s parts before and after the mission: the random loading
  // picture with the wait cursor; the cinematic, newspapers or tow of the
  // boat's fate for `outcome` (with its end code), then
  // `mission_compute_results` with the options word `options` and the
  // return to port. Returns the results.
  void show_mission_loading();
  MissionResults mission_end_screens(const MissionOutcome& outcome, std::uint32_t options);
  void report_not_implemented(std::string_view what);
  // `career_menu_loop` (0x1b4bc, career_flow.cpp); with `resume`, its
  // patrol or career base first.
  void career_menu_loop(const ResumeSnapshot* resume = nullptr);
  // `show_credits` (0x3579c, frontend_credits.cpp).
  void show_credits();
  // The main menu's EXTENDED OPTIONS page (frontend_options.cpp, not in the
  // original): the improvements of `kFeatureOptions`, kept in EXTENDED.CFG.
  void extended_options_screen();
  // `interviews_screen` (0xa8bff, frontend_interviews.cpp): the INTERVIEWS
  // pages until MAIN MENU.
  void interviews_screen();
  // `interviews_run_dialog` (0xa8a72): runs the INTERVIEWS dialog of the
  // current page, the screen faded out first when `first`.
  int interviews_run_dialog(bool first);
  // `play_interview_video` (0xa8ef1): plays BUD\<video>.SMK at (300, 121)
  // with the STOP / RESTART controls; returns false when it is missing.
  bool play_interview_video(int video);
  // `subtour_screen` (0x6fba1, frontend_subtour.cpp).
  void subtour_screen();
  friend class SubTour;
  // `subsel_screen` (0x72cfb, frontend_subsel.cpp): returns false on
  // CANCEL, else true with the chosen SUBS.DAT class name.
  bool subsel_screen(std::string& class_name, const SubClassFilter& filter);
  // The screens after a mission (frontend_results_screen.cpp).
  void wait_for_press();
  void show_fate_newspaper(int kind, const MissionOutcome& outcome);  // 0xa7246, kind 1..3
  void show_towed_out_of_fuel(const MissionOutcome& outcome);         // 0xa754d
  void show_port_return(const MissionOutcome& outcome);               // 0xa78b6
  void show_port_departure(const std::string& profile_sprite);        // 0xa767d
  // The port scene of both: the boat's profile (ART\<profile_sprite>)
  // moving from x 600 on PORTBACK.PCX, mirrored and leftwards when
  // `returning`.
  void port_scene(const std::string& profile_sprite, bool returning);
  void mission_results_screen();                                     // 0xa4c04
  void best_missions_screen();                                        // 0xa69a8
  void best_missions_record_score();                                  // 0xa6da9
  std::string high_score_enter_name();                                // 0xa71a9
  // Shows the results of the mission that ended with `end` unless it was
  // aborted, then records its score (the mission loops after
  // `run_mission`).
  void show_mission_results(std::int32_t end);

  const Install& install_;
  const ShPaths& paths_;
  const ExeImage& exe_;
  const SaveDir& saves_;
  FrontendHost& host_;
  MissionRunner& runner_;
  const SaveDir* custom_;
  const bool extended_config_;
  // The improvements, as EXTENDED OPTIONS sets them.
  Features features_;
  EditorRunner* editor_ = nullptr;
  std::unique_ptr<ui::Dialog> status_popup_;  // 0x120670

  Image8 screen_;
  ui::GuiResources gui_res_;
  ui::MenuResources menu_res_;
  Shp cursors_;
  ui::DefaultSkin default_skin_;
  FrontendMenuSkin menu_skin_;
  ui::Gui gui_;

  VgaPalette dac_{};      // the displayed palette (the VGA DAC)
  VgaPalette palette_{};  // 0x11a274: the screen palette
  VgaPalette grey_saved_{};  // 0x1244e8: palette_grey_out's saved colours
  WatcomRandom random_;
  bool play_intro_ = true;
  bool resume_ = false;
  bool launch_taken_ = false;
  // The launch's resume snapshot (for the life of `run`), and whether it
  // is being restored.
  std::unique_ptr<ResumeSnapshot> launch_snapshot_;
  bool resuming_ = false;
  FrontEndLocation location_;

  GameOptions game_options_;
  std::uint32_t options_ = 0;  // 0x11a85c
  // The rules the REALISM dialog edits with the options word: its extended
  // items (not in the original).
  MissionRules realism_rules_;
  EncounterSettings encounter_;
  // The scenario list position kept across the REALISM dialog (0x18a654
  // selection, 0x18a658 top row).
  int list_selection_ = 0;
  int list_top_ = 0;
  std::string scenario_path_;      // 0x126580: the chosen `.SCN`
  int last_custom_picture_ = -1;   // 0x1108ec
  bool exit_confirmed_ = false;  // 0x12434c
  // 0x10e22c: the SUB CLASS screen shows the statistics (else the
  // description); kept between visits.
  bool subsel_statistics_ = true;
  // The class last chosen on the SUB CLASS screen, for `Features::last_boat`
  // (not in the original), kept in EXTENDED.CFG with `extended_config`.
  std::string subsel_last_class_;
  // The CONTINUE MISSION list position (0x18a65c selection, 0x18a660 top
  // row) and the save chosen (its header, 0x122238).
  int saves_selection_ = 0;
  int saves_top_ = 0;
  std::int32_t continue_save_ = -1;
  // The results record 0x187254 of the last mission.
  MissionResults results_;
  // The INTERVIEWS page (0x18a66c), kept between visits.
  int interview_page_ = 0;
  // The SUB TOUR view (0x10e1f4: the internal view, B-GRND-2.PCX) and
  // labels (0x10e1f8), kept between visits.
  bool subtour_internal_ = true;
  bool subtour_text_ = true;
};

}  // namespace sh
