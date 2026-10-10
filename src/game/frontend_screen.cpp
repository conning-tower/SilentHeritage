// The front end's shared machinery: the frame loop, the palette helpers of
// module misc, the screen pictures, the message boxes and the EXIT TO DOS?
// prompt.

#include <algorithm>
#include <array>

#include "assets/exe_image.h"
#include "assets/pcx.h"
#include "core/install.h"
#include "game/frontend.h"
#include "game/shpath.h"
#include "ui/controls.h"

namespace sh {

namespace {

// SH.EXE addresses.
constexpr std::uint32_t kExitTitle = 0xf73b8;      // "EXIT TO DOS?"
constexpr std::uint32_t kExitCells = 0x10c3e0;     // the NO / YES grid cells
constexpr std::uint32_t kMessageOk = 0xf43f1;      // "OK"

// The first DAC entry the grey-out changes (0..8 are the GUI colours).
constexpr int kFirstGreyedColour = 9;

// The size of a message box's buttons (0x1208b8, 0x120a50 / 0x120a8c).
constexpr int kBoxButtonWidth = 100;

}  // namespace

void FrontEnd::frame() {
  ui::Mouse& mouse = gui_.mouse();
  mouse.draw(screen_);
  host_.present(screen_, dac_);
  mouse.erase(screen_);
  host_.poll_input(gui_);
}

void FrontEnd::wait_clock(std::uint32_t ticks) {
  const std::uint32_t start = gui_.clock();
  while (gui_.clock() - start < ticks) {
    frame();
  }
}

void FrontEnd::palette_fade_in(const VgaPalette& target, std::uint32_t delay, int step) {
  // palette_fade_in (0x58bd0): every component moves up by `step` towards
  // the target (one that is above it drops at once); each step is shown
  // after the retrace wait, then `delay` clock ticks pass.
  VgaPalette cur = dac_;
  while (true) {
    bool done = true;
    for (std::size_t i = 0; i < cur.size(); i += 3) {
      if (cur[i] == target[i] && cur[i + 1] == target[i + 1] && cur[i + 2] == target[i + 2]) {
        continue;
      }
      done = false;
      for (std::size_t c = i; c < i + 3; ++c) {
        cur[c] = static_cast<std::uint8_t>(std::min(cur[c] + step, static_cast<int>(target[c])));
      }
    }
    if (done) {
      return;
    }
    dac_ = cur;
    const std::uint32_t start = gui_.clock();
    do {
      frame();
    } while (gui_.clock() - start < delay);
  }
}

void FrontEnd::palette_fade_out(std::uint32_t delay, int step) {
  // palette_fade_out (0x58d09): every component moves down by `step`.
  VgaPalette cur = dac_;
  while (true) {
    bool done = true;
    for (std::size_t i = 0; i < cur.size(); i += 3) {
      if (cur[i] == 0 && cur[i + 1] == 0 && cur[i + 2] == 0) {
        continue;
      }
      done = false;
      for (std::size_t c = i; c < i + 3; ++c) {
        cur[c] = static_cast<std::uint8_t>(std::max(cur[c] - step, 0));
      }
    }
    if (done) {
      return;
    }
    dac_ = cur;
    const std::uint32_t start = gui_.clock();
    do {
      frame();
    } while (gui_.clock() - start < delay);
  }
}

void FrontEnd::palette_grey_out(int percent, int last) {
  // palette_grey_out (0x58e29): colours 9..last are saved and replaced by
  // a grey of `percent` of their lightness (max + min) / 2.
  for (int i = kFirstGreyedColour; i <= last; ++i) {
    const auto at = static_cast<std::size_t>(i) * 3;
    std::copy_n(dac_.begin() + static_cast<std::ptrdiff_t>(at), 3,
                grey_saved_.begin() + static_cast<std::ptrdiff_t>(at));
    const int r = dac_[at];
    const int g = dac_[at + 1];
    const int b = dac_[at + 2];
    const int hi = std::max({r, g, b});
    const int lo = std::min({r, g, b});
    const int grey = std::max(percent * (hi - (hi - lo) / 2) / 100, 0);
    std::fill_n(dac_.begin() + static_cast<std::ptrdiff_t>(at), 3, static_cast<std::uint8_t>(grey));
  }
}

void FrontEnd::palette_restore_saved(int last) {
  // palette_restore_saved (0x58ef1)
  for (int i = kFirstGreyedColour; i <= last; ++i) {
    const auto at = static_cast<std::ptrdiff_t>(i) * 3;
    std::copy_n(grey_saved_.begin() + at, 3, dac_.begin() + at);
  }
}

void FrontEnd::load_screen_pcx(std::string_view name) {
  const PcxImage pcx = load_pcx(install_, paths_.path(GameDir::kArt, name));
  screen_.blit(pcx.image, 0, 0);
  palette_ = pcx.palette;
}

std::string FrontEnd::exe_string(std::uint32_t address) const { return std::string(exe_.c_string(address)); }

std::vector<std::string> FrontEnd::exe_strings(std::uint32_t address, int count) const {
  std::vector<std::string> out;
  for (int i = 0; i < count; ++i) {
    out.emplace_back(exe_.string_pointer(address + static_cast<std::uint32_t>(i) * 4));
  }
  return out;
}

void FrontEnd::add_exit_area(ui::Dialog& dialog) {
  // The key-only area every front-end dialog ends with: Alt-Q runs
  // exit_to_dos_prompt on the press message.
  dialog.emplace<ui::Area>(
      -1, -1, -1, -1,
      [this](ui::Dialog&, ui::Area&, ui::Msg msg, int, const ui::Event&) {
        if (msg == ui::Msg::kPress) {
          exit_to_dos_prompt();
        }
        return 0;
      },
      nullptr, 0x1000);
}

void FrontEnd::exit_to_dos_prompt() {
  // exit_to_dos_prompt (0x5894f) and the dialog 0x124350
  // (exit_to_dos_init_dialog, 0x56f04).
  exit_confirmed_ = false;
  palette_grey_out(70, 255);
  ui::Skin& skin = gui_.skin();
  gui_.set_skin(default_skin_);
  {
    ui::Dialog d(gui_, 211, 191, 219, 116, ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
    d.emplace<ui::TitleBar>(exe_string(kExitTitle));
    d.emplace<ui::GroupBox>(11, 44, 208, 105);
    std::vector<ui::GridCell> cells;
    for (std::uint32_t i = 0; i < 2; ++i) {
      const std::uint32_t a = kExitCells + i * 0x28;
      cells.push_back({.x = exe_.i32(a),
                       .y = exe_.i32(a + 4),
                       .w = exe_.i32(a + 8),
                       .label = std::string(exe_.string_pointer(a + 0xc)),
                       .up = exe_.i32(a + 0x10),
                       .down = exe_.i32(a + 0x14),
                       .left = exe_.i32(a + 0x18),
                       .right = exe_.i32(a + 0x1c),
                       .key = exe_.u32(a + 0x20),
                       .disabled = exe_.i32(a + 0x24) != 0});
    }
    // exit_to_dos_on_button (0x589ba)
    d.emplace<ui::ButtonGrid>(std::move(cells), [this](int cell) {
      if (cell == 1) {
        exit_confirmed_ = true;
      }
      return ui::kResultAccept;
    });
    d.run();
  }
  gui_.set_skin(skin);
  palette_restore_saved(255);
  if (exit_confirmed_) {
    throw QuitRequested();
  }
}

int FrontEnd::run_menu_dialog(ui::Dialog& dialog) {
  // menu_load_fonts (0x570b6) .. menu_free_fonts (0x57336), which puts the
  // default hooks back.
  gui_.set_skin(menu_skin_);
  const int result = dialog.run();
  gui_.set_skin(default_skin_);
  return result;
}

void FrontEnd::show_message_box(std::string_view title, std::string_view line1, std::string_view line2) {
  // show_message_box (0x4c38d), dialog 0x1207b4. The original also pauses
  // the simulation (0x129c00) around it.
  const Font& text_font = gui_res_.font1;
  const int width = std::max({text_font.text_width(line1), text_font.text_width(line2),
                              gui_res_.large.text_width(title), kBoxButtonWidth});
  const int w = width + 40;
  ui::Dialog d(gui_, (kScreenWidth - w) / 2, 200, w, 118, ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d.emplace<ui::TitleBar>(std::string(title));
  d.emplace<ui::Button>((w - kBoxButtonWidth) / 2, 83, kBoxButtonWidth, exe_string(kMessageOk), ui::return_accept);
  for (const auto& [y0, y1, text] : {std::tuple{47, 58, line1}, std::tuple{60, 68, line2}}) {
    auto& field = d.emplace<ui::TextField>(20, y0, width + 20, y1, [s = std::string(text)] { return s; },
                                           ui::FontSlot::kCustom, TextAlign::kCentre);
    field.fill = 6;
    field.custom = &gui_res_.font1;
  }
  palette_grey_out(70, 255);
  d.run();
  palette_restore_saved(255);
}

int FrontEnd::ask_yes_no(std::string_view title, std::string_view line1, std::string_view line2,
                         std::string_view yes, std::string_view no) {
  // ask_yes_no (0x4c670), dialog 0x12094c: NO (key N) on the left, YES
  // (key Y) on the right.
  const Font& text_font = gui_res_.font1;
  const int width = std::max({text_font.text_width(line1), text_font.text_width(line2),
                              gui_res_.large.text_width(title), 2 * kBoxButtonWidth});
  const int w = width + 40;
  ui::Dialog d(gui_, (kScreenWidth - w) / 2, 200, w, 118, ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d.emplace<ui::TitleBar>(std::string(title));
  d.emplace<ui::Button>(15, 83, kBoxButtonWidth, std::string(no), ui::return_cancel, 0xff4e);
  d.emplace<ui::Button>(width + 25 - kBoxButtonWidth, 83, kBoxButtonWidth, std::string(yes), ui::return_accept,
                        0xff59);
  for (const auto& [y0, y1, text] : {std::tuple{47, 58, line1}, std::tuple{60, 68, line2}}) {
    auto& field = d.emplace<ui::TextField>(20, y0, width + 20, y1, [s = std::string(text)] { return s; },
                                           ui::FontSlot::kCustom, TextAlign::kCentre);
    field.fill = 6;
    field.custom = &gui_res_.font1;
  }
  palette_grey_out(70, 255);
  const int result = d.run();
  palette_restore_saved(255);
  return result;
}

}  // namespace sh
