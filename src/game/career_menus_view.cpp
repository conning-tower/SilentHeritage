// The career's record screens: BEST CAREERS with the career summary, TOP
// CAREER, TOP COMMANDERS, and VIEW CAREER with its awards and log book.

#include <algorithm>
#include <array>
#include <format>
#include <functional>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "core/install.h"
#include "game/career_menus.h"
#include "game/career_rules.h"
#include "game/career_save.h"
#include "game/career_ui.h"
#include "game/shpath.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"
#include "ui/draw.h"
#include "ui/menu_skin.h"

namespace sh {

namespace {

// SH.EXE addresses.
namespace addr {
constexpr std::uint32_t kMonths = 0x10e910;      // char*[12] "January"..
constexpr std::uint32_t kStatuses = 0x10eac0;    // char*[7] "", "In Port"..
constexpr std::uint32_t kShipTypes = 0x10ea04;   // {char*, char*}[24] "Aircraft Carrier"..
constexpr std::uint32_t kBaseYear = 0x10e3a4;    // i32 1930
// BEST CAREERS (dialog 0x117210) and TOP CAREER (0x117548).
constexpr std::uint32_t kBestPcx = 0x1032ec;     // "M8.PCX"
constexpr std::uint32_t kTopCareerPcx = 0x1032f4;  // "M7.PCX"
// TOP COMMANDERS (dialog 0x128d70).
constexpr std::uint32_t kBoardPcx = 0x10f480;    // "BBOARD.PCX"
constexpr std::uint32_t kBoardSprites = 0x10f48c;  // "AWARDS.SHP"
constexpr std::uint32_t kChalkFont = 0x10f498;   // "CHALK.FNT"
// VIEW CAREER (dialog 0x129f64), the awards (0x129d68) and the log book
// (0x12a5a8).
constexpr std::uint32_t kViewPcx = 0x10fca0;     // "M8.PCX"
constexpr std::uint32_t kViewTitle = 0x10fca8;   // "CMDR "
constexpr std::uint32_t kAwardsPcx = 0x10fb00;   // "wood.pcx"
constexpr std::uint32_t kAwardsSprites = 0x10fb0c;  // "awards.shp"
constexpr std::uint32_t kAwardPlaces = 0x10fb18;    // {x, y}[6]: the medals
constexpr std::uint32_t kAwardRepeats = 0x10fb48;   // {x, y}[6][5]: the stars
constexpr std::uint32_t kCitationPlaces = 0x10fc38;  // {x, y}[8]: NUC then PUC
constexpr std::uint32_t kLogPcx = 0x10fcc4;      // "LOG-2.PCX"
constexpr std::uint32_t kLogFont = 0x10fcd0;     // "SCRIPT.FNT"
constexpr std::uint32_t kLogSprites = 0x10fcdc;  // "awards.shp"
}  // namespace addr

constexpr unsigned kMenuFlags = ui::kDialogTitled | ui::kDialogFramed;

// The AWARDS.SHP frames: the six medals, NUC, PUC, the DONE button up and
// down, and the repeat star.
constexpr std::size_t kNucFrame = 6;
constexpr std::size_t kPucFrame = 7;
constexpr std::size_t kDoneFrame = 8;
constexpr std::size_t kDonePressedFrame = 9;
constexpr std::size_t kStarFrame = 10;

std::string month_name(const ExeImage& exe, std::int32_t month) {
  return std::string(exe.string_pointer(addr::kMonths + static_cast<std::uint32_t>(month) * 4));
}

std::string full_year(const ExeImage& exe, std::int32_t year) { return std::to_string(year + exe.i32(addr::kBaseYear)); }

// `int_to_padded_str` (0x5908d) with width 2.
std::string two_digits(std::int32_t v) { return std::format("{:02}", v); }

// "deg-mmH, deg-mmH" of a log entry.
std::string log_position(const PatrolLogEntry& e) {
  return std::to_string(e.latitude.degrees) + "-" + two_digits(e.latitude.minutes) + e.latitude.hemisphere + ", " +
         std::to_string(e.longitude.degrees) + "-" + two_digits(e.longitude.minutes) + e.longitude.hemisphere;
}

// The DONE sprite button of the board, awards and log book screens
// (`cycle_button_proc` 0x49472 with definitions 0x10f4a4, 0x10fc78,
// 0x10fce8): frame 8, 9 while held; a press clicks and ends the dialog
// (0x79031, 0x8e810, 0x90388 set its result to 1). It is the dialog's
// back control.
void add_done_button(CareerUi& ui, ui::Dialog& d, const Shp& sprites, int x0, int y0, int x1, int y1) {
  const auto draw = [&sprites, x0, y0](ui::Dialog& dialog, std::size_t frame) {
    const SpriteFrame& f = sprites.frame(frame);
    draw_sprite_clipped(dialog.gfx(), f, x0, y0);
    dialog.add_dirty({x0, y0, x0 + f.width() - 1, y0 + f.height() - 1});
  };
  auto& done = d.emplace<ui::Area>(
      x0, y0, x1, y1,
      [&ui, draw](ui::Dialog& dialog, ui::Area& self, ui::Msg msg, int, const ui::Event&) {
        switch (msg) {
          case ui::Msg::kPress:
            dialog.set_repeat_interval(0);
            draw(dialog, kDonePressedFrame);
            if (!self.pressed) {
              ui.host().play_sfx(2, 0x1e, -1);
              dialog.set_result(1);
            }
            return -1;
          case ui::Msg::kRelease:
          case ui::Msg::kPaint:
            draw(dialog, kDoneFrame);
            return -1;
          default:
            return 0;
        }
      },
      ui::Area::ActivateFn{});
  d.set_back(done);
}

Font load_art_font(CareerUi& ui, std::uint32_t name) {
  return Font::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(name)));
}

