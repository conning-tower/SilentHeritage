// The career's menus: CAREER MENU, START A CAREER, CAREER ROSTER, BACKUP
// CAREER and BASE MENU.

#include "game/career_menus.h"

#include <algorithm>
#include <array>
#include <functional>

#include "assets/exe_image.h"
#include "game/career_boats.h"
#include "game/career_ui.h"
#include "ui/controls.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

// SH.EXE addresses.
namespace addr {
constexpr std::uint32_t kMonths = 0x10e910;     // char*[12] "January"..
constexpr std::uint32_t kStatuses = 0x10eac0;   // char*[7] "", "In Port"..
constexpr std::uint32_t kCommands = 0x10eb38;   // char*[4] "ComSubsAsiatic"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;   // i32 1930
// CAREER MENU (dialog 0x116cf8).
constexpr std::uint32_t kCareerPcx = 0x1032e4;  // "M6.PCX"
constexpr std::uint32_t kCareerTitle = 0xf0b10;
// BASE MENU (dialog 0x116f0c).
constexpr std::uint32_t kBasePcx = 0x1032fc;    // "RTNPORT.PCX"
constexpr std::uint32_t kBaseTitle = 0xf0b54;
constexpr std::uint32_t kStartPatrol = 0xf0b5e;
constexpr std::uint32_t kTopCommanders = 0xf0b6f;
constexpr std::uint32_t kViewCareer = 0xf0b7e;
constexpr std::uint32_t kSaveCareer = 0xf0b8a;
constexpr std::uint32_t kBaseMainMenu = 0xf0b96;
constexpr std::uint32_t kRequestTransfer = 0xf0bae;
constexpr std::uint32_t kRetire = 0xf0bbf;
constexpr std::uint32_t kTransferLine = 0xf0c73;    // "Requesting transfer out of "
constexpr std::uint32_t kTransferDot = 0xf0cbb;     // "."
constexpr std::uint32_t kTransferSure = 0xf0cbd;    // "Are you sure?"
constexpr std::uint32_t kTransferTitle = 0xf0ccb;   // "REQUEST TRANSFER"
constexpr std::uint32_t kYes = 0xf0cdf;
constexpr std::uint32_t kNo = 0xf0cdc;
constexpr std::uint32_t kRetireLine = 0xf0cf9;      // "Retiring Commander "
constexpr std::uint32_t kRetireDot = 0xf0d39;       // "."
constexpr std::uint32_t kRetireSure = 0xf0d3b;      // "Are you sure?"
constexpr std::uint32_t kRetireTitle = 0xf0d49;     // "REQUEST RETIREMENT"
// START A CAREER (dialog 0x124970).
constexpr std::uint32_t kNewCareerPcx = 0x10c4ac;   // "M10.PCX"
constexpr std::uint32_t kNewCareerTitle = 0xf7468;
constexpr std::uint32_t kYears = 0x10c4b4;          // char*[5] "1941".."1945"
constexpr std::uint32_t kDateLabel = 0xf7477;
constexpr std::uint32_t kNameLabel = 0xf747d;
constexpr std::uint32_t kNewCareerOk = 0xf748e;
constexpr std::uint32_t kNewCareerCancel = 0xf7491;
constexpr std::uint32_t kCommandLabel = 0xf7498;
// CAREER ROSTER (dialog 0x119d20) and BACKUP CAREER (0x11a078).
constexpr std::uint32_t kRosterPcx = 0x1038b8;      // "M8.PCX"
constexpr std::uint32_t kRosterTitle = 0xf327c;
constexpr std::uint32_t kRosterCancel = 0xf328a;
constexpr std::uint32_t kRosterView = 0xf3291;
constexpr std::uint32_t kRosterDelete = 0xf3296;
constexpr std::uint32_t kRosterSelect = 0xf329d;
constexpr std::uint32_t kRosterNumber = 0xf32a4;    // "No."
constexpr std::uint32_t kRosterName = 0xf32a8;
constexpr std::uint32_t kRosterDate = 0xf32ad;
constexpr std::uint32_t kRosterStatus = 0xf32b2;
constexpr std::uint32_t kRosterPrompt = 0xf3365;    // "Select a career to continue..."
constexpr std::uint32_t kDeleteLine = 0xf3392;      // "Delete this career."
constexpr std::uint32_t kDeleteSure = 0xf3384;
constexpr std::uint32_t kDeleteTitle = 0xf33a6;
constexpr std::uint32_t kDeleteYes = 0xf33b7;
constexpr std::uint32_t kDeleteNo = 0xf33b4;
constexpr std::uint32_t kRowColon = 0xf3400;        // ":"
constexpr std::uint32_t kRowSpace = 0xf3447;        // " "
constexpr std::uint32_t kBackupTitle = 0xf32b9;
constexpr std::uint32_t kBackupCancel = 0xf32c7;
constexpr std::uint32_t kBackupAccept = 0xf32ce;
constexpr std::uint32_t kBackupLabel = 0xf32d5;
}  // namespace addr

