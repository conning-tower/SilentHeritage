#include "editor/dialogs_boxes.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"

namespace sh::editor {

namespace {

constexpr unsigned kBoxFlags = ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel;
constexpr int kButtonWidth = 100;

// SE strings and tables.
constexpr std::uint32_t kMessageOk = 0x90956;
constexpr std::uint32_t kSaveChangesTitle = 0x90960;
constexpr std::uint32_t kSaveChangesSave = 0x90968;
constexpr std::uint32_t kSaveChangesDiscard = 0x9096d;
constexpr std::uint32_t kSaveChangesCancel = 0x90975;
constexpr std::uint32_t kSaveChangesText = 0x916fd;
constexpr std::uint32_t kQuitTitle = 0x9097c;
constexpr std::uint32_t kQuitCells = 0x9c080;  // two 0x28-byte grid cells
constexpr std::uint32_t kAboutContinue = 0x90989;
constexpr std::uint32_t kAboutLines = 0x9bcc4;  // 11 x {x, y, char*, append version}
constexpr std::uint32_t kVersion = 0x98308;     // "1.00"
// The return value of SAVE (`save_changes_on_save`).
constexpr int kSaveChangesOnSave = 10;

// Adds the three centred FONT1 lines of the message and yes / no boxes,
// from x 20 to x1, at the rows of dialogs 0xa9a84 / 0xa9c54.
void add_lines(EditorUi& ui, ui::Dialog& d, int x1, std::string_view line1, std::string_view line2,
               std::string_view line3) {
  const std::array<std::array<int, 2>, 3> rows{{{47, 58}, {60, 68}, {73, 78}}};
  const std::array<std::string_view, 3> lines{line1, line2, line3};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    auto& f = d.emplace<ui::TextField>(20, rows[i][0], x1, rows[i][1], [s = std::string(lines[i])] { return s; },
                                       ui::FontSlot::kCustom, TextAlign::kCentre);
    f.fill = 6;
    f.custom = &ui.font1();
  }
}

// The width of the widest line: the lines measured in the GUI font of
// slot 2 (0xa63fc, GUI_M) although FONT1 draws them, the title in GUI_L.
int text_width(EditorUi& ui, std::string_view title, std::string_view line1, std::string_view line2,
               std::string_view line3) {
  const Font& lines = ui.resources().font1;
  return std::max({lines.text_width(line1), lines.text_width(line2), ui.resources().large.text_width(title),
                   lines.text_width(line3)});
}

}  // namespace

void show_message_box(EditorUi& ui, std::string_view title, std::string_view line1, std::string_view line2,
                      std::string_view line3) {
  const int width = std::max(text_width(ui, title, line1, line2, line3), kButtonWidth);
  const int w = width + 40;
  EditorDialog d(ui, (kScreenWidth - w) / 2, 200, w, 128, kBoxFlags);
  d.emplace<ui::TitleBar>(std::string(title));
  d.emplace<ui::Button>((w - kButtonWidth) / 2, 93, kButtonWidth, ui.str(kMessageOk), ui::return_accept);
  add_lines(ui, d, width + 20, line1, line2, line3);
  d.run();
}

int ask_yes_no(EditorUi& ui, std::string_view title, std::string_view line1, std::string_view line2,
               std::string_view line3, std::string_view yes, std::string_view no) {
  const int width = std::max(text_width(ui, title, line1, line2, line3), 2 * kButtonWidth);
  const int w = width + 40;
  EditorDialog d(ui, (kScreenWidth - w) / 2, 192, w, 135, kBoxFlags);
  d.emplace<ui::TitleBar>(std::string(title));
  d.emplace<ui::Button>(15, 100, kButtonWidth, std::string(no), ui::return_cancel, 0xff4e);
  d.emplace<ui::Button>(width + 25 - kButtonWidth, 100, kButtonWidth, std::string(yes), ui::return_accept, 0xff59);
  add_lines(ui, d, width + 20, line1, line2, line3);
  return d.run();
}

