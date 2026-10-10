#include "ui/menu_skin.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "gfx/sprite.h"
#include "ui/dialog.h"
#include "ui/draw.h"

namespace sh::ui {

namespace {

// MENU.SHP frames.
constexpr std::size_t kFrameButtonSmall = 0;  // 33x35, +1 pressed
constexpr std::size_t kFrameButtonMedium = 2;  // 121x35
constexpr std::size_t kFrameButtonLarge = 4;  // 198x35, also the check box label plate
constexpr std::size_t kFrameCheckOff = 6;
constexpr std::size_t kFrameCheckOn = 7;
constexpr std::size_t kFrameTitle = 8;   // 168x53 title plate
constexpr std::size_t kFrameCornerA = 9;
constexpr std::size_t kFrameCornerB = 10;

// Menu skin colours.
constexpr std::uint8_t kFocusOn = 0x68;
constexpr std::uint8_t kFocusOff = 0x51;

// Patches the identity table for one text call and restores it.
class TablePatches {
 public:
  explicit TablePatches(GlyphColourTable& table) : table_(table), saved_(table) {}
  ~TablePatches() { table_ = saved_; }
  TablePatches(const TablePatches&) = delete;
  TablePatches& operator=(const TablePatches&) = delete;

 private:
  GlyphColourTable& table_;
  GlyphColourTable saved_;
};

}  // namespace

MenuResources MenuResources::load(const Install& install) {
  // menu_load_fonts (0x570b6)
  return MenuResources{Shp::load(install, "ART/MENU.SHP"),     Font::load(install, "ART/MENU_B.FNT"),
                       Font::load(install, "ART/MENU_L.FNT"),  Font::load(install, "ART/MENU_M.FNT"),
                       Font::load(install, "ART/MENU_S.FNT"),  Font::load(install, "ART/MENU_H.FNT"),
                       load_pcx(install, "ART/MENUBGND.PCX").image};
}

void menu_pad_text_to_width(std::string& text, int width, const Font& font) {
  // menu_pad_text_to_width (0x57551)
  const std::size_t nl = text.rfind('\n');
  int w = font.text_width(std::string_view(text).substr(nl == std::string::npos ? 0 : nl));
  const int g1 = font.glyph_width(1);
  while (w < width) {
    const char c = width - w < g1 ? '\x02' : '\x01';
    text.push_back(c);
    w += font.glyph_width(static_cast<std::uint8_t>(c));
  }
}

MenuSkin::MenuSkin(GuiResources& gui, const MenuResources& menu) : DefaultSkin(gui), menu_(menu) {
  // menu_skin_install (0x57110): the metrics table 0x10c37a of SH.EXE (the
  // button height is a literal).
  const ExeImage& exe = game_exe();
  const auto get = [&](std::uint32_t address) { return static_cast<int>(static_cast<std::int16_t>(exe.u16(address))); };
  metrics_.button_height = 35;
  metrics_.check_box_height = get(0x10c37a);
  metrics_.check_box_label_width = get(0x10c37c);
  metrics_.list_row_height = get(0x10c37e);
  metrics_.list_rows = get(0x10c380);
  metrics_.list_height = metrics_.list_row_height * metrics_.list_rows + 2;
  metrics_.list_track = metrics_.list_row_height * metrics_.list_rows - 60;
  metrics_.entry_height = get(0x10c382);
  metrics_.cycle_height = get(0x10c384);
  check_exe_table(metrics_.list_rows > 0 && metrics_.list_row_height > 0, "menu metrics", 0x10c37a);
}

const Font& MenuSkin::font(FontSlot slot, const Font* custom) const {
  switch (slot) {
    case FontSlot::kSmall:
      return menu_.small;
    case FontSlot::kLarge:
      return menu_.large;
    case FontSlot::kFont1:
      return menu_.medium;
    case FontSlot::kGui1:
    case FontSlot::kCustom:
      break;
  }
  return DefaultSkin::font(slot, custom);
}

void MenuSkin::click() {
  if (click_sound) {
    click_sound();
  }
}

void MenuSkin::bevel(const Viewport& gfx, int x0, int y0, int x1, int y1, bool raised) {
  // menu_draw_bevel (0x58796)
  const std::uint8_t c1 = raised ? 0x4a : 0x67;
  const std::uint8_t c2 = raised ? 0x67 : 0x4a;
  const std::uint8_t c3 = raised ? 0x43 : 0x6f;
  const std::uint8_t c4 = raised ? 0x6f : 0x43;
  draw_line(gfx, x0, y0, x0, y1, c1);
  draw_line(gfx, x0, y0, x1, y0, c1);
  draw_line(gfx, x1, y0, x1, y1, c2);
  draw_line(gfx, x0, y1, x1, y1, c2);
  draw_line(gfx, x0 + 1, y0 + 1, x0 + 1, y1 - 1, c3);
  draw_line(gfx, x0 + 1, y0 + 1, x1 - 1, y0 + 1, c3);
  draw_line(gfx, x1 - 1, y0 + 1, x1 - 1, y1 - 1, c4);
  draw_line(gfx, x0 + 1, y1 - 1, x1 - 1, y1 - 1, c4);
}

void MenuSkin::frame(const Viewport& gfx, int x, int y, int w, int h) {
  // menu_draw_frame (0x588bb)
  const int x1 = x + w - 1;
  const int y1 = y + h - 1;
  bevel(gfx, x, y, x1, y1, true);
  draw_line(gfx, x + 5, y + 5, x + 5, y1 - 5, 0x61);
  draw_line(gfx, x + 5, y + 5, x1 - 5, y + 5, 0x6b);
  draw_line(gfx, x1 - 5, y + 5, x1 - 5, y1 - 5, 0x48);
  draw_line(gfx, x + 5, y1 - 5, x1 - 5, y1 - 5, 0x46);
}

Rect MenuSkin::dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) {
  // menu_skin_draw_dialog_frame (0x575ce)
  const int x1 = x + w - 1;
  const int y1 = y + h - 1;
  Rect r{x, y, x1, y1};
  if (titled) {
    const int b = metrics().border;
    fill_rect(gfx, x + b, y + b, x1 + b, y1 + b, res_.colours.b[0]);
    r.x1 += b;
    r.y1 += b;
  }
  // The MENUBGND.PCX area of the frame is copied to the same place.
  if (gfx.valid()) {
    const ClipRect clip = gfx.clip();
    for (int py = y; py <= y1; ++py) {
      for (int px = x; px <= x1; ++px) {
        const int bx = gfx.x0 + px;
        const int by = gfx.y0 + py;
        if (menu_.background.contains(px, py) && clip.contains(bx, by)) {
          gfx.bitmap->set(bx, by, menu_.background.at(px, py));
        }
      }
    }
  }
  draw_line(gfx, x, y, x, y1, 0x48);
  draw_line(gfx, x, y, x1, y, 0x40);
  draw_line(gfx, x1, y, x1, y1, 0x6a);
  draw_line(gfx, x, y1, x1, y1, 0x6a);
  draw_line(gfx, x + 6, y + 6, x + 6, y1 - 6, 0x6a);
  draw_line(gfx, x + 6, y + 6, x1 - 6, y + 6, 0x6a);
  draw_line(gfx, x1 - 6, y + 6, x1 - 6, y1 - 6, 0x48);
  draw_line(gfx, x + 6, y1 - 6, x1 - 6, y1 - 6, 0x48);
  draw_sprite_clipped(gfx, menu_.sprites.frame(kFrameCornerA), x + 14, y + 14);
  draw_sprite_clipped(gfx, menu_.sprites.frame(kFrameCornerB), x1 - 13, y + 14);
  draw_sprite_clipped(gfx, menu_.sprites.frame(kFrameCornerB), x + 14, y1 - 13);
  draw_sprite_clipped(gfx, menu_.sprites.frame(kFrameCornerA), x1 - 13, y1 - 13);
  return r;
}