constexpr unsigned kMenuFlags = ui::kDialogTitled | ui::kDialogFramed;

std::function<int(int)> returns(int result) {
  return [result](int) { return result; };
}

// The idle callback of the menus that fade the palette in on their first
// iteration (`career_menu_fade_in` 0x1b91a and the like).
ui::Dialog::IdleFn fade_in_on_open(CareerUi& ui) {
  return [&ui](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
  };
}

// Runs `fn` with the default skin, as the callbacks between
// `menu_free_fonts` and `menu_load_fonts` do.
template <class Fn>
auto with_default_skin(CareerUi& ui, Fn fn) {
  ui.gui().set_skin(ui.default_skin());
  auto result = fn();
  ui.gui().set_skin(ui.menu_skin());
  return result;
}

// Loads a menu picture and fades it in (the screens' common opening).
void open_menu_picture(CareerUi& ui, std::uint32_t pcx, std::uint32_t wait) {
  ui.load_screen_pcx(ui.exe_string(pcx));
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  ui.palette_fade_in(ui.palette(), 0, 10);
  ui.wait_clock(wait);
}

}  // namespace

int career_menu_screen(CareerUi& ui) {
  // career_menu_screen (0x1b89d), dialog 0x116cf8.
  open_menu_picture(ui, addr::kCareerPcx, 0x12);
  ui::Dialog d(ui.gui(), 0x159, 0x4b, 0x10e, 0x140, kMenuFlags);
  d.set_idle(fade_in_on_open(ui));
  d.emplace<ui::TitleBar>(ui.exe_string(addr::kCareerTitle));
  constexpr std::array<std::pair<std::uint32_t, int>, 4> kButtons{{
      {0xf0b1c, career_code::kStartCareer},
      {0xf0b2b, career_code::kContinueCareer},
      {0xf0b3d, career_code::kBestCareers},
      {0xf0b4a, career_code::kMainMenu},
  }};
  int y = 0x82;
  for (const auto& [label, result] : kButtons) {
    auto& button = d.emplace<ui::Button>(0x23, y, 0xc6, ui.exe_string(label), returns(result));
    if (result == career_code::kMainMenu) {
      d.set_back(button);
    }
    y += 0x28;
  }
  ui.add_exit_area(d);
  return ui.run_menu_dialog(d);
}