Shp load_art_sprites(CareerUi& ui, std::uint32_t name) {
  return Shp::load(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(name)));
}

}  // namespace

void best_careers_screen(CareerUi& ui, const BestCareersTable& table) {
  // best_careers_screen (0x1cae7), dialog 0x117210, with the list metrics
  // of best missions.
  const ExeImage& exe = ui.exe();
  ui.load_screen_pcx(ui.exe_string(addr::kBestPcx));
  ui.gui().set_skin(ui.menu_skin());
  ui::SkinMetrics& metrics = ui.menu_skin().mutable_metrics();
  const ui::SkinMetrics saved = metrics;
  metrics.list_rows = 7;
  metrics.list_height = 0x72;
  metrics.list_track = 0x32;
  ui.palette_fade_out(0, 0x100);
  const std::vector<std::string> rows = best_careers_rows(table, ui.menu_resources().large);
  std::vector<const BestCareerEntry*> used;
  for (const std::optional<BestCareerEntry>& e : table.entries) {
    if (e) {
      used.push_back(&*e);
    }
  }
  // best_careers_show_summary (0x1cbc4)
  const auto summary = [&](int row) {
    return career_end_summary(*used[static_cast<std::size_t>(row)], exe, ui.menu_resources().small);
  };

  ui::Dialog d(ui.gui(), 10, 12, 0x267, 0x1c2, kMenuFlags);
  d.emplace<ui::TitleBar>(ui.exe_string(0xf0bc6));
  d.set_back(d.emplace<ui::Button>(0xf5, 400, 0x79, ui.exe_string(0xf0bd3), ui::return_accept));
  auto& text = d.emplace<ui::TextBox>(0x12, 0x110, 0x209, 0x68, 10, 100, rows.empty() ? std::string() : summary(0));
  text.read_only = true;
  bool refresh = true;  // 0x116cf0
  // best_careers_scroll_up (0x1cca2) / best_careers_scroll_down (0x1ccc3)
  auto& up = d.emplace<ui::Button>(0x235, 0x10c, 0x21, ui.exe_string(0xf0bd6), [&](int) {
    text.scroll_up(d);
    text.dirty = true;
    refresh = true;
    return 0;
  }, 0, 0x14);
  auto& down = d.emplace<ui::Button>(0x235, 0x167, 0x21, ui.exe_string(0xf0bd8), [&](int) {
    text.scroll_down(d);
    text.dirty = true;
    refresh = true;
    return 0;
  }, 0, 0x14);
  d.emplace<ui::GroupBox>(0x235, 0x131, 0x255, 0x164);
  auto& list = d.emplace<ui::List>(
      0x14, 0x91, 0x21b, [&rows] { return static_cast<int>(rows.size()); },
      [&rows](int row) { return rows[static_cast<std::size_t>(row)]; });
  // best_careers_select_row (0x1cccf)
  list.on_select = [&](int row) {
    text.text = summary(row);
    text.top = 0;
    text.dirty = true;
    refresh = true;
  };
  d.emplace<ui::Label>(0x14, 0x7a, ui.exe_string(0xf0bda), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x43, 0x7a, ui.exe_string(0xf0bdf), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x18b, 0x7a, ui.exe_string(0xf0bf0), ui::FontSlot::kFont1);
  d.emplace<ui::Label>(0x1d6, 0x7a, ui.exe_string(0xf0bf6), ui::FontSlot::kFont1);
  ui.add_exit_area(d);
  // best_careers_dialog_draw (0x1cbdf): the arrows greyed by probing a
  // scroll each way.
  bool fade_pending = true;  // 0x116cec
  d.set_idle([&](const Viewport&, int first) {
    if (first == 0) {
      if (fade_pending) {
        ui.palette_fade_in(ui.palette(), 0, 10);
        fade_pending = false;
      }
    } else {
      list.selection = 0;
      list.dirty = true;
      refresh = true;
    }
    if (text.dirty || !refresh) {
      return;
    }
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
    refresh = false;
  });
  ui.run_menu_dialog(d);
  metrics = saved;
}