Rect MenuSkin::group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view) {
  // menu_skin_draw_group_box (0x57922): the caption is not drawn.
  bevel(gfx, x0, y0, x1, y1, true);
  return {x0, y0, x1, y1};
}

Rect MenuSkin::line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) {
  // menu_skin_draw_line (0x578d9): the colour is used as is.
  draw_line(gfx, x0, y0, x1, y1, static_cast<std::uint8_t>(colour));
  return {x0, y0, x1, y1};
}

Rect MenuSkin::title_bar(const Viewport& gfx, int w, int, std::string_view caption, bool) {
  // menu_skin_draw_title (0x57955). The original upper-cases the caption
  // in place; here only the drawn text is.
  const SpriteFrame& plate = menu_.sprites.frame(kFrameTitle);
  draw_sprite_clipped(gfx, plate, w / 2, 10);
  const int ty = plate.height() + 18;
  std::string upper(caption);
  std::transform(upper.begin(), upper.end(), upper.begin(),
                 [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  gui_draw_text(gfx, (w - menu_.heading.text_width(upper)) / 2, ty, menu_.heading, upper, &res_.identity);
  return {0, 0, w - 1, ty + menu_.heading.height()};
}

Rect MenuSkin::text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, FontSlot slot, std::string_view s,
                          int fill, TextAlign align, bool use_colours, const Font* custom) {
  // menu_skin_draw_text_field (0x57a26): no fill by default.
  if (fill != -1) {
    gui_fill_rect(gfx, x0, y0, x1 - x0 + 1, y1 - y0 + 1, static_cast<std::uint8_t>(fill));
  }
  const Font& f = font(slot, custom);
  gui_draw_text(gfx, x0 + text_align_offset(f, s, x0, x1, align), y0, f, s,
                use_colours ? &res_.table : &res_.identity);
  return {x0, y0, x1, y1};
}

Rect MenuSkin::button(const Viewport& gfx, int x, int y, int w, std::string_view s, bool pressed, bool, bool focused,
                      bool disabled) {
  // menu_skin_draw_button (0x57b33): the sprite is chosen by the width.
  const std::size_t base = w == 121 ? kFrameButtonMedium : w == 198 ? kFrameButtonLarge : kFrameButtonSmall;
  if (pressed) {
    click();
  }
  draw_sprite_clipped(gfx, menu_.sprites.frame(base + (!disabled && pressed ? 1 : 0)), x, y);
  const int bh = metrics().button_height;
  const int off = (w - menu_.bold.text_width(s) + 1) / 2;
  gui_focus_rect(gfx, x + 3, y + 3, w - 6, bh - 6, focused ? kFocusOn : kFocusOff, false);
  if (pressed) {
    y += 2;
  }
  {
    TablePatches patches(res_.identity);
    if (disabled) {
      res_.identity[0x3d] = 0x5a;
      res_.identity[0x38] = 0x55;
      res_.identity[0x49] = 0x4b;
    }
    gui_draw_text(gfx, x + off, y + 8, menu_.bold, s, &res_.identity);
  }
  return {x, y, x + w - 1, y + bh - 1};
}

Rect MenuSkin::check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused,
                         std::string_view s) {
  // menu_skin_draw_checkbox (0x57c9c)
  const int bh = metrics().button_height;
  const int lw = metrics().check_box_label_width;
  const std::uint8_t fc = focused ? kFocusOn : kFocusOff;
  if (pressed) {
    click();
  }
  draw_sprite_clipped(gfx, menu_.sprites.frame(checked ? kFrameCheckOn : kFrameCheckOff), x, y);
  gui_focus_rect(gfx, x + 3, y + 3, 26, bh - 6, fc, false);
  const int lx = x + 34;
  draw_sprite_clipped(gfx, menu_.sprites.frame(kFrameButtonLarge + (pressed ? 1 : 0)), lx, y);
  const int off = (lw - menu_.bold.text_width(s) + 1) / 2;
  gui_focus_rect(gfx, lx + 3, y + 3, lw - 6, bh - 6, fc, false);
  if (!s.empty()) {
    gui_draw_text(gfx, lx + off, pressed ? y + 2 + 8 : y + 8, menu_.bold, s, &res_.table);
  }
  return {x, y, x + check_box_width(s), y + bh - 1};
}

