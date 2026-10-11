// The front-end flow of module wscreens: `front_end_loop` (0xa1232), the
// main and single-mission menus, the scenario lists, the encounter
// dialogs and the launch of a mission (`run_mission`, 0xa1b10). The dialog
// layouts are those of the static initializer `wscreens_init_dialogs`
// (0x98410); their texts are read from SH.EXE.

#include "game/frontend.h"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <optional>
#include <utility>

#include "assets/exe_image.h"
#include "assets/format_header.h"
#include "core/byte_reader.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/options_realism.h"
#include "game/resume.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "ui/controls.h"

namespace sh {

namespace {

// SH.EXE addresses of the screens' data.
namespace addr {
constexpr std::uint32_t kLogo = 0x110910;      // "LOGO444"
constexpr std::uint32_t kIntro = 0x110900;     // "SHIN"
constexpr std::uint32_t kCopyright = 0x110908;  // "COPYR"
constexpr std::uint32_t kMainPcx = 0x110948;   // "M3.PCX"
constexpr std::uint32_t kMenuPcx = 0x110940;   // "M8.PCX"
constexpr std::uint32_t kLoadingPcx = 0x110930;  // char*[4]: M2, M4, M11, M12
constexpr std::uint32_t kCustomPics = 0x1109c8;  // "MENUPIC3.SHP"
constexpr std::uint32_t kMonthDays = 0x10e46c;  // u8[16][12] by year since 1930
constexpr std::uint32_t kHistPattern = 0x104e34;  // "HIST???.SCN"
constexpr std::uint32_t kCustomPattern = 0x104e40;  // "*.SCN"
// BUILD CUSTOM MISSION (`run_scenario_editor`).
constexpr std::uint32_t kLoadingEditor = 0xfc17b;  // "LOADING SCENARIO EDITOR..."
constexpr std::uint32_t kOneMoment = 0xfc216;      // "ONE MOMENT PLEASE..."
constexpr std::uint32_t kRunFile = 0x104e48;       // "AEONSCEN.TMP"
// Main menu (dialog 0x187370).
constexpr std::uint32_t kMainTitle = 0xfbcd8;
// Single mission (dialog 0x1875fc).
constexpr std::uint32_t kSingleTitle = 0xfbd25;
// The arrows of the text boxes.
constexpr std::uint32_t kArrowUp = 0xfbedb;
constexpr std::uint32_t kArrowDown = 0xfbedd;
// The scenario lists (dialogs 0x187ec8 and 0x1881ec).
constexpr std::uint32_t kHistoricTitle = 0xfbeb5;
constexpr std::uint32_t kCustomTitle = 0xfbedf;
constexpr std::uint32_t kHistoricMissing = 0xfc2e1;
constexpr std::uint32_t kCustomMissing = 0xfc480;
constexpr std::uint32_t kNotFound = 0xfc2d6;        // " not found"
constexpr std::uint32_t kNotFoundDot = 0xfc474;     // " not found."
// The end-of-mission cinematics (`run_mission`).
constexpr std::uint32_t kImpCine = 0x110918;   // "IMP"
constexpr std::uint32_t kBotCine = 0x11091c;   // "BOT"
constexpr std::uint32_t kSurfCine = 0x110920;  // "SURF"
constexpr std::uint32_t kSrecCine = 0x110928;  // "SREC"
}  // namespace addr

// The result codes of the menus.
namespace code {
constexpr int kExitToDos = 10;
constexpr int kSingleMission = 11;
constexpr int kCareer = 13;
constexpr int kInterviews = 14;
constexpr int kSubTour = 15;
constexpr int kCredits = 16;
constexpr int kHistoric = 0x5a;
constexpr int kConvoy = 0x5b;
constexpr int kWarship = 0x5c;
constexpr int kEnemySub = 0x5d;
constexpr int kPatrol = 0x5e;
constexpr int kSpecial = 0x5f;
constexpr int kBuildCustom = 0x60;
constexpr int kLoadCustom = 0x61;
constexpr int kBestMissions = 0x62;
constexpr int kContinue = 0x63;
constexpr int kMainMenu = 100;
constexpr int kRealism = 0x50;
constexpr int kNoScenario = 0x17;  // the list was empty (message shown)
}  // namespace code

// The end codes `run_mission` reacts to (`0x11a890`).
constexpr std::int32_t kEndAbort = 1;
constexpr std::int32_t kEndQuitToDos = 3;

// The y of the status popups of `run_scenario_editor`.
constexpr int kStatusPopupY = 0x1a4;

constexpr ui::DialogFlag kMenuFlags = static_cast<ui::DialogFlag>(ui::kDialogTitled | ui::kDialogFramed);

// Returns a button callback returning `result`.
std::function<int(int)> returns(int result) {
  return [result](int) { return result; };
}

// Returns true when the DOS file name `name` matches `pattern` (`*` and
// `?`, which also matches nothing at the end of the name or extension),
// case-insensitively, as `opendir` does.
bool dos_name_matches(std::string_view name, std::string_view pattern) {
  const auto split = [](std::string_view s) {
    const std::size_t dot = s.rfind('.');
    return dot == std::string_view::npos ? std::pair{s, std::string_view{}}
                                         : std::pair{s.substr(0, dot), s.substr(dot + 1)};
  };
  const auto part_matches = [](std::string_view s, std::string_view p) {
    std::size_t i = 0;
    for (const char c : p) {
      if (c == '*') {
        return true;
      }
      if (c == '?') {
        i += i < s.size() ? 1 : 0;
        continue;
      }
      if (i >= s.size() || std::toupper(static_cast<unsigned char>(s[i])) != std::toupper(static_cast<unsigned char>(c))) {
        return false;
      }
      ++i;
    }
    return i == s.size();
  };
  const auto [base, ext] = split(name);
  const auto [pbase, pext] = split(pattern);
  return part_matches(base, pbase) && part_matches(ext, pext);
}

}  // namespace

std::vector<ScenarioListEntry> scan_scenarios(const Install& install, const ShPaths& paths, GameDir dir,
                                              std::string_view pattern, bool custom, const SaveDir* user_dir) {
  std::vector<std::string> files;
  if (user_dir != nullptr) {
    for (std::string& name : user_dir->list(".SCN")) {
      if (dos_name_matches(name, pattern)) {
        files.push_back(std::move(name));
      }
    }
  } else {
    for (const std::string& path : install.list(paths.dir_name(dir), ".SCN")) {
      std::string name = path.substr(path.rfind('/') + 1);
      if (dos_name_matches(name, pattern)) {
        files.push_back(std::move(name));
      }
    }
  }
  // scenario_list_sort (0xa34d2) compares with stricmp; the listed names
  // are upper case.
  std::ranges::sort(files);
  std::vector<ScenarioListEntry> entries;
  for (std::string& file : files) {
    // scen_load_ascn (0x676b5) without the units: the header and the
    // 1000-byte briefing; a file it cannot read is left out.
    std::string briefing;
    try {
      // A file deleted from the player's directory reads empty and fails.
      const std::vector<std::uint8_t> data =
          user_dir != nullptr ? user_dir->read(file).value_or(std::vector<std::uint8_t>{})
                              : install.read(paths.path(dir, file));
      ByteReader r(data, file);
      read_format_header(r, "ASCN", 2);
      briefing = r.fixed_string(1000);
    } catch (const DataError&) {
      continue;
    }
    ScenarioListEntry e;
    if (custom) {
      e.title = file.substr(0, std::min(file.find('.'), std::size_t{0x31}));
      e.description = briefing;
    } else {
      // The first line (with its '\n', at most 49 characters), then the
      // rest.
      const std::size_t nl = briefing.find('\n');
      const std::size_t rest = nl == std::string::npos ? 0 : nl + 1;
      e.title = briefing.substr(0, std::min(rest, std::size_t{0x31}));
      e.description = briefing.substr(rest);
    }
    e.file = std::move(file);
    entries.push_back(std::move(e));
  }
  return entries;
}

std::vector<std::uint8_t> read_scenario_file(const Install& install, const ShPaths& paths, const SaveDir* custom,
                                             std::string_view dos_path) {
  const std::string& custom_dir = paths.dir(GameDir::kCustom);
  if (custom != nullptr && dos_path.size() > custom_dir.size() && dos_path.starts_with(custom_dir)) {
    std::optional<std::vector<std::uint8_t>> data = custom->read(dos_path.substr(custom_dir.size()));
    if (!data || data->empty()) {
      throw DataError(std::format("{}: no such scenario", dos_path));
    }
    return std::move(*data);
  }
  return install.read(dos_path);
}

FrontEnd::FrontEnd(const FrontendServices& services, FrontendHost& host, MissionRunner& runner, std::uint32_t seed)
    : install_(services.install),
      paths_(services.paths),
      exe_(services.exe),
      saves_(services.saves),
      host_(host),
      runner_(runner),
      custom_(services.custom),
      features_(services.features),
      screen_(kScreenWidth, kScreenHeight),
      gui_res_(ui::GuiResources::load(install_)),
      menu_res_(ui::MenuResources::load(install_)),
      cursors_(Shp::load(install_, paths_.path(GameDir::kArt, "CURSORS.SHP"))),
      default_skin_(gui_res_),
      menu_skin_(gui_res_, menu_res_),
      gui_(screen_, [this] { return host_.ticks(); }, default_skin_),
      random_(seed) {
  menu_skin_.click_sound = [this] { host_.play_sfx(1, 5, -1); };
  gui_.set_pump([this] { frame(); });
  // load_cursors (0x10a12): the pointer, shown.
  gui_.set_dialog_cursor(&cursors_, static_cast<std::size_t>(ui::CursorId::kPointer));
  gui_.mouse().set_cursor(cursors_, ui::CursorId::kPointer);
  gui_.mouse().show();
}

FrontEnd::~FrontEnd() = default;

void FrontEnd::run() {
  // check_cd_and_play_intro (0x10be1) only checks the CD (BUD\1.SMK of the
  // CD's size); the intro itself plays in front_end_loop.
  if (resume_ && !launch_taken_) {
    try {
      if (std::optional<ResumeSnapshot> taken = take_resume_snapshot(saves_)) {
        launch_snapshot_ = std::make_unique<ResumeSnapshot>(std::move(*taken));
        resuming_ = true;
      }
    } catch (const DataError& e) {
      host_.report(std::format("{} (starting anew)", e.what()));
    }
    launch_taken_ = true;
  }
  front_end_loop(launch_snapshot_.get());
}

void FrontEnd::resume_from(const ResumeSnapshot& resume) {
  random_.srand(resume.random);
  switch (resume.kind) {
    case ResumeKind::kMission:
      // The runner ends the resuming once the mission is restored.
      resume_mission(resume);
      single_mission_menu_loop();
      return;
    case ResumeKind::kPatrol:
      career_menu_loop(&resume);
      return;
    case ResumeKind::kFrontEnd:
      break;
  }
  location_ = resume.location;
  end_resuming();
  switch (resume.location.place) {
    case FrontEndPlace::kMainMenu:
      break;
    case FrontEndPlace::kSingleMission:
      single_mission_menu_loop();
      break;
    case FrontEndPlace::kCareerMenu:
      career_menu_loop();
      break;
    case FrontEndPlace::kCareerBase:
      career_menu_loop(&resume);
      break;
  }
}

void FrontEnd::resume_mission(const ResumeSnapshot& resume) {
  // As CONTINUE MISSION's BEGIN: the save's realism bits in the options
  // word, the mission in saved-mission mode, then its results.
  location_ = {FrontEndPlace::kSingleMission, -1};
  options_ = (options_ & ~0xffffu) | (resume.mission.value().header.options() & 0xffffu);
  MissionRequest request;
  request.type = MissionType::kScenario;
  request.resume = &resume;
  const std::int32_t end = run_mission(request);
  if (end == kEndQuitToDos) {
    // fatal_error(0, 0): the game ends.
    throw QuitRequested();
  }
  show_mission_results(end);
}

void FrontEnd::report_not_implemented(std::string_view what) {
  host_.report(std::format("{} is not implemented yet", what));
}

void FrontEnd::play_intro_logos() {
  // play_intro_logos (0xa1289), skipped by /nointro (0x11a7b8).
  if (!play_intro_) {
    return;
  }
  play_cinematic(exe_string(addr::kLogo), 0, 0);
  play_cinematic(exe_string(addr::kIntro), -1, -1);
  play_cinematic(exe_string(addr::kCopyright), 0, 0);
}

void FrontEnd::front_end_loop(const ResumeSnapshot* resume) {
  // front_end_loop (0xa1232)
  game_options_ = GameOptions::load(saves_);
  host_.apply_sound_options(game_options_);
  if (resume != nullptr) {
    resume_from(*resume);
  } else {
    play_intro_logos();
  }
  while (true) {
    location_ = {FrontEndPlace::kMainMenu, -1};
    switch (main_menu_screen()) {
      case code::kExitToDos:
        return;
      case code::kSingleMission:
        single_mission_menu_loop();
        break;
      case code::kCareer:
        career_menu_loop();
        break;
      case code::kInterviews:
        interviews_screen();
        break;
      case code::kSubTour:
        subtour_screen();
        break;
      case code::kCredits:
        show_credits();
        break;
      default:
        break;
    }
  }
}

int FrontEnd::main_menu_screen() {
  // main_menu_screen (0xa25f3), dialog 0x187370.
  load_screen_pcx(exe_string(addr::kMainPcx));
  gui_.set_skin(menu_skin_);
  palette_fade_out(0, 0x100);
  palette_fade_in(palette_, 0, 10);
  wait_clock(9);
  ui::Dialog d(gui_, 20, 43, 270, 400, kMenuFlags);
  // main_menu_dialog_draw (0xa2670)
  d.set_idle([this](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
    }
  });
  d.emplace<ui::TitleBar>(exe_string(addr::kMainTitle));
  constexpr std::array<std::pair<std::uint32_t, int>, 6> kButtons{{
      {0xfbce2, code::kSingleMission},
      {0xfbcf1, code::kCareer},
      {0xfbcfd, code::kInterviews},
      {0xfbd08, code::kSubTour},
      {0xfbd11, code::kCredits},
      {0xfbd19, code::kExitToDos},
  }};
  int y = 130;
  for (const auto& [label, result] : kButtons) {
    auto& button = d.emplace<ui::Button>(35, y, 198, exe_string(label), returns(result));
    if (result == code::kExitToDos) {
      // Back leaves the game at once, like this button (the EXIT TO DOS?
      // box is Alt-Q's).
      d.set_back(button);
    }
    y += 40;
  }
  add_exit_area(d);
  return run_menu_dialog(d);
}