void top_career_screen(CareerUi& ui, const CareerRecord& record) {
  // top_career_enter_name (0x1dc29), dialog 0x117548.
  ui.load_screen_pcx(ui.exe_string(addr::kTopCareerPcx));
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  ui::Dialog d(ui.gui(), 0x118, 5, 0x157, 0xfb, kMenuFlags);
  d.emplace<ui::TitleBar>(ui.exe_string(0xf0bfe));
  // career_commander_title (0x1dc92)
  const std::string title = ui.exe_string(0xf1878) + record.name;
  d.emplace<ui::TextField>(9, 0x86, 0x14f, 0x96, [title] { return title; }, ui::FontSlot::kFont1, TextAlign::kCentre);
  d.emplace<ui::Label>(0x1b, 0x9a, ui.exe_string(0xf0c09), ui::FontSlot::kFont1);
  d.set_back(d.emplace<ui::Button>(0x72, 0xbf, 0x79, ui.exe_string(0xf0c3f), ui::return_accept));
  ui.add_exit_area(d);
  // top_career_fade_in (0x1dc7a)
  d.set_idle([&ui](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
  });
  ui.run_menu_dialog(d);
}

void top_commanders_screen(CareerUi& ui, const CareerRecord& record, std::span<const TopCommanderRow> rows,
                           std::span<const Commander> commanders) {
  // top_commanders_screen (0x78a3e), dialog 0x128d70, rows by
  // cmdlist_draw_rows (0x78cf2).
  const ExeImage& exe = ui.exe();
  ui.load_screen_pcx(ui.exe_string(addr::kBoardPcx));
  const Shp sprites = load_art_sprites(ui, addr::kBoardSprites);
  const Font chalk = load_art_font(ui, addr::kChalkFont);
  ui.gui().set_skin(ui.menu_skin());
  const Viewport vp = Viewport::full(ui.screen());
  const GlyphColourTable& identity = ui.gui_resources().identity;
  const std::string date = month_name(exe, record.month) + " " + std::to_string(record.day) + ", " +
                           full_year(exe, record.year);
  gui_draw_text(vp, (kScreenWidth - chalk.text_width(date)) / 2, 0x25, chalk, date, &identity);
  gui_draw_text(vp, 0x21, 0x4b, chalk, exe.c_string(0xfb0f5), &identity);
  gui_draw_text(vp, 0x1fc, 0x4b, chalk, exe.c_string(0xfb104), &identity);
  constexpr std::uint8_t kChalk = 0x12;
  ui::draw_line(vp, 0x21, 0x62, 0x25d, 0x62, kChalk);
  ui::draw_line(vp, 0x21, 0x65, 0x25d, 0x65, kChalk);
  ui::draw_line(vp, 0x1c7, 0x62, 0x1c7, 0x197, kChalk);
  ui::draw_line(vp, 0x1c8, 0x62, 0x1c8, 0x197, kChalk);
  for (std::size_t i = 0; i < 10; ++i) {
    const int y = 0x76 + static_cast<int>(i) * 0x1e;
    ui::draw_line(vp, 0x21, y + 0x14, 0x25d, y + 0x14, kChalk);
    if (i >= rows.size()) {
      continue;
    }
    const TopCommanderRow& row = rows[i];
    const std::string name = row.player ? record.name : commanders[static_cast<std::size_t>(row.commander)].name;
    const std::string tonnage = std::to_string(row.player ? record.tonnage : row.tonnage);
    gui_draw_text(vp, 0x21, y, chalk, name, &identity);
    gui_draw_text(vp, 0x25d - chalk.text_width(tonnage), y, chalk, tonnage, &identity);
  }
  ui.palette_fade_out(0, 0x100);
  ui::Dialog d(ui.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &ui.screen());
  add_done_button(ui, d, sprites, 0x11d, 0x1cd, 0x161, 0x1e0);
  ui.add_exit_area(d);
  // cmdlist_fade_in (0x79019)
  d.set_idle([&ui](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
  });
  ui.run_menu_dialog(d);
}

