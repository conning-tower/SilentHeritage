// The CONTINUE MISSION screens of module wscreens: `continue_mission_loop`
// (0xa1486) and `list_saved_missions` (0xa42a4), dialog 0x188534 of
// `wscreens_init_dialogs`, over the MISSn.SAV files (game/mission_save.h).

#include <string>
#include <vector>

#include "game/frontend.h"
#include "game/mission_save.h"
#include "ui/controls.h"

namespace sh {

namespace {

// SH.EXE addresses.
namespace addr {
constexpr std::uint32_t kMenuPcx = 0x110940;     // "M8.PCX"
constexpr std::uint32_t kTitle = 0xfbf0c;        // "CONTINUE MISSION"
constexpr std::uint32_t kCancel = 0xfbf1d;       // "CANCEL"
constexpr std::uint32_t kDelete = 0xfbf24;       // "DELETE"
constexpr std::uint32_t kBegin = 0xfbf2b;        // "BEGIN"
constexpr std::uint32_t kNoSaves = 0xfc59f;      // "No missions have been saved."
constexpr std::uint32_t kDeleteText = 0xfc5c9;   // "Delete "
constexpr std::uint32_t kDeleteTitle = 0xfc5ec;  // "DELETE MISSION"
constexpr std::uint32_t kSure = 0xfc5de;         // "Are you sure?"
constexpr std::uint32_t kYes = 0xfc5fe;
constexpr std::uint32_t kNo = 0xfc5fb;
}  // namespace addr

constexpr int kBegin = ui::kResultAccept;
constexpr int kDeleted = 0x51;
constexpr int kNoSave = 0x17;
// run_mission's end codes the loop reacts to.
constexpr std::int32_t kEndAbort = 1;
constexpr std::int32_t kEndQuitToDos = 3;

}  // namespace

bool FrontEnd::continue_mission_loop() {
  // continue_mission_loop (0xa1486)
  bool first = true;
  bool show_background = true;
  while (true) {
    const int result = list_saved_missions(first, show_background);
    show_background = true;
    if (result == ui::kResultCancel) {
      return true;
    }
    if (result == kBegin) {
      const std::optional<MissionSaveFile> save = load_mission_save(saves_, continue_save_);
      if (!save) {
        host_.report(mission_save_file_name(continue_save_) + ": not a saved mission");
        return true;
      }
      // The save's realism bits go to the options word, in saved-mission
      // mode (bit 1); its equipment is the scenario's.
      options_ = (options_ & ~0xffffu) | (save->header.options() & 0xffffu);
      MissionRequest request;
      request.type = MissionType::kScenario;
      request.saved_mission = continue_save_;
      const std::int32_t end = run_mission(request);
      if (end == kEndQuitToDos) {
        // fatal_error(0, 0): the game ends.
        throw QuitRequested();
      }
      if (end != kEndAbort) {
        show_mission_results(end);
      }
      return true;
    }
    if (result == kNoSave) {
      // The message box stayed over the menu on the first call.
      return !first;
    }
    if (result == kDeleted) {
      show_background = false;
    }
    first = false;
  }
}

int FrontEnd::list_saved_missions(bool first_call, bool show_background) {
  // list_saved_missions (0xa42a4)
  if (first_call) {
    saves_selection_ = 0;
    saves_top_ = 0;
  }
  const std::vector<MissionSaveHeader> saves = list_mission_saves(saves_);
  if (saves.empty()) {
    if (show_background) {
      palette_fade_in(palette_, 0, 10);
      show_message_box(exe_string(addr::kTitle), exe_string(addr::kNoSaves), "");
    }
    return kNoSave;
  }
  saves_selection_ = std::min(saves_selection_, static_cast<int>(saves.size()) - 1);
  if (show_background) {
    load_screen_pcx(exe_string(addr::kMenuPcx));
    palette_fade_out(0, 0x100);
  }
  gui_.set_skin(menu_skin_);
  const auto summary = [&](int row) {
    return saved_mission_summary(saves[static_cast<std::size_t>(row)], exe_, menu_res_.small);
  };

  ui::Dialog d(gui_, 0x45, 0x13, 0x1f6, 0x1bd, ui::kDialogTitled | ui::kDialogFramed);
  d.emplace<ui::TitleBar>(exe_string(addr::kTitle));
  auto& list = d.emplace<ui::List>(
      0x1f, 0x78, 400, [&saves] { return static_cast<int>(saves.size()); },
      [&saves](int row) { return saves[static_cast<std::size_t>(row)].name; });
  d.emplace<ui::Button>(0x23, 0x18b, 0x79, exe_string(addr::kCancel), ui::return_cancel);
  // delete_saved_mission (0xa4512): DELETE MISSION? with the default skin.
  d.emplace<ui::Button>(0xb9, 0x18b, 0x79, exe_string(addr::kDelete), [&](int) {
    const MissionSaveHeader& h = saves[static_cast<std::size_t>(list.selection)];
    gui_.set_skin(default_skin_);
    const int yes = ask_yes_no(exe_string(addr::kDeleteTitle), exe_string(addr::kDeleteText) + h.name,
                               exe_string(addr::kSure), exe_string(addr::kYes), exe_string(addr::kNo));
    gui_.set_skin(menu_skin_);
    if (yes == ui::kResultAccept) {
      delete_mission_save(saves_, h.number);
      return kDeleted;
    }
    return 0;
  });
  d.emplace<ui::Button>(0x14f, 0x18b, 0x79, exe_string(addr::kBegin), ui::return_accept);
  auto& text = d.emplace<ui::TextBox>(0x1e, 0x128, 0x1a0, 0x4a, 10, 1000, summary(saves_selection_));
  text.read_only = true;
  add_exit_area(d);
  // continue_mission_on_select (0xa45b7)
  list.on_select = [&](int row) {
    text.top = 0;
    text.text = summary(row);
    text.dirty = true;
  };
  // continue_mission_dialog_draw (0xa44c5): the list position comes back
  // on the first call, the picture fades in once.
  bool fade_pending = show_background;  // 0x187240
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      list.selection = saves_selection_;
      list.top = saves_top_;
      list.dirty = true;
    } else if (fade_pending) {
      palette_fade_in(palette_, 0, 10);
      fade_pending = false;
    }
  });
  const int result = run_menu_dialog(d);
  saves_selection_ = list.selection;
  saves_top_ = list.top;
  if (result != ui::kResultCancel && list.selection >= 0) {
    continue_save_ = saves[static_cast<std::size_t>(list.selection)].number;
  }
  return result;
}

}  // namespace sh