void FrontEnd::single_mission_menu_loop() {
  // single_mission_menu_loop (0xa12c9): the menu is shown again after each
  // screen; `show_background` is false only after an empty scenario list
  // (its message box leaves the menu on screen).
  bool show_background = true;
  while (true) {
    location_ = {FrontEndPlace::kSingleMission, -1};
    const int result = single_mission_menu(show_background);
    show_background = true;
    switch (result) {
      case code::kHistoric:
        show_background = historic_mission_loop(false) != 0;
        break;
      case code::kConvoy:
      case code::kWarship:
      case code::kEnemySub:
      case code::kPatrol:
      case code::kSpecial:
        encounter_mission_loop(result);
        break;
      case code::kBuildCustom:
        run_scenario_editor();
        break;
      case code::kLoadCustom:
        show_background = historic_mission_loop(true) != 0;
        break;
      case code::kBestMissions:
        best_missions_screen();
        break;
      case code::kContinue:
        show_background = continue_mission_loop();
        break;
      case code::kMainMenu:
        return;
      default:
        break;
    }
  }
}

int FrontEnd::single_mission_menu(bool show_background) {
  // single_mission_menu (0xa26ac), dialog 0x1875fc.
  if (show_background) {
    load_screen_pcx(exe_string(addr::kMenuPcx));
  }
  gui_.set_skin(menu_skin_);
  if (show_background) {
    palette_fade_out(0, 0x100);
    palette_fade_in(palette_, 0, 10);
    wait_clock(9);
  }
  ui::Dialog d(gui_, 69, 59, 502, 361, kMenuFlags);
  // single_mission_dialog_draw (0xa280f)
  d.set_idle([this](const Viewport&, int first) {
    if (first != 0) {
      palette_fade_in(palette_, 0, 10);
    }
  });
  d.emplace<ui::TitleBar>(exe_string(addr::kSingleTitle));
  d.emplace<ui::Button>(38, 125, 198, exe_string(0xfbd34), returns(code::kHistoric));
  d.emplace<ui::Button>(261, 125, 198, exe_string(0xfbd45), returns(code::kConvoy));
  d.emplace<ui::Button>(38, 165, 198, exe_string(0xfbd56), returns(code::kWarship));
  d.emplace<ui::Button>(261, 165, 198, exe_string(0xfbd68), returns(code::kPatrol));
  d.emplace<ui::Button>(261, 299, 198, exe_string(0xfbd79), returns(code::kBestMissions));
  d.set_back(d.emplace<ui::Button>(38, 299, 198, exe_string(0xfbd87), returns(code::kMainMenu)));
  // The upper box grows by one row of buttons for SPECIAL / CONTINUE and
  // one more when the scenario editor is installed (0x11a7d0).
  const bool editor = install_.exists("SE.EXE");
  d.emplace<ui::GroupBox>(25, 116, 472, 209 + 40 + (editor ? 40 : 0));
  d.emplace<ui::GroupBox>(25, 290, 472, 342);
  add_exit_area(d);
  d.emplace<ui::Button>(38, 205, 198, exe_string(0xfbd91), returns(code::kSpecial));
  d.emplace<ui::Button>(261, 205, 198, exe_string(0xfbda1), returns(code::kContinue));
  if (editor) {
    d.emplace<ui::Button>(38, 245, 198, exe_string(0xfbdb2), returns(code::kBuildCustom));
    d.emplace<ui::Button>(261, 245, 198, exe_string(0xfbdc7), returns(code::kLoadCustom));
  }
  return run_menu_dialog(d);
}

