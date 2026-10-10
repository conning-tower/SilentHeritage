#include "editor/editor.h"

#include <algorithm>
#include <format>
#include <utility>

#include "assets/pcx.h"
#include "core/error.h"
#include "editor/dialogs_boxes.h"
#include "editor/dialogs_patrol.h"
#include "editor/dialogs_select.h"
#include "editor/dialogs_settings.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"

namespace sh::editor {

namespace {

// The menu bar (0x9bee8) and the Files menu (0x9be00), whose items start at
// Run (0x9bd74) or at New (0x9bd88).
constexpr std::uint32_t kMenuBar = 0x9bee8;
constexpr std::uint32_t kFilesMenu = 0x9be00;
constexpr std::uint32_t kFilesFromRun = 0x9bd74;
constexpr std::uint32_t kFilesFromNew = 0x9bd88;
constexpr std::uint32_t kBarItemSize = 0x10;
constexpr std::uint32_t kPopupItemSize = 0x14;

// The item callbacks (`menu_about` .. `menu_file_delete_scenario`).
constexpr std::array<std::pair<std::uint32_t, MenuCommand>, 15> kCallbacks{{
    {0x24841, MenuCommand::kAbout},
    {0x24836, MenuCommand::kExit},
    {0x2484c, MenuCommand::kPassword},
    {0x24857, MenuCommand::kBriefingText},
    {0x24862, MenuCommand::kPatrolArea},
    {0x2486d, MenuCommand::kEnvironment},
    {0x24878, MenuCommand::kTorpedoes},
    {0x24883, MenuCommand::kEquipment},
    {0x2488e, MenuCommand::kOptions},
    {0x24899, MenuCommand::kRun},
    {0x248a4, MenuCommand::kNew},
    {0x248af, MenuCommand::kSaveScenario},
    {0x248ba, MenuCommand::kLoadScenario},
    {0x248c5, MenuCommand::kLoadHistorical},
    {0x248d0, MenuCommand::kDeleteScenario},
}};

// The keys of `editor_menu_handle_event`.
constexpr std::uint16_t kKeyAltQ = 0x1000;
// The palette greying of the dispatcher (`palette_grey_out(0x32, 0xff)`)
// and of the chart's boxes (`palette_grey_out(0x46, 0xff)`).
constexpr int kMenuGrey = 50;
constexpr int kBoxGrey = 70;
constexpr int kLastGreyed = 0xff;

// The title box (dialog 0xaf384, `sb_intro_draw`).
constexpr std::uint32_t kVersion = 0x98308;  // "1.00"
constexpr int kIntroX = 0x4e;
constexpr int kIntroY = 0x3a;
constexpr int kIntroW = 0x1e3;
constexpr int kIntroH = 0x16c;
constexpr int kVersionX = 0x169;
constexpr int kVersionY = 0xf2;
// `sbchart_open` waits for a key at most until this GUI tick.
constexpr std::uint32_t kIntroWaitTicks = 399;

}  // namespace

// --- The menu -----------------------------------------------------------------

EditorMenu::EditorMenu(const SeExe& se, bool started_by_game, MenuCommand& selection) {
  const ExeImage& image = se.image();
  const std::uint32_t count = image.u32(kMenuBar);
  const std::uint32_t titles = image.u32(kMenuBar + 4);
  bar_.shadow = image.i32(kMenuBar + 8) != 0;
  if (count > menus_.size()) {
    throw DataError(std::format("SE.EXE: menu bar of {} titles", count));
  }
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::uint32_t t = titles + i * kBarItemSize;
    const std::uint32_t menu = image.u32(t + 8);
    // editor_menu_handle_event points the Files menu at its items from Run
    // or from New.
    std::uint32_t items = image.u32(menu + 4);
    std::uint32_t n = image.u32(menu);
    if (menu == kFilesMenu) {
      items = started_by_game ? kFilesFromRun : kFilesFromNew;
      n = started_by_game ? 7 : 6;
    }
    ui::PopupMenu& popup = menus_[i];
    for (std::uint32_t k = 0; k < n; ++k) {
      const std::uint32_t a = items + k * kPopupItemSize;
      const std::uint32_t callback = image.u32(a + 0xc);
      const auto it = std::ranges::find(kCallbacks, callback, &std::pair<std::uint32_t, MenuCommand>::first);
      if (it == kCallbacks.end()) {
        throw DataError(std::format("SE.EXE: unknown menu callback 0x{:x}", callback));
      }
      const MenuCommand command = it->second;
      popup.items.push_back(ui::PopupItem::make_action(
          std::string(image.string_pointer(a)), [&selection, command] { selection = command; }, image.u32(a + 0x10),
          std::string(image.string_pointer(a + 4))));
    }
    bar_.items.push_back({.menu = &popup,
                          .label = std::string(image.string_pointer(t + 4)),
                          .x = image.i32(t),
                          .key = image.u32(t + 0xc)});
  }
}