std::vector<std::string> view_career_fields(const CareerRecord& r, const ExeImage& exe) {
  // view_career_format_text (0x8fc23)
  return {month_name(exe, r.month) + " " + std::to_string(r.day) + " " + full_year(exe, r.year),
          std::string(exe.string_pointer(addr::kStatuses + static_cast<std::uint32_t>(r.status) * 4)),
          r.boat,
          r.class_title,
          r.home_port,
          r.location,
          std::to_string(r.patrols),
          std::to_string(r.tonnage),
          std::to_string(r.ships_sunk),
          std::to_string(r.score)};
}

namespace {

// `view_awards_screen` (0x8e776), dialog 0x129d68: the medals of the career
// on WOOD.PCX (0x8e81b).
void view_awards_screen(CareerUi& ui, const CareerRecord& r) {
  const ExeImage& exe = ui.exe();
  ui.load_screen_pcx(ui.exe_string(addr::kAwardsPcx));
  const Shp sprites = load_art_sprites(ui, addr::kAwardsSprites);
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  const Viewport vp = Viewport::full(ui.screen());
  const auto place = [&exe](std::uint32_t table, std::size_t i) {
    const std::uint32_t a = table + static_cast<std::uint32_t>(i) * 8;
    return std::pair{exe.i32(a), exe.i32(a + 4)};
  };
  // The six medals, then a star for each further award: the second at the
  // first repeat place itself, the following ones at the next places offset
  // by the first (so a third award moves the second star).
  for (std::size_t i = 0; i < 6; ++i) {
    const std::int32_t n = r.awards[i];
    if (n == 0) {
      continue;
    }
    const auto [x, y] = place(addr::kAwardPlaces, i);
    draw_sprite_clipped(vp, sprites.frame(i), x, y);
    if (n < 2) {
      continue;
    }
    const std::uint32_t repeats = addr::kAwardRepeats + static_cast<std::uint32_t>(i) * 0x28;
    const auto [x0, y0] = place(repeats, 0);
    std::int32_t k = 1;
    if (n == 2) {
      k = 2;
      draw_sprite_clipped(vp, sprites.frame(kStarFrame), x0, y0);
    }
    for (; k < n; ++k) {
      const auto [dx, dy] = place(repeats, static_cast<std::size_t>(k));
      draw_sprite_clipped(vp, sprites.frame(kStarFrame), dx + x0, dy + y0);
    }
  }
  std::size_t slot = 0;
  for (const auto& [award, frame] : {std::pair{Award::kNuc, kNucFrame}, std::pair{Award::kPuc, kPucFrame}}) {
    for (std::int32_t k = 0; k < r.award(award); ++k, ++slot) {
      const auto [x, y] = place(addr::kCitationPlaces, slot);
      draw_sprite_clipped(vp, sprites.frame(frame), x, y);
    }
  }
  ui::Dialog d(ui.gui(), 0, 0, 0x27f, 0x1df, ui::kDialogNoNavigation, &ui.screen());
  add_done_button(ui, d, sprites, 0x11d, 0x1ca, 0x161, 0x1dd);
  ui.add_exit_area(d);
  d.set_idle([&ui](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
    }
  });
  ui.run_menu_dialog(d);
}