std::int32_t FrontEnd::historic_mission_loop(bool custom) {
  // historic_mission_loop (0xa1338) / custom_mission_loop (0xa13df):
  // returns -1 to show the single mission menu again with its background,
  // 0 after the "no scenario" message.
  bool first = true;
  while (true) {
    const int result = list_scenarios(custom, first);
    first = false;
    if (result == ui::kResultCancel) {
      return -1;
    }
    if (result == ui::kResultAccept) {
      MissionRequest request;
      request.type = MissionType::kScenario;
      request.scenario_path = scenario_path_;
      const std::int32_t end = run_mission(request);
      if (end == kEndQuitToDos) {
        // fatal_error(0, 0): the game ends.
        throw QuitRequested();
      }
      show_mission_results(end);
      return -1;
    }
    if (result == code::kNoScenario) {
      return 0;
    }
    if (result == code::kRealism) {
      realism_menu(true, false);
    }
  }
}

int FrontEnd::list_scenarios(bool custom, bool first_call) {
  // list_historical_scenarios (0xa2f1a) / list_custom_scenarios (0xa3cb1)
  // with historic_scan_scenarios (0xa3254) / custom_scan_scenarios
  // (0xa4039): the files sorted by name (`scenario_list_sort`), those that
  // fail to load left out.
  // The directory buffers 0x104d34 / 0x104df4 that load_shpath_ini fills.
  const std::string dir = paths_.dir(custom ? GameDir::kCustom : GameDir::kScen);
  const std::string pattern = exe_string(custom ? addr::kCustomPattern : addr::kHistPattern);
  const std::vector<ScenarioListEntry> entries =
      scan_scenarios(install_, paths_, custom ? GameDir::kCustom : GameDir::kScen, pattern, custom,
                     custom ? custom_ : nullptr);
  if (first_call) {
    list_selection_ = 0;
    list_top_ = 0;
  }

  if (entries.empty()) {
    palette_fade_in(palette_, 0, 10);
    const std::string title = exe_string(custom ? addr::kCustomTitle : addr::kHistoricTitle);
    const std::string missing = dir + pattern + exe_string(custom ? addr::kNotFoundDot : addr::kNotFound);
    show_message_box(title, exe_string(custom ? addr::kCustomMissing : addr::kHistoricMissing), missing);
    return code::kNoScenario;
  }

  load_screen_pcx(exe_string(addr::kMenuPcx));
  gui_.set_skin(menu_skin_);
  // The list shows 7 rows here.
  ui::SkinMetrics& metrics = menu_skin_.mutable_metrics();
  const ui::SkinMetrics saved = metrics;
  metrics.list_rows = 7;
  metrics.list_height = 0x72;
  metrics.list_track = 0x32;
  palette_fade_out(0, 0x100);

  ui::Dialog d(gui_, 69, 19, 502, 445, kMenuFlags);
  if (custom) {
    // custom_mission_draw_picture (0xa3e9b): a random MENUPIC3.SHP frame,
    // another than last time.
    d.emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
      int pick = 0;
      do {
        pick = random_.random_int(13);
      } while (pick == last_custom_picture_);
      last_custom_picture_ = pick;
      const Shp pics = Shp::load(install_, paths_.path(GameDir::kArt, exe_string(addr::kCustomPics)));
      const SpriteFrame& f = pics.frame(static_cast<std::size_t>(pick));
      sprite_apply_palette(f, palette_);
      draw_sprite_clipped(gfx, f, 0x10f, 0x82);
    });
  } else {
    d.emplace<ui::OwnerDraw>([](const Viewport&, ui::OwnerDraw&) {});
  }
  d.emplace<ui::TitleBar>(exe_string(custom ? addr::kCustomTitle : addr::kHistoricTitle));
  auto& list = d.emplace<ui::List>(
      41, 134, custom ? 186 : 375, [&entries] { return static_cast<int>(entries.size()); },
      [&entries](int row) { return entries[static_cast<std::size_t>(row)].title; });
  d.set_back(d.emplace<ui::Button>(35, 395, 121, exe_string(custom ? 0xfbef3 : 0xfbec6), ui::return_cancel));
  d.emplace<ui::Button>(185, 395, 121, exe_string(custom ? 0xfbefa : 0xfbecd), returns(code::kRealism));
  d.emplace<ui::Button>(335, 395, 121, exe_string(custom ? 0xfbf02 : 0xfbed5), ui::return_accept);
  auto& text = d.emplace<ui::TextBox>(40, 266, 357, 100, 10, 1000, entries.front().description);
  text.read_only = true;
  auto& up = d.emplace<ui::Button>(422, 262, 33, exe_string(addr::kArrowUp), nullptr, 0, 20);
  auto& down = d.emplace<ui::Button>(422, 348, 33, exe_string(addr::kArrowDown), nullptr, 0, 20);
  d.emplace<ui::GroupBox>(422, 299, 454, 346);
  add_exit_area(d);
  if (custom) {
    d.emplace<ui::GroupBox>(269, 128, 454, 251);
  }

  // historic_mission_scroll_up / _down (0xa31e9, 0xa3202).
  bool refresh_arrows = true;  // 0x187244
  up.on_click = [&](int) {
    text.scroll_up(d);
    text.dirty = true;
    refresh_arrows = true;
    return 0;
  };
  down.on_click = [&](int) {
    text.scroll_down(d);
    text.dirty = true;
    refresh_arrows = true;
    return 0;
  };
  // historic_mission_on_select (0xa3223)
  list.on_select = [&](int row) {
    text.top = 0;
    text.dirty = true;
    refresh_arrows = true;
    text.text = entries[static_cast<std::size_t>(row)].description;
  };
  // historic_mission_dialog_draw (0xa3110): the list position comes back
  // on the first call, the picture fades in on the second; the arrows are
  // greyed when the text cannot scroll that way.
  bool fade_pending = true;  // 0x187240
  d.set_idle([&](const Viewport&, int first) {
    if (first == 0) {
      if (fade_pending) {
        palette_fade_in(palette_, 0, 10);
        fade_pending = false;
      }
    } else {
      list.dirty = true;
      list.selection = list_selection_;
      list.top = list_top_;
      refresh_arrows = true;
    }
    if (!text.dirty && refresh_arrows) {
      const bool can_up = text.scroll_up(d);
      if (can_up) {
        text.scroll_down(d);
      }
      const bool can_down = text.scroll_down(d);
      if (can_down) {
        text.scroll_up(d);
      }
      up.disabled = !can_up;
      down.disabled = !can_down;
      up.dirty = true;
      down.dirty = true;
      refresh_arrows = false;
    }
  });
  const int result = run_menu_dialog(d);
  list_selection_ = list.selection;
  list_top_ = list.top;
  metrics = saved;
  if (result != ui::kResultCancel && list.selection >= 0) {
    scenario_path_ = dir + entries[static_cast<std::size_t>(list.selection)].file;
  }
  return result;
}