std::optional<NewCareerChoice> new_career_screen(CareerUi& ui, bool all_maps) {
  // new_career_screen (0x5aa7f), dialog 0x124970 (`new_career_enter_name`).
  const ExeImage& exe = ui.exe();
  open_menu_picture(ui, addr::kNewCareerPcx, 9);
  NewCareerChoice c;
  ui::Dialog d(ui.gui(), 0x122, 0x73, 0x157, 0x168, kMenuFlags);
  d.emplace<ui::TitleBar>(ui.exe_string(addr::kNewCareerTitle));
  ui::Button* begin = nullptr;
  auto& entry = d.emplace<ui::TextEntry>(0x14, 0xa4, 0x12e, 0x15, std::string(),
                                         [&begin](std::string&, int) { return begin->disabled ? 0 : -1; });
  ui::CycleButton* command_cycle = nullptr;
  // new_career_update_command_names (0x5ac7f), after each date change.
  const auto update_command_names = [&] {
    if (command_cycle != nullptr) {
      const std::array<std::string, 3> names = new_career_command_names(c, exe);
      command_cycle->strings.assign(names.begin(), names.end());
      command_cycle->dirty = true;
    }
  };
  d.emplace<ui::CycleButton>(0x68, 0xd5, 0x7d, ui.exe_strings(addr::kMonths, 12), [&](bool advance) {
    if (advance) {
      c.next_month();
      update_command_names();
    }
    return c.month;
  });
  d.emplace<ui::CycleButton>(0xec, 0xd5, 0x5c, ui.exe_strings(addr::kYears, 5), [&](bool advance) {
    if (advance) {
      c.next_year();
      update_command_names();
    }
    return c.year - (1941 - 1930);
  });
  d.emplace<ui::Label>(0x17, 0xdc, ui.exe_string(addr::kDateLabel), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x17, 0x89, ui.exe_string(addr::kNameLabel), ui::FontSlot::kFont1);
  begin = &d.emplace<ui::Button>(0xc4, 300, 0x79, ui.exe_string(addr::kNewCareerOk), ui::return_accept);
  begin->disabled = true;
  d.set_back(d.emplace<ui::Button>(0x14, 300, 0x79, ui.exe_string(addr::kNewCareerCancel), ui::return_cancel));
  d.emplace<ui::GroupBox>(9, 0x7d, 0x14c, 0xcd);
  d.emplace<ui::GroupBox>(9, 0xd0, 0x14c, all_maps ? 0x11d : 0xfa);
  ui.add_exit_area(d);
  if (all_maps) {
    d.emplace<ui::Label>(0x17, 0x101, ui.exe_string(addr::kCommandLabel), ui::FontSlot::kFont1);
    const std::array<std::string, 3> names = new_career_command_names(c, exe);
    command_cycle = &d.emplace<ui::CycleButton>(0x68, 0xf9, 0xe0, std::vector<std::string>(names.begin(), names.end()),
                                                [&](bool advance) {
                                                  if (advance) {
                                                    c.next_command();
                                                  }
                                                  return c.command;
                                                });
  }
  // new_career_dialog_draw (0x5abd9): leading spaces are dropped and OK is
  // enabled once there is a name.
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
    const std::size_t lead = entry.text.find_first_not_of(' ');
    entry.text.erase(0, lead == std::string::npos ? entry.text.size() : lead);
    entry.caret = std::min(entry.caret, static_cast<int>(entry.text.size()));
    const bool empty = entry.text.empty();
    if (empty != begin->disabled) {
      begin->disabled = empty;
      begin->dirty = true;
    }
  });
  if (ui.run_menu_dialog(d) != ui::kResultAccept) {
    return std::nullopt;
  }
  c.name = entry.text;
  c.name.erase(c.name.find_last_not_of(' ') + 1);
  return c;
}

std::vector<std::string> roster_rows(std::span<const CareerRecord> careers, const ExeImage& exe, const Font& font) {
  // croster_build_list (0x35393) with croster_pad_text_to_width (0x35552).
  std::vector<std::string> rows;
  const std::string colon(exe.c_string(addr::kRowColon));
  const std::string space(exe.c_string(addr::kRowSpace));
  for (const CareerRecord& r : careers) {
    std::string row = std::to_string(rows.size() + 1) + colon;
    ui::menu_pad_text_to_width(row, 0x16, font);
    row += r.name;
    ui::menu_pad_text_to_width(row, 0x137, font);
    row += std::string(exe.string_pointer(addr::kMonths + static_cast<std::uint32_t>(r.month) * 4)).substr(0, 3);
    row += space + std::to_string(r.day) + space + std::to_string(r.year + exe.i32(addr::kBaseYear));
    ui::menu_pad_text_to_width(row, 0x196, font);
    row += exe.string_pointer(addr::kStatuses + static_cast<std::uint32_t>(r.status) * 4);
    rows.push_back(std::move(row));
  }
  return rows;
}