// `logbook_page` (0x90393): page `page` of the career log over LOG-2.PCX
// (above y 0x1c9 unless `full`), nine sunk entries a page, the career
// tonnage on the last page. Returns the page count.
int draw_logbook_page(CareerUi& ui, const Font& font, std::span<const SunkListEntry> log, int page, bool full) {
  const ExeImage& exe = ui.exe();
  const PcxImage pcx = load_pcx(ui.install(), ui.paths().path(GameDir::kArt, ui.exe_string(addr::kLogPcx)));
  Image8& screen = ui.screen();
  const int rows = std::min(full ? 0x1df : 0x1c9, pcx.image.height());
  for (int y = 0; y < rows; ++y) {
    std::ranges::copy(pcx.image.row(y), screen.row(y).begin());
  }
  ui.palette() = pcx.palette;
  const Viewport vp{&screen, 0, 0, screen.width() - 1, rows - 1};
  const GlyphColourTable& identity = ui.gui_resources().identity;
  constexpr int kLeft = 0x32;
  constexpr int kRight = 0x21a + kLeft;
  const auto right = [&](const std::string& s, int y) { gui_draw_text(vp, kRight - font.text_width(s), y, font, s, &identity); };
  int y = 0x23;
  if (page != 0) {
    gui_draw_text(vp, kLeft, y, font, exe.c_string(0xfb83a), &identity);
    gui_draw_text(vp, 0xaa, y, font, exe.c_string(0xfb844), &identity);
    gui_draw_text(vp, 0x14a, y, font, exe.c_string(0xfb84d), &identity);
    gui_draw_text(vp, 0x218, y, font, exe.c_string(0xfb859), &identity);
    ui::draw_line(vp, kLeft, 0x2d, 0x24c, 0x2d, 0);
    y = 0x3d;
  }
  int y2 = y + 0xd;
  std::int32_t total = 0;
  int pages = 1;
  int on_page = 0;
  for (const SunkListEntry& e : log) {
    if (e.status != SunkStatus::kSunk) {
      continue;
    }
    if (on_page > 8) {
      on_page = 0;
      ++pages;
    }
    total += e.log.tonnage;
    if (pages == page) {
      const PatrolLogEntry& l = e.log;
      gui_draw_text(vp, kLeft, y, font,
                    month_name(exe, l.month) + " " + std::to_string(l.day) + " " + full_year(exe, l.year), &identity);
      gui_draw_text(vp, kLeft, y2, font, two_digits(l.hour) + ":" + two_digits(l.minute), &identity);
      gui_draw_text(vp, 0xaa, y, font, log_position(l), &identity);
      if (l.type == PatrolLogEntry::kRescuedPilot) {
        gui_draw_text(vp, 0x14a, y, font, exe.c_string(0xfb9f5), &identity);
      } else if (l.type == PatrolLogEntry::kPhotoRecon) {
        gui_draw_text(vp, 0x14a, y, font, exe.c_string(0xfba03), &identity);
        gui_draw_text(vp, 0x14a, y2, font, l.name, &identity);
      } else {
        gui_draw_text(vp, 0x14a, y, font, exe.string_pointer(addr::kShipTypes + static_cast<std::uint32_t>(l.type) * 8),
                      &identity);
        if (!l.name.empty()) {
          // " Class" except for submarines (type 11).
          gui_draw_text(vp, 0x14a, y2, font, l.type != 0xb ? l.name + std::string(exe.c_string(0xfba3d)) : l.name, &identity);
        }
      }
      y2 += 0x27;
      right(std::to_string(l.tonnage), y);
      y += 0x27;
    }
    ++on_page;
  }
  // A full last page adds an empty one (with the career tonnage only).
  if (on_page > 8) {
    ++pages;
  }
  if (page != 0) {
    if (page == pages) {
      gui_draw_text(vp, 0x118 + kLeft, y, font, exe.c_string(0xfba44), &identity);
      right(std::to_string(total), y);
    }
    right(std::string(exe.c_string(0xfba6b)) + std::to_string(page) + std::string(exe.c_string(0xfba9f)) +
              std::to_string(pages),
          0x19c);
  }
  return pages;
}