void FrontEnd::encounter_mission_loop(int menu_code) {
  // encounter_mission_loop (0xa180d)
  bool reload = true;
  while (true) {
    if (reload) {
      encounter_ = EncounterSettings::load(saves_);
    }
    reload = true;
    const int result = encounter_menu(menu_code);
    if (menu_code == code::kSpecial && encounter_.special_mission > 1) {
      menu_code = code::kEnemySub;
    }
    if (result == ui::kResultCancel) {
      return;
    }
    if (result >= 0) {
      if (result == code::kRealism) {
        realism_menu(true, false);
        reload = false;
      }
      continue;
    }
    if (result != ui::kResultAccept) {
      continue;
    }
    encounter_.save(saves_);
    MissionRequest request;
    if (!encounter_setup_mission(request.generator)) {
      continue;
    }
    request.type = menu_code == code::kConvoy     ? MissionType::kConvoy
                   : menu_code == code::kWarship  ? MissionType::kWarship
                   : menu_code == code::kEnemySub ? MissionType::kEnemySub
                   : menu_code == code::kPatrol   ? MissionType::kPatrol
                                                  : MissionType::kSpecial;
    const std::int32_t end = run_mission(request);
    if (end == kEndQuitToDos) {
      throw QuitRequested();
    }
    show_mission_results(end);
    return;
  }
}