int MenuSkin::check_box_width(std::string_view s) {
  // menu_skin_checkbox_width (0x57e14)
  return s.empty() ? 32 : metrics().check_box_label_width + 34;
}

Rect MenuSkin::cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused, bool disabled,
                            std::string_view s) {
  // menu_skin_draw_cycle_button (0x57e2c)
  const int h = metrics().cycle_height;
  gui_fill_rect(gfx, x, y, w, h, pressed ? 0x59 : 0x54);
  gui_focus_rect(gfx, x + 4, y + 4, w - 8, h - 8, focused ? kFocusOn : 0x54, false);
  bevel(gfx, x, y, x + w - 1, y + h - 1, !pressed);
  int ty = y;
  if (pressed) {
    click();
    ty += 2;
  }
  {
    TablePatches patches(res_.identity);
    if (disabled) {
      res_.identity[0x40] = 0x4f;
      res_.identity[0x68] = 0x56;
      res_.identity[0x6f] = 0x5a;
    } else {
      res_.identity[0x68] = 0x5f;
      res_.identity[0x40] = pressed ? 0x4e : 0x46;
    }
    gui_draw_text(gfx, x + (w - menu_.medium.text_width(s)) / 2, ty + 9, menu_.medium, s, &res_.identity);
  }
  return {x, y, x + w - 1, y + h - 1};
}