// `view_logbook_screen` (0x902b4), dialog 0x12a5a8: the log book pages,
// turned by clicking the left or right page.
void view_logbook_screen(CareerUi& ui, std::span<const SunkListEntry> log) {
  const Shp sprites = load_art_sprites(ui, addr::kLogSprites);
  ui.gui().set_skin(ui.menu_skin());
  ui.palette_fade_out(0, 0x100);
  const Font font = load_art_font(ui, addr::kLogFont);
  int page = 1;  // 0x119280
  int pages = draw_logbook_page(ui, font, log, page, true);  // 0x12a5a4
  bool turned = false;  // 0x12a59c
  ui::Dialog d(ui.gui(), 0, 0, 0x27f, 0x1df, 0, &ui.screen());
  ui::Mouse& mouse = ui.gui().mouse();
  const auto add_turn = [&](int x0, int x1, std::function<bool()> can, int step, std::size_t cursor) {
    auto& area = d.emplace<ui::Area>(
        x0, 0x15, x1, 0x1a8,
        [&turned, &page, can, step](ui::Dialog&, ui::Area& self, ui::Msg msg, int, const ui::Event&) {
          // hotkey_proc_simple: the page turns once per press.
          if (msg == ui::Msg::kPress && !self.pressed && can()) {
            turned = true;
            page += step;
          }
          return 0;
        },
        ui::Area::ActivateFn{});
    // logbook_hover_prev (0x90b01) / logbook_hover_next (0x90adb)
    area.on_hover = [&ui, &mouse, can, cursor](bool on, int, const ui::Event&) {
      mouse.set_cursor(ui.cursors(), on && can() ? cursor : ui.gui().dialog_cursor_frame());
    };
  };
  add_turn(0x29, 0x13f, [&page] { return page > 1; }, -1, 0xe);
  add_turn(0x140, 600, [&page, &pages] { return page < pages; }, 1, 0xf);
  add_done_button(ui, d, sprites, 0x11d, 0x1ca, 0x161, 0x1dd);
  ui.add_exit_area(d);
  // 0x90351
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      ui.palette_fade_in(ui.palette(), 0, 10);
      return;
    }
    if (turned) {
      d.invalidate();
      pages = draw_logbook_page(ui, font, log, page, false);
      turned = false;
    }
  });
  ui.run_menu_dialog(d);
}

}  // namespace