bool FrontEnd::encounter_setup_mission(GeneratorParams& p) {
  // encounter_setup_mission (0xa1960)
  const EncounterSettings& s = encounter_;
  p = GeneratorParams{.sub_class = {},
                      .day = 1,
                      .month = s.month,
                      .year = s.year,
                      .time = s.time,
                      .weather = s.weather,
                      .sea = s.sea,
                      .convoy_size = s.convoy_size,
                      .escort = s.escort,
                      .warship_type = s.warship_type,
                      .enemy = s.enemy,
                      .air_cover = s.air_cover,
                      .fog = s.fog,
                      .patrol_size = s.patrol_size,
                      .crew = s.crew,
                      .special_mission = s.special_mission};
  options_ &= ~option::kModeMask;
  // The day is drawn from the month's length, read from the table by
  // [month][year] where it is laid out by [year][month] (an original bug).
  const int days = exe_.u8(addr::kMonthDays + static_cast<std::uint32_t>(p.month * 12 + p.year));
  p.day = random_.random_int(days) + 1;
  if (p.year == WarWindow::kStartYear) {
    p.month = std::max(p.month, WarWindow::kStartMonth);
    if (p.month == WarWindow::kStartMonth && p.day < WarWindow::kStartDay + 6) {
      p.day = WarWindow::kStartDay + 6;
    }
  }
  if (p.year == WarWindow::kEndYear) {
    p.month = std::min(p.month, WarWindow::kEndMonth);
    if (p.month == WarWindow::kEndMonth && p.day > WarWindow::kEndDay - 3) {
      p.day = WarWindow::kEndDay - 3;
    }
  }
  // The boat among the classes in service (filter 0x1279a0); CANCEL goes
  // back to the encounter dialog.
  const std::int32_t year = p.year;
  const std::int32_t month = p.month;
  return subsel_screen(p.sub_class,
                       [year, month](const SubClass& sub) { return sub_class_in_service(sub, year, month); });
}