Rect MenuSkin::entry_box(const Viewport& gfx, int x, int y, int w) {
  // menu_skin_edit_frame (0x58388)
  const int h = metrics().entry_height;
  frame(gfx, x - 6, y - 6, w + 12, h + 12);
  gui_fill_rect(gfx, x, y, w, h, 0);
  return {x - 6, y - 6, x + w + 11, y + h + 11};
}

Rect MenuSkin::entry_text(const Viewport& gfx, int x, int y, int w, std::string_view s, bool) {
  // menu_skin_edit_text (0x58407)
  const int h = metrics().entry_height;
  gui_fill_rect(gfx, x, y, w, h, 0);
  gui_draw_text(gfx, x + 5, y + 5, menu_.large, s, &res_.identity);
  return {x, y, x + w - 1, y + h - 1};
}

void MenuSkin::entry_caret(Dialog& dialog, int x, int y, std::string_view s, int caret, bool on) {
  // menu_skin_edit_caret (0x58474)
  const std::size_t n = static_cast<std::size_t>(caret);
  const std::uint8_t ch = n < s.size() ? static_cast<std::uint8_t>(s[n]) : 0;
  const int g = menu_.large.glyph_width(ch != 0 ? ch : '_');
  const int sx = dialog.x() + x + 5 + menu_.large.text_width(s.substr(0, n));
  const int sy = dialog.y() + y + 5 + menu_.large.height() - 1;
  screen_caret(dialog, sx, sy, g - 1, on ? 0x0f : 0);
}

int MenuSkin::entry_char_index_at(int click_x, int x, std::string_view s) {
  // menu_skin_edit_fit_chars (0x58557)
  const int limit = click_x - (x + 5);
  int sum = 0;
  int i = 0;
  for (; i < static_cast<int>(s.size()) && s[static_cast<std::size_t>(i)] != '\0'; ++i) {
    sum += menu_.large.glyph_width(static_cast<std::uint8_t>(s[static_cast<std::size_t>(i)]));
    if (sum > limit) {
      break;
    }
  }
  return i;
}

Rect MenuSkin::textbox_frame(const Viewport& gfx, int x, int y, int w, int h) {
  // menu_skin_textbox_frame (0x5859c)
  frame(gfx, x - 4, y - 4, w + 28, h + 23);
  return {x - 4, y - 4, x + w + 27, y + h + 22};
}

Rect MenuSkin::textbox_background(const Viewport& gfx, int x, int y, int w, int h) {
  // menu_skin_textbox_background (0x585f8): 0xff is a real colour here.
  gui_fill_rect(gfx, x + 2, y + 2, w + 16, h + 11, 0xff);
  return {x, y, x + w + 19, y + h + 14};
}

void MenuSkin::textbox_text(const Viewport& gfx, int x, int y, std::string_view s, bool) {
  // menu_skin_textbox_line (0x58649); its highlight patches the identity
  // table, without visible effect.
  gui_draw_text(gfx, x + 10, y + 10, font(FontSlot::kSmall), s, &res_.table);
}

int MenuSkin::textbox_char_width(std::uint8_t ch) { return font(FontSlot::kSmall).glyph_width(ch); }