int roster_select_screen(CareerUi& ui, std::span<const CareerRecord> careers, int& selection, bool redraw) {
  // croster_select_screen (0x351d3), dialog 0x119d20.
  if (redraw) {
    ui.load_screen_pcx(ui.exe_string(addr::kRosterPcx));
  }
  ui.gui().set_skin(ui.menu_skin());
  const std::vector<std::string> rows = roster_rows(careers, ui.exe(), ui.menu_resources().large);
  if (redraw) {
    ui.palette_fade_out(0, 0x100);
  }
  const bool none = careers.empty();
  // The row of a career just deleted may be past the end of the list.
  selection = std::clamp(selection, 0, std::max(static_cast<int>(careers.size()) - 1, 0));
  ui::Dialog d(ui.gui(), 0xb, 0xe, 0x26b, 0x1c7, kMenuFlags);
  d.emplace<ui::TitleBar>(ui.exe_string(addr::kRosterTitle));
  d.set_back(d.emplace<ui::Button>(0x2b, 400, 0x79, ui.exe_string(addr::kRosterCancel), ui::return_cancel));
  d.emplace<ui::Button>(0xb1, 400, 0x79, ui.exe_string(addr::kRosterView), returns(career_code::kRosterView))
      .disabled = none;
  // croster_delete_career (0x35352)
  d.emplace<ui::Button>(0x136, 400, 0x79, ui.exe_string(addr::kRosterDelete), [&ui](int) {
     const int answer = with_default_skin(ui, [&ui] {
       return ui.ask_yes_no(ui.exe_string(addr::kDeleteTitle), ui.exe_string(addr::kDeleteLine),
                            ui.exe_string(addr::kDeleteSure), ui.exe_string(addr::kDeleteYes),
                            ui.exe_string(addr::kDeleteNo));
     });
     return answer == ui::kResultAccept ? career_code::kRosterDelete : 0;
   }).disabled = none;
  auto& select = d.emplace<ui::Button>(0x1bb, 400, 0x79, ui.exe_string(addr::kRosterSelect), ui::return_accept);
  select.disabled = none;
  auto& list = d.emplace<ui::List>(
      0x19, 0xbc, 0x210, [&rows] { return static_cast<int>(rows.size()); },
      [&rows](int row) { return rows[static_cast<std::size_t>(row)]; });
  // croster_on_select_row (0x352fe): SELECT only for careers in port or on
  // patrol. The list's activation callback (0x352ef) returns a value the
  // list ignores, so a double click selects nothing.
  list.on_select = [&](int row) {
    if (careers.empty() || row < 0) {
      return;
    }
    const CareerStatus s = careers[static_cast<std::size_t>(row)].status;
    select.disabled = s != CareerStatus::kInPort && s != CareerStatus::kOnPatrol;
    select.dirty = true;
  };
  d.emplace<ui::GroupBox>(9, 0x9b, 0x260, 0x177);
  d.emplace<ui::Label>(0x1c, 0xa4, ui.exe_string(addr::kRosterNumber), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x32, 0xa4, ui.exe_string(addr::kRosterName), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x154, 0xa4, ui.exe_string(addr::kRosterDate), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x1b2, 0xa4, ui.exe_string(addr::kRosterStatus), ui::FontSlot::kFont1);
  d.emplace<ui::TextField>(0, 0x82, 0x26a, 0x91, [&ui] { return ui.exe_string(addr::kRosterPrompt); },
                           ui::FontSlot::kFont1, TextAlign::kCentre);
  ui.add_exit_area(d);
  // croster_dialog_draw (0x3529f)
  bool fade_pending = redraw;  // 0x119d00
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      list.selection = selection;
      list.top = selection;
      list.dirty = true;
      list.on_select(selection);
      return;
    }
    if (fade_pending) {
      ui.palette_fade_in(ui.palette(), 0, 10);
      fade_pending = false;
    }
  });
  const int result = ui.run_menu_dialog(d);
  selection = list.selection;
  return result == ui::kResultAccept ? selection : result;
}