void FrontEnd::show_status_popup(int y, std::string_view text) {
  // show_status_popup (0x4c4aa), dialog 0x120670: the text in font slot 2
  // (FONT1), centred in a box 40 pixels wider, left open.
  const int text_w = gui_res_.font1.text_width(text);
  const int w = text_w + 0x28;
  status_popup_ = std::make_unique<ui::Dialog>(gui_, (kScreenWidth - w) / 2, y, w, 0x2d,
                                               ui::kDialogTitled | ui::kDialogFramed);
  status_popup_->emplace<ui::TextField>(0x14, 9, text_w + 0x14, 0x1b, [t = std::string(text)] { return t; },
                                        ui::FontSlot::kFont1, TextAlign::kCentre);
  status_popup_->open();
  status_popup_->process();
}

void FrontEnd::close_status_popup() {
  if (status_popup_) {
    status_popup_->close();
    status_popup_.reset();
  }
}

void FrontEnd::run_scenario_editor() {
  // run_scenario_editor (0xa22d2). The memory check and SE.EXE's command
  // line (/GFXADR= /SFXBUF= /BASMEM=) have no equivalent: the editor runs
  // in this process, on this host.
  if (editor_ == nullptr) {
    report_not_implemented("The scenario editor (SE.EXE)");
    return;
  }
  palette_fade_out(0, 0x100);
  load_screen_pcx(exe_string(addr::kMenuPcx));
  frame();
  palette_fade_in(palette_, 0, 10);
  show_status_popup(kStatusPopupY, exe_string(addr::kLoadingEditor));
  frame();
  const EditorExit exit = editor_->run_editor();
  palette_fade_out(0, 0x100);
  close_status_popup();
  if (exit == EditorExit::kQuitToDos) {
    // fatal_error(0, 0): the game ends.
    throw QuitRequested();
  }
  load_screen_pcx(exe_string(addr::kMenuPcx));
  frame();
  palette_fade_in(palette_, 0, 10);
  show_status_popup(kStatusPopupY, exe_string(addr::kOneMoment));
  frame();
  close_status_popup();
  if (exit != EditorExit::kRun) {
    return;
  }
  // File / Run: the scenario the editor wrote, with the realism of the
  // REALISM dialog (CANCEL greyed).
  realism_menu(true, true);
  const std::string file = exe_string(addr::kRunFile);
  MissionRequest request;
  request.type = MissionType::kScenario;
  request.scenario_path = paths_.path(GameDir::kCustom, file);
  std::int32_t end = 0;
  try {
    end = run_mission(request);
  } catch (const DataError& e) {
    // A file the reader rejects (SE could leave a truncated one after a
    // write error; the editor no longer does): SH 1995 ends with a fatal
    // error (`scen_report_load_error` 0x53c2a: 0x37 read error, 0x20 other)
    // back to DOS. Deviation: the error is shown and the menu comes back.
    show_message_box(exe_string(addr::kCustomTitle), e.what(), request.scenario_path);
    if (custom_ != nullptr) {
      custom_->remove(file);
    }
    return;
  }
  if (end == kEndQuitToDos) {
    // fatal_error(0, 0) before the file is deleted: it stays.
    throw QuitRequested();
  }
  show_mission_results(end);
  if (custom_ != nullptr) {
    custom_->remove(file);
  }
}