// --- The editor -----------------------------------------------------------------

Editor::Editor(const EditorServices& services, FrontendHost& host, bool started_by_game, Scenario::Clock clock)
    : services_(services),
      started_by_game_(started_by_game),
      clock_(std::move(clock)),
      se_(SeExe::load(services.install)),
      ui_(services.install, se_, host) {}

Editor::~Editor() = default;

void Editor::open_intro() {
  // sb_intro_open (0x461b3): the dialog draws into the screen itself.
  intro_ = std::make_unique<ui::Dialog>(ui_.gui(), kIntroX, kIntroY, kIntroW, kIntroH, ui::kDialogTitled,
                                        &ui_.screen());
  intro_->emplace<ui::OwnerDraw>([this](const Viewport& gfx, ui::OwnerDraw&) {
    // sb_intro_draw (0x461e0): the picture's colours faded in, then the
    // picture and the version.
    const Shp intro = Shp::load(services_.install, "ART/SBINTRO.SHP");
    const Font digits = Font::load(services_.install, "ART/SBDIGITS.FNT");
    sprite_apply_palette(intro.frame(0), ui_.palette());
    ui_.palette_fade_in(ui_.palette(), 0, 10);
    draw_sprite_clipped(gfx, intro.frame(0), 0, 0);
    gui_draw_text(gfx, kVersionX, kVersionY, digits, ui_.str(kVersion), &ui_.resources().table);
    ui_.frame();
    ui_.palette_fade_in(ui_.palette(), 0, 10);
  });
  intro_->open();
  intro_->process();
}

void Editor::close_intro() {
  if (intro_) {
    intro_->close();
    intro_.reset();
  }
}

ChartHooks Editor::chart_hooks() {
  ChartHooks h;
  h.menu_event = [this](const ui::Event& event) { return menu_event(event); };
  h.ask_yes_no = [this](std::string_view title, const std::array<std::string_view, 3>& lines) {
    ui_.palette_grey_out(kBoxGrey, kLastGreyed);
    const bool yes = ask_yes_no(ui_, title, lines[0], lines[1], lines[2], ui_.str(se_addr::kYes),
                                ui_.str(se_addr::kNo)) == ui::kResultAccept;
    ui_.palette_restore_saved(kLastGreyed);
    return yes;
  };
  h.message = [this](std::string_view title, const std::array<std::string_view, 3>& lines) {
    ui_.palette_grey_out(kBoxGrey, kLastGreyed);
    show_message_box(ui_, title, lines[0], lines[1], lines[2]);
    ui_.palette_restore_saved(kLastGreyed);
  };
  h.select_sub_type = [this](std::int32_t& sub_class, ScnChars<31>& name) {
    return select_sub_type(ui_, *scenario_, options_, sub_class, name);
  };
  h.select_sub_name = [this](std::int32_t sub_class, ScnChars<31>& name) {
    return select_sub_name(ui_, *data_, sub_class, name);
  };
  h.select_ship_type = [this](std::int32_t& ship_class) { select_ship_type(ui_, *scenario_, options_, ship_class); };
  h.select_aircraft_type = [this](std::int32_t& aircraft) {
    return select_aircraft_type(ui_, *scenario_, options_, aircraft);
  };
  h.orders_popup = [this](const std::vector<std::string>& labels, const std::vector<bool>& disabled, int initial) {
    return orders_popup_run(ui_, labels, disabled, initial);
  };
  h.fade_in = [this](const VgaPalette& target) { ui_.palette_fade_in(target, 0, 10); };
  return h;
}

EditorExit Editor::run() {
  const Install& install = services_.install;
  // init_video: started by SH, the screen keeps M8.PCX and its palette;
  // started from DOS it is black.
  if (started_by_game_) {
    PcxImage m8 = load_pcx(install, "ART/M8.PCX");
    ui_.screen().blit(m8.image, 0, 0);
    ui_.palette() = m8.palette;
    ui_.dac() = m8.palette;
  }
  // sb_load_data (0x44e0b): the title box, SBOPTION.SAV, the tables, the
  // world and a new scenario.
  open_intro();
  options_ = EditorOptions::load(services_.saves);
  data_.emplace(EditorData::load(install));
  scenario_ = std::make_unique<Scenario>(install, *data_, options_, clock_);
  files_ = std::make_unique<FileCommands>(FileEnv{
      .ui = ui_, .scenario = *scenario_, .install = install, .paths = services_.paths, .custom = services_.custom});
  menu_ = std::make_unique<EditorMenu>(se_, started_by_game_, selection_);
  exiting_ = false;

  // sbchart_run (0x3b602) with sbchart_open (0x3b668): the chart behind
  // the title box, which stays until the first frame, then a short wait
  // for a key or a click (the map is drawn at the first frame here).
  chart_ = std::make_unique<ChartScreen>(
      ChartScreenEnv{install, ui_.gui(), ui_.palette(), ui_.font1(), ui_.msg_font(), ui_.menu_font(), ui_.sbchart(),
                     ui_.cursors()},
      *scenario_, options_, chart_hooks(), started_by_game_);
  close_intro();
  chart_->open();
  // The title box applies its colours again; the chart's palette is the one
  // the first frame fades in.
  const VgaPalette chart_palette = ui_.palette();
  open_intro();
  ui_.palette() = chart_palette;
  ui::EventQueue& events = ui_.gui().events();
  while (ui_.gui().ticks() <= kIntroWaitTicks) {
    const std::optional<ui::Event> pending = events.peek();
    if (pending && (pending->flags & ui::kEventMoved) == 0) {
      break;
    }
    ui_.frame();
  }
  bool first = true;
  running_ = true;
  while (!exiting_) {
    std::optional<ui::Event> event;
    do {
      event = events.get();
      if (event) {
        chart_->handle_event(*event);
      }
      chart_->process();
    } while (events.peek() && event && (event->flags & ui::kEventMoved) != 0);
    chart_->update(first);
    if (first) {
      // sbchart_update closes the title box after the fade in.
      close_intro();
      first = false;
    }
    ui_.frame();
  }
  running_ = false;
  chart_->close();
  return exit_;
}

