#include "game/frontend_subsel.h"

#include <algorithm>

#include "assets/exe_image.h"
#include "core/byte_reader.h"
#include "core/install.h"
#include "game/frontend.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/controls.h"

namespace sh {

namespace {

// SH.EXE addresses of the screen.
namespace addr {
constexpr std::uint32_t kPcx = 0x10e218;          // "M8.PCX"
constexpr std::uint32_t kPictures = 0x10e220;     // "SUBPICS.SHP"
constexpr std::uint32_t kTitle = 0xf9ccc;         // "SUB CLASS"
constexpr std::uint32_t kSelect = 0xf9cd6;        // "SELECT"
constexpr std::uint32_t kCancel = 0xf9cdd;        // "CANCEL"
constexpr std::uint32_t kStatistics = 0xf9d07;    // "STATISTICS"
constexpr std::uint32_t kDescription = 0xf9cfb;   // "DESCRIPTION"
constexpr std::uint32_t kClassSuffix = 0xf9dce;   // " CLASS"
}  // namespace addr

// SUBS.DAT fields the original reads by offset.
std::int32_t field(const SubClass& sub, std::size_t offset) {
  return static_cast<std::int32_t>(read_u32le(sub.raw, offset));
}

// The info panel: a 333 x 113 bitmap (0x127970) shown at (95, 257).
constexpr int kPanelWidth = 0x14d;
constexpr int kPanelHeight = 0x71;
// The panel and the picture relative to the dialog at (69, 39).
constexpr int kPanelX = 0x5f - 69;
constexpr int kPanelY = 0x101 - 39;
constexpr int kPictureX = 0x5f - 69;
constexpr int kPictureY = 0xa7 - 39;
// The list height `subsel_screen` sets, and the one the Alt-Q area leaves.
constexpr int kListHeight = 0xd3;
constexpr int kListHeightAfterExitPrompt = 0xcb;
// The description's start and line height (0x73c16).
constexpr int kDescriptionX = 0x11;
constexpr int kDescriptionY = 0x1e;
constexpr int kDescriptionLine = 0xb;

constexpr ui::DialogFlag kDialogFlags = static_cast<ui::DialogFlag>(ui::kDialogTitled | ui::kDialogFramed);

}  // namespace

bool sub_class_in_service(const SubClass& sub, std::int32_t year, std::int32_t month) {
  // encounter_sub_class_in_service (0xa1902)
  const std::int32_t y = year + 1930;
  const std::int32_t m = month + 1;
  const std::int32_t start_month = field(sub, 0x48);
  const std::int32_t start_year = field(sub, 0x4c);
  const std::int32_t end_month = field(sub, 0x50);
  const std::int32_t end_year = field(sub, 0x54);
  return start_year <= y && (y != start_year || start_month <= m) && y <= end_year &&
         (y != end_year || m <= end_month);
}

std::vector<const SubClass*> subsel_classes(std::span<const SubClass> subs, const SubClassFilter& filter) {
  std::vector<const SubClass*> out;
  for (const SubClass& s : subs) {
    if (!filter || filter(s)) {
      out.push_back(&s);
    }
  }
  return out;
}

std::vector<SubInfoText> sub_statistics(const SubClass& sub, const ExeImage& exe) {
  // subsel_draw_statistics (0x73322): labels at x 9, values at 0x8d, one
  // row every 9 pixels from 0x14.
  const auto s = [&exe](std::uint32_t address) { return std::string(exe.c_string(address)); };
  const auto n = [&sub](std::size_t offset) { return std::to_string(field(sub, offset)); };
  constexpr int kLabelX = 9;
  constexpr int kValueX = 0x8d;
  constexpr int kRow = 9;
  std::vector<SubInfoText> t;
  int y = 0x14;
  const auto row = [&](std::string label, std::string value) {
    t.push_back({kLabelX, y, std::move(label)});
    t.push_back({kValueX, y, std::move(value)});
    y += kRow;
  };
  row(s(0xf9de0), n(0xd0) + s(0xf9e0b) + n(0xd4) + s(0xf9e2d));
  row(s(0xf9e43), n(0x94) + s(0xf9e6c) + n(0x98) + s(0xf9e86) + n(0x9c));
  row(s(0xf9ea0), n(0x104) + s(0xf9ec2) + n(0x108) + s(0xf9ee2));
  row(s(0xf9ef7), n(0xdc) + s(0xf9f1d) + n(0xe0) + s(0xf9f3f));
  row(s(0xf9f55), sub.surface_speed_kts + s(0xf9f7e) + sub.submerged_speed_kts + s(0xf9fa0));
  row(s(0xf9fb6), n(0xa0));
  t.push_back({kLabelX, y, s(0xf9fdf)});
  t.push_back({kValueX, y, n(100) + s(0xfa004) + n(0x68) + s(0xfa01f)});
  if (field(sub, 0x6c) != -1) {
    y += kRow;
    t.push_back({kValueX, y, n(0x6c) + s(0xfa045) + n(0x70) + s(0xfa060)});
  }
  y += kRow;
  std::string tubes = n(0x74) + s(0xfa0a2);
  if (field(sub, 0x78) > 0) {
    tubes += n(0x78) + s(0xfa0c3);
  }
  tubes += s(0xfa0d5) +
           std::to_string(field(sub, 0x74) + field(sub, 0x78) + field(sub, 0x7c) + field(sub, 0x80));
  row(s(0xfa07c), std::move(tubes));
  row(s(0xfa0f7), n(0x88) + s(0xfa114) + n(0x8c) + s(0xfa12e));
  return t;
}

bool FrontEnd::subsel_screen(std::string& class_name, const SubClassFilter& filter) {
  // subsel_screen (0x72cfb), dialog 0x1279a4 (subsel_init_dialogs 0x72854).
  class_name.clear();
  load_screen_pcx(exe_string(addr::kPcx));
  gui_.set_skin(menu_skin_);
  ui::SkinMetrics& metrics = menu_skin_.mutable_metrics();
  const ui::SkinMetrics saved = metrics;
  metrics.list_height = kListHeight;
  const Shp pictures = Shp::load(install_, paths_.path(GameDir::kArt, exe_string(addr::kPictures)));
  const std::vector<SubClass> subs = parse_subs_dat(install_.read(paths_.path(GameDir::kData, "SUBS.DAT")));
  const std::vector<const SubClass*> classes = subsel_classes(subs, filter);
  const auto toggle_label = [this] {
    return exe_string(subsel_statistics_ ? addr::kDescription : addr::kStatistics);
  };
  palette_fade_out(0, 0x100);

  ui::Dialog d(gui_, 69, 39, 502, 401, kDialogFlags);
  ui::List* list = nullptr;
  // 0x730b3: the frames of the picture and the info panel; the list's
  // select callback `subsel_draw_sub_info` (0x73141) draws the selected
  // class into them, redrawn here with the frames.
  auto& info = d.emplace<ui::OwnerDraw>([&](const Viewport& gfx, ui::OwnerDraw&) {
    menu_skin_.draw_frame(gfx, 0x14, 0x7a, 0x159, 0x59);
    menu_skin_.draw_frame(gfx, 0x14, 0xd4, 0x159, 0x7d);
    d.add_dirty({0x14, 0x7a, 0x14 + 0x159 - 1, 0xd4 + 0x7d - 1});
    if (list == nullptr || list->selection < 0 || list->selection >= static_cast<int>(classes.size())) {
      return;
    }
    const SubClass& sub = *classes[static_cast<std::size_t>(list->selection)];
    draw_sprite_clipped(gfx, pictures.frame(sub.picture_frame), kPictureX, kPictureY);
    Image8 panel(kPanelWidth, kPanelHeight, 0xff);
    const Viewport pv = Viewport::full(panel);
    const Font& font = menu_res_.small;
    const GlyphColourTable& colours = gui_res_.identity;
    const std::string title = sub.display_name + exe_string(addr::kClassSuffix);
    const int w = font.text_width(title);
    const int x = (kPanelWidth - w) / 2;
    gui_draw_text(pv, x, 3, font, title, &colours);
    for (int i = x; i <= x + w - 1; ++i) {
      if (i >= 0 && i < kPanelWidth) {
        panel.set(i, 0xb, 0);
      }
    }
    if (subsel_statistics_) {
      for (const SubInfoText& t : sub_statistics(sub, exe_)) {
        gui_draw_text(pv, t.x, t.y, font, t.text, &colours);
      }
    } else {
      // 0x73c47: glyph by glyph; a '\n' moves to the next line and is
      // drawn there too.
      int cx = kDescriptionX;
      int cy = kDescriptionY;
      for (const char c : sub.description) {
        if (c == '\n') {
          cx = kDescriptionX;
          cy += kDescriptionLine;
        }
        cx += font_draw_char(pv, cx, cy, font, static_cast<std::uint8_t>(c), &colours);
      }
    }
    gfx.bitmap->blit(panel, gfx.x0 + kPanelX, gfx.y0 + kPanelY);
  });
  d.emplace<ui::TitleBar>(exe_string(addr::kTitle));
  list = &d.emplace<ui::List>(
      0x174, 0x7f, 0x68, [&classes] { return static_cast<int>(classes.size()); },
      [&classes](int row) { return classes[static_cast<std::size_t>(row)]->name; });
  list->no_scrollbar = true;
  bool info_changed = false;
  list->on_select = [&info_changed](int) { info_changed = true; };
  d.emplace<ui::Button>(0x158, 0x15c, 0x79, exe_string(addr::kSelect), ui::return_accept);
  auto& toggle = d.emplace<ui::Button>(0xbd, 0x15c, 0x79, toggle_label(), nullptr);
  // subsel_toggle_stats (0x730f0)
  toggle.on_click = [&](int) {
    subsel_statistics_ = !subsel_statistics_;
    toggle.label = toggle_label();
    toggle.dirty = true;
    info_changed = true;
    return 0;
  };
  d.set_back(d.emplace<ui::Button>(0x23, 0x15c, 0x79, exe_string(addr::kCancel), ui::return_cancel));
  // 0x73c97: the Alt-Q area also leaves the list shorter.
  d.emplace<ui::Area>(
      -1, -1, -1, -1,
      [this, &metrics](ui::Dialog&, ui::Area&, ui::Msg msg, int, const ui::Event&) {
        if (msg == ui::Msg::kPress) {
          exit_to_dos_prompt();
          metrics.list_height = kListHeightAfterExitPrompt;
        }
        return 0;
      },
      nullptr, 0x1000);
  // 0x732e3: the first class selected, then the fade-in. With
  // `Features::last_boat`, the class last chosen when the filter kept it.
  int initial = 0;
  if (features_.last_boat) {
    const auto last = std::ranges::find(classes, subsel_last_class_, &SubClass::name);
    if (last != classes.end()) {
      initial = static_cast<int>(last - classes.begin());
    }
  }
  bool fade_pending = true;  // 0x12799c
  d.set_idle([&](const Viewport&, int first) {
    if (first != 0) {
      list->selection = initial;
      list->dirty = true;
      info_changed = true;
      return;
    }
    if (fade_pending) {
      palette_fade_in(palette_, 0, 10);
      fade_pending = false;
    }
    if (info_changed) {
      info_changed = false;
      info.dirty = true;
    }
  });
  const int result = run_menu_dialog(d);
  metrics = saved;
  if (result == ui::kResultCancel || list->selection < 0 || list->selection >= static_cast<int>(classes.size())) {
    return false;
  }
  class_name = classes[static_cast<std::size_t>(list->selection)]->name;
  subsel_last_class_ = class_name;
  return true;
}

}  // namespace sh