void FrontEnd::show_mission_results(std::int32_t end) {
  // The mission loops: the results and the best missions unless the
  // mission was aborted.
  if (end != kEndAbort) {
    mission_results_screen();
    best_missions_record_score();
  }
}

std::int32_t FrontEnd::run_mission(const MissionRequest& base) {
  // run_mission (0xa1b10): a random loading picture, the wait cursor, the
  // mission, then the cinematic of the boat's fate.
  show_mission_loading();

  // init_simulation reloads GOPTION.SAV and, for a single mission,
  // REALISM.SAV; the mode bits are 0.
  MissionRequest request = base;
  request.game_options = GameOptions::load(saves_);
  // A saved mission keeps the realism of its save (options bit 1).
  std::uint32_t options = options_ & ~option::kModeMask;
  if (base.saved_mission < 0 && base.resume == nullptr) {
    RealismFile::load(saves_, options);
  } else {
    options |= option::kModeSavedMission;
  }
  request.options = options;
  request.random = random_;
  const std::int32_t end = runner_.run(request);
  MissionOutcome outcome = runner_.outcome();
  outcome.end_code = end;
  results_ = mission_end_screens(outcome, options);
  return end;
}

void FrontEnd::show_mission_loading() {
  palette_fade_out(0, 0x100);
  gui_.mouse().set_cursor(cursors_, ui::CursorId::kWait);
  const int pick = random_.random_int(4);
  load_screen_pcx(exe_.string_pointer(addr::kLoadingPcx + static_cast<std::uint32_t>(pick) * 4));
  palette_fade_in(palette_, 0, 10);
}