void MenuSkin::textbox_caret(Dialog& dialog, int x, int y, std::uint8_t ch, bool on) {
  // menu_skin_textbox_caret (0x586c7): inverted colours.
  const Font& f = font(FontSlot::kSmall);
  screen_caret(dialog, dialog.x() + x + 10, dialog.y() + y + 10 + f.height() - 6, f.glyph_width(ch) - 1,
               on ? 0 : 0xff);
}

void MenuSkin::textbox_text_origin(int& x, int& y) {
  // menu_skin_textbox_origin (0x586c0)
  x += 10;
  y += 10;
}

Rect MenuSkin::list_frame(const Viewport& gfx, int x, int y, int w) {
  // menu_skin_listbox_frame (0x57fe1)
  const int h = metrics().list_height;
  frame(gfx, x - 5, y - 5, w + 10, h + 10);
  gui_fill_rect(gfx, x + 1, y + 1, w - 2, h - 2, 0);
  return {x - 6, y - 6, x + w + 9, y + h + 9};
}

Rect MenuSkin::list_up_rect(int x, int y, int w) { return {x + w + 6, y - 6, x + w + 38, y + 28}; }

Rect MenuSkin::list_down_rect(int x, int y, int w) {
  const int t = metrics().list_track;
  return {x + w + 6, y + t + 33, x + w + 38, y + t + 67};
}

Rect MenuSkin::list_track_rect(int x, int y, int w) {
  return {x + w + 6, y + 31, x + w + 38, y + 31 + metrics().list_track - 1};
}

Rect MenuSkin::list_item(const Viewport& gfx, int x, int y, int w, std::string_view s, bool selected) {
  // menu_skin_listbox_item (0x5806e)
  const int rh = metrics().list_row_height;
  if (selected) {
    gui_fill_rect(gfx, x + 1, y + 1, w - 2, rh, 0x3f);
  }
  gui_draw_text(gfx, x + 4, y + 4, menu_.large, s, &res_.identity);
  return {x + 1, y + 1, x + w - 2, y + rh};
}

Rect MenuSkin::list_scrollbar(const Viewport& gfx, int x, int y, int w, int top, int count) {
  // menu_skin_listbox_scrollbar (0x58195)
  const int track = metrics().list_track;
  const int t = track - 4;
  int pos = 0;
  int len = t;
  if (count != 0) {
    len = (t * metrics().list_rows + count / 2) / count;
    pos = (top * t + count / 2) / count;
    if (len > t) {
      len = t;
    }
    if (len < 3) {
      len = 3;
    }
    if (pos + len > t) {
      pos = t - len;
    }
    if (pos < 0) {
      pos = 0;
    }
  }
  const int sx = x + w + 6;
  const int sy = y + 31;
  gui_fill_rect(gfx, sx, sy, 33, track, 0x65);
  bevel(gfx, sx, sy, sx + 32, sy + track - 1, false);
  gui_fill_rect(gfx, sx + 2, sy + pos + 2, 29, len, 0x54);
  bevel(gfx, sx + 2, sy + pos + 2, sx + 30, sy + pos + 2 + len - 1, true);
  return {sx, sy, sx + 32, sy + track - 1};
}

Rect MenuSkin::list_arrow(const Viewport& gfx, int x, int y, int w, bool down, bool pressed) {
  // menu_skin_listbox_arrow (0x582dd): a 33-wide button through the hook
  // table (which clicks a second time).
  if (pressed) {
    click();
  }
  const std::string glyph(1, down ? '\x1f' : '\x1e');
  return button(gfx, x + w + 6, down ? y + metrics().list_track + 33 : y - 6, 33, glyph, pressed, false, false,
                false);
}

Rect MenuSkin::list_focus(const Viewport& gfx, int x, int y, int w, bool focused) {
  // menu_skin_listbox_focus (0x58351)
  const int h = metrics().list_height + 4;
  gui_focus_rect(gfx, x - 2, y - 2, w + 4, h, focused ? kFocusOn : kFocusOff, false);
  return {x - 2, y - 2, x + w + 1, y - 2 + h - 1};
}

}  // namespace sh::ui