std::optional<std::string> backup_career_dialog(CareerUi& ui) {
  // croster_save_career_as (0x35669), dialog 0x11a078, from SAVE CAREER
  // after `menu_free_fonts`.
  ui::Dialog d(ui.gui(), 0x91, 0xb4, 0x154, 0x91, ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d.emplace<ui::TitleBar>(ui.exe_string(addr::kBackupTitle));
  auto& entry = d.emplace<ui::TextEntry>(0xc, 0x46, 0x13c, 0x15, std::string(),
                                         [](std::string&, int) { return ui::kResultAccept; });
  d.emplace<ui::Button>(0x1a, 0x6a, 100, ui.exe_string(addr::kBackupCancel), ui::return_cancel);
  // 0x3576d: Accept needs a name.
  d.emplace<ui::Button>(0xd6, 0x6a, 100, ui.exe_string(addr::kBackupAccept),
                        [&entry](int) { return entry.text.empty() ? 0 : ui::kResultAccept; });
  d.emplace<ui::Label>(0x3e, 0x30, ui.exe_string(addr::kBackupLabel), ui::FontSlot::kFont1);
  ui.palette_grey_out(0x46, 0xff);
  const int result = d.run();
  ui.palette_restore_saved(0xff);
  if (result != ui::kResultAccept) {
    return std::nullopt;
  }
  return entry.text;
}

int base_menu_screen(CareerUi& ui, const Career& career, bool redraw, const std::function<void()>& save_career) {
  // career_base_menu_screen (0x1b94a), dialog 0x116f0c.
  const CareerRecord& r = career.record;
  if (r.on_patrol != 0) {
    return career_code::kStartPatrol;
  }
  if (redraw) {
    ui.load_screen_pcx(ui.exe_string(addr::kBasePcx));
  }
  ui.gui().set_skin(ui.menu_skin());
  if (redraw) {
    ui.palette_fade_out(0, 0x100);
    ui.palette_fade_in(ui.palette(), 0, 10);
    ui.wait_clock(0x12);
  }
  // The buttons after VIEW CAREER (y 0xd2) follow each other 0x28 apart;
  // the dialog ends 0x6e below SAVE CAREER and is centred vertically.
  const bool transfer = career.summary.transfer_offered != 0;
  const bool retire = r.patrols > 4;
  const int save_y = 0xd2 + 0x28 * (1 + (transfer ? 1 : 0) + (retire ? 1 : 0));
  const int h = save_y + 0x6e;
  ui::Dialog d(ui.gui(), 5, (kScreenHeight - h) / 2, 0x104, h, kMenuFlags);
  ui.add_exit_area(d);
  d.emplace<ui::TitleBar>(ui.exe_string(addr::kBaseTitle));
  d.emplace<ui::Button>(0x1e, 0x82, 0xc6, ui.exe_string(addr::kStartPatrol), returns(career_code::kStartPatrol));
  d.emplace<ui::Button>(0x1e, 0xaa, 0xc6, ui.exe_string(addr::kTopCommanders),
                        returns(career_code::kTopCommanders));
  d.emplace<ui::Button>(0x1e, 0xd2, 0xc6, ui.exe_string(addr::kViewCareer), returns(career_code::kViewCareer));
  int y = 0xfa;
  if (transfer) {
    // career_request_transfer (0x1bad5)
    d.emplace<ui::Button>(0x1e, y, 0xc6, ui.exe_string(addr::kRequestTransfer), [&ui, &career](int) {
      const std::string line =
          ui.exe_string(addr::kTransferLine) +
          std::string(ui.exe().string_pointer(addr::kCommands +
                                              static_cast<std::uint32_t>(career.summary.current_command) * 4)) +
          ui.exe_string(addr::kTransferDot);
      const int answer = with_default_skin(ui, [&] {
        return ui.ask_yes_no(ui.exe_string(addr::kTransferTitle), line, ui.exe_string(addr::kTransferSure),
                             ui.exe_string(addr::kYes), ui.exe_string(addr::kNo));
      });
      return answer == ui::kResultAccept ? career_code::kTransfer : 0;
    });
    y += 0x28;
  }
  if (retire) {
    // career_request_retirement (0x1bb77)
    d.emplace<ui::Button>(0x1e, y, 0xc6, ui.exe_string(addr::kRetire), [&ui, &r](int) {
      const std::string line = ui.exe_string(addr::kRetireLine) + r.name + ui.exe_string(addr::kRetireDot);
      const int answer = with_default_skin(ui, [&] {
        return ui.ask_yes_no(ui.exe_string(addr::kRetireTitle), line, ui.exe_string(addr::kRetireSure),
                             ui.exe_string(addr::kYes), ui.exe_string(addr::kNo));
      });
      return answer == ui::kResultAccept ? career_code::kRetire : 0;
    });
    y += 0x28;
  }
  // career_base_save_cb (0x1bac3)
  d.emplace<ui::Button>(0x1e, y, 0xc6, ui.exe_string(addr::kSaveCareer), [&ui, &save_career](int) {
    return with_default_skin(ui, [&save_career] {
      save_career();
      return 0;
    });
  });
  d.set_back(d.emplace<ui::Button>(0x1e, y + 0x28, 0xc6, ui.exe_string(addr::kBaseMainMenu),
                                   returns(career_code::kLeaveCareer)));
  return ui.run_menu_dialog(d);
}

}  // namespace sh