int ask_save_changes(EditorUi& ui, bool modified, const std::function<int()>& save) {
  if (!modified) {
    return ui::kResultAccept;
  }
  EditorDialog d(ui, 203, 142, 233, 224, kBoxFlags);
  d.emplace<ui::TitleBar>(ui.str(kSaveChangesTitle));
  d.emplace<ui::Button>(67, 131, kButtonWidth, ui.str(kSaveChangesSave), [](int) { return kSaveChangesOnSave; });
  d.emplace<ui::Button>(67, 92, kButtonWidth, ui.str(kSaveChangesDiscard), ui::return_accept);
  d.emplace<ui::Button>(67, 169, kButtonWidth, ui.str(kSaveChangesCancel), ui::return_cancel);
  auto& text = d.emplace<ui::TextField>(20, 50, 213, 68, [&ui] { return ui.str(kSaveChangesText); },
                                        ui::FontSlot::kCustom, TextAlign::kCentre);
  text.fill = 6;
  text.custom = &ui.font1();
  d.emplace<ui::GroupBox>(21, 80, 211, 204);
  const int result = d.run();
  if (result == ui::kResultAccept || result == ui::kResultCancel) {
    return result;
  }
  return save();
}

bool quit_to_dos_prompt(EditorUi& ui) {
  const ExeImage& se = ui.se().image();
  bool quit = false;
  EditorDialog d(ui, 211, 191, 219, 116, kBoxFlags);
  d.emplace<ui::TitleBar>(ui.str(kQuitTitle));
  d.emplace<ui::GroupBox>(11, 44, 208, 105);
  std::vector<ui::GridCell> cells;
  for (std::uint32_t i = 0; i < 2; ++i) {
    const std::uint32_t a = kQuitCells + i * 0x28;
    cells.push_back({.x = se.i32(a),
                     .y = se.i32(a + 4),
                     .w = se.i32(a + 8),
                     .label = std::string(se.string_pointer(a + 0xc)),
                     .up = se.i32(a + 0x10),
                     .down = se.i32(a + 0x14),
                     .left = se.i32(a + 0x18),
                     .right = se.i32(a + 0x1c),
                     .key = se.u32(a + 0x20),
                     .disabled = se.i32(a + 0x24) != 0});
  }
  // quit_menu_select (0x2b31c): YES sets the exit code 3.
  d.emplace<ui::ButtonGrid>(std::move(cells), [&quit](int cell) {
    if (cell == 1) {
      quit = true;
    }
    return ui::kResultAccept;
  });
  d.run();
  return quit;
}

void show_about_box(EditorUi& ui) {
  const ExeImage& se = ui.se().image();
  EditorDialog d(ui, 54, 125, 532, 230, kBoxFlags);
  // about_box_draw (0x2b5e5)
  d.emplace<ui::OwnerDraw>([&ui, &se](const Viewport& gfx, ui::OwnerDraw&) {
    ui::GuiResources& res = ui.resources();
    for (std::uint32_t i = 0; i < 11; ++i) {
      const std::uint32_t a = kAboutLines + i * 16;
      std::string line(se.string_pointer(a + 8));
      if (se.i32(a + 12) != 0) {
        line += ui.str(kVersion);
      }
      gui_draw_text(gfx, se.i32(a), se.i32(a + 4), ui.font1(), line, &res.table);
    }
    const SpriteFrame& logo = ui.sbchart().frame(sbchart_frame::kAboutLogo);
    // The logo sits at the group box's corner (0xaa2c4 / 0xaa2c8).
    draw_sprite_clipped(gfx, logo, 11, 11);
    ui.set_dac_from_frame(logo, 224, 254);
  });
  d.emplace<ui::Button>(304, 189, kButtonWidth, ui.str(kAboutContinue), ui::return_accept);
  d.emplace<ui::GroupBox>(11, 11, 186, 218);
  d.run();
}

}  // namespace sh::editor