MissionResults FrontEnd::mission_end_screens(const MissionOutcome& outcome, std::uint32_t options) {
  const std::int32_t end = outcome.end_code;
  std::uint32_t cinematic = 0;
  int newspaper = 0;
  if (end == 6) {
    newspaper = 2;
  } else if (end == 7) {
    newspaper = 3;
  } else if (end == 0xc) {
    cinematic = addr::kBotCine;
  } else if (end == 0xd) {
    cinematic = addr::kImpCine;
  } else if (end == 0xe || end == 0xf) {
    cinematic = addr::kSurfCine;
  } else if (end == 10 || end == 11 || end == 0x10) {
    cinematic = addr::kSrecCine;
  }
  if (cinematic != 0) {
    play_cinematic(exe_string(cinematic), -1, -1);
    show_fate_newspaper(1, outcome);
  } else if (outcome.fuel == 0 && (end == 4 || end == 5 || end == 8)) {
    show_towed_out_of_fuel(outcome);
  }
  if (newspaper != 0) {
    show_fate_newspaper(newspaper, outcome);
  }
  // mission_compute_results (0xa1c87), which shows the return to port.
  MissionResults results = compute_mission_results(outcome, options);
  if (results.fate == BoatFate::kOperational && returns_to_port(end)) {
    show_port_return(outcome);
  }
  return results;
}

}  // namespace sh