bool Editor::menu_event(const ui::Event& event) {
  // editor_menu_handle_event (0x2491b)
  selection_ = MenuCommand::kNone;
  if ((event.is_key() && event.key == ui::kKeyEsc) || (event.flags & ui::kEventRightUp) != 0) {
    chart_->with_panel_closed([this] {
      chart_->leave_view();
      ui_.palette_grey_out(kMenuGrey, kLastGreyed);
      ui::DefaultMenuHooks hooks(ui_.resources());
      ui::menu_bar_run(ui_.gui(), hooks, menu_->bar());
      if (selection_ != MenuCommand::kNone) {
        run_command(selection_);
      }
      if (!exiting_) {
        ui_.palette_restore_saved(kLastGreyed);
      }
    });
    return true;
  }
  if (event.is_key() && event.key == kKeyAltQ) {
    chart_->with_panel_closed([this] {
      chart_->leave_view();
      ui_.palette_grey_out(kMenuGrey, kLastGreyed);
      if (quit_to_dos_prompt(ui_)) {
        exit_ = EditorExit::kQuitToDos;
        exiting_ = true;
      } else {
        ui_.palette_restore_saved(kLastGreyed);
      }
    });
  }
  // Alt-M runs MemCheck's `memstat_report`, which is not reimplemented.
  // Neither key counts as taken.
  return false;
}

void Editor::run_command(MenuCommand command) {
  Scenario& s = *scenario_;
  switch (command) {
    case MenuCommand::kNone:
      break;
    case MenuCommand::kAbout:
      show_about_box(ui_);
      break;
    case MenuCommand::kExit:
      if (files_->ask_save_changes() == ui::kResultAccept) {
        exit_ = EditorExit::kExit;
        exiting_ = true;
      }
      break;
    case MenuCommand::kPassword:
      cmd_set_password(ui_, s);
      break;
    case MenuCommand::kBriefingText:
      cmd_briefing_text(ui_, s);
      break;
    case MenuCommand::kPatrolArea:
      if (cmd_patrol_area(ui_, s, options_, [this] { return files_->save(); })) {
        chart_->world_changed();
        chart_->center_on_player();
      }
      break;
    case MenuCommand::kEnvironment:
      cmd_environment(ui_, s, options_);
      break;
    case MenuCommand::kTorpedoes:
      cmd_torpedoes(ui_, s, options_);
      break;
    case MenuCommand::kEquipment:
      cmd_equipment(ui_, s, options_);
      break;
    case MenuCommand::kOptions:
      cmd_options(ui_, s, options_, services_.saves);
      break;
    case MenuCommand::kRun:
      if (files_->run()) {
        exit_ = EditorExit::kRun;
        exiting_ = true;
      }
      break;
    case MenuCommand::kNew:
      if (files_->new_scenario()) {
        // sb_reset_scenario's sb_setup_world resets the view (zoom 1, the
        // map's centre), then sb_center_on_player.
        chart_->world_changed();
        chart_->center_on_player();
      }
      break;
    case MenuCommand::kSaveScenario:
      files_->save();
      break;
    case MenuCommand::kLoadScenario:
    case MenuCommand::kLoadHistorical:
    case MenuCommand::kDeleteScenario: {
      const ScenarioListMode mode = command == MenuCommand::kLoadHistorical ? ScenarioListMode::kLoadHistorical
                                    : command == MenuCommand::kLoadScenario ? ScenarioListMode::kLoadScenario
                                                                            : ScenarioListMode::kDeleteScenario;
      if (files_->load(mode)) {
        // The world was loaded again: the view is rebuilt on it.
        chart_->world_changed();
        chart_->center_on_player();
      }
      break;
    }
  }
}

}  // namespace sh::editor