void view_career(CareerUi& ui, const CareerRecord& r, std::span<const SunkListEntry> log) {
  // view_career (0x8facf) and view_career_menu_loop (0x8fb59).
  const ExeImage& exe = ui.exe();
  while (true) {
    // view_career_screen (0x8fb96), dialog 0x129f64.
    ui.load_screen_pcx(ui.exe_string(addr::kViewPcx));
    ui.gui().set_skin(ui.menu_skin());
    ui.palette_fade_out(0, 0x100);
    const std::vector<std::string> fields = view_career_fields(r, exe);
    ui::Dialog d(ui.gui(), 0xb, 0xe, 0x26b, 0x1c7, kMenuFlags);
    d.emplace<ui::TitleBar>(ui.exe_string(addr::kViewTitle) + r.name);
    d.emplace<ui::GroupBox>(8, 0x7c, 0x261, 0x11a);
    d.emplace<ui::GroupBox>(8, 0x11d, 0x261, 0x153);
    constexpr std::array<std::tuple<int, int, std::uint32_t>, 10> kLabels{{
        {0x2e, 0xa0, 0xfb5f0},
        {0x142, 0xa0, 0xfb5f6},
        {0x13, 0xc6, 0xfb5fe},
        {0x133, 0xc6, 0xfb608},
        {0xf, 0xea, 0xfb613},
        {0x137, 0xea, 0xfb61e},
        {0x3b, 0x132, 0xfb628},
        {0xb6, 0x132, 0xfb631},
        {0x134, 0x132, 0xfb63a},
        {0x1b9, 0x132, 0xfb646},
    }};
    for (const auto& [x, y, label] : kLabels) {
      d.emplace<ui::Label>(x, y, ui.exe_string(label), ui::FontSlot::kFont1);
    }
    // view_career_update_buttons (0x8fe88)
    auto& awards = d.emplace<ui::Button>(0x26, 0x15a, 0xc6, ui.exe_string(0xfb64d), [](int) { return 2; });
    awards.disabled = std::ranges::none_of(r.awards, [](std::int32_t n) { return n != 0; });
    auto& logbook = d.emplace<ui::Button>(0x179, 0x15a, 0xc6, ui.exe_string(0xfb659), [](int) { return 3; });
    logbook.disabled = r.log_count == 0 && (r.on_patrol == 0 || r.patrol_log_count == 0);
    d.set_back(d.emplace<ui::Button>(0xfa, 0x196, 0x79, ui.exe_string(0xfb667), [](int) { return 1; }));
    // The read-only fields (x, y, w, size).
    constexpr std::array<std::tuple<int, int, int, int>, 10> kFields{{
        {0x53, 0x9a, 0x96, 0x13},
        {0x172, 0x9a, 0x95, 0x13},
        {0x53, 0xc0, 0xd3, 0x1e},
        {0x172, 0xc0, 0x95, 0x1e},
        {0x53, 0xe6, 0xd3, 0x1e},
        {0x172, 0xe6, 0xe3, 0x13},
        {0x70, 300, 0x34, 0x13},
        {0xee, 300, 0x34, 0x13},
        {0x175, 300, 0x34, 0x13},
        {0x1e5, 300, 0x34, 0x13},
    }};
    for (std::size_t i = 0; i < kFields.size(); ++i) {
      const auto& [x, y, w, size] = kFields[i];
      auto& field = d.emplace<ui::TextEntry>(x, y, w, size, fields[i].substr(0, static_cast<std::size_t>(size - 1)),
                                             [](std::string&, int) { return 0; });
      field.read_only = true;
    }
    ui.add_exit_area(d);
    // view_career_fade_in (0x8fbf9)
    d.set_idle([&ui](const Viewport&, int first) {
      if (first != 0) {
        ui.palette_fade_in(ui.palette(), 0, 10);
      }
    });
    const int result = ui.run_menu_dialog(d);
    if (result == 1) {
      return;
    }
    if (result == 2) {
      view_awards_screen(ui, r);
    } else if (result == 3) {
      view_logbook_screen(ui, log);
    }
  }
}

}  // namespace sh
