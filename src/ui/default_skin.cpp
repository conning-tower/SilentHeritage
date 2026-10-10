#include "ui/default_skin.h"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>

#include "core/fixed_math.h"
#include "gfx/sprite.h"
#include "ui/dialog.h"
#include "ui/draw.h"

namespace sh::ui {

namespace {

Rect to_rect(const TextRect& r) { return {r.x0, r.y0, r.x1, r.y1}; }

// Restores a glyph table entry when leaving a scope (the hooks patch the
// shared table around one text call).
class TablePatch {
 public:
  TablePatch(GlyphColourTable& table, std::uint8_t index, std::uint8_t value)
      : table_(table), index_(index), saved_(table[index]) {
    table_[index_] = value;
  }
  ~TablePatch() { table_[index_] = saved_; }
  TablePatch(const TablePatch&) = delete;
  TablePatch& operator=(const TablePatch&) = delete;

 private:
  GlyphColourTable& table_;
  std::uint8_t index_;
  std::uint8_t saved_;
};

}  // namespace

DefaultSkin::DefaultSkin(GuiResources& resources) : res_(resources), metrics_(SkinMetrics::from_game_exe()) {}

const Font& DefaultSkin::font(FontSlot slot, const Font* custom) const {
  switch (slot) {
    case FontSlot::kSmall:
      return res_.small;
    case FontSlot::kLarge:
      return res_.large;
    case FontSlot::kFont1:
      return res_.font1;
    case FontSlot::kGui1:
      return res_.gui1;
    case FontSlot::kCustom:
      break;
  }
  return *custom;
}

Rect DefaultSkin::text(const Viewport& gfx, int x, int y, const Font& f, std::string_view s,
                       const GlyphColourTable& table) {
  return to_rect(gui_draw_text(gfx, x, y, f, s, &table));
}

Rect DefaultSkin::raised_box(const Viewport& gfx, int x, int y, int w, int h, bool sunken, bool style) {
  const GuiColours& c = res_.colours;
  const std::uint8_t fill = style ? c.a[1] : c.b[1];
  std::uint8_t left = style ? c.a[3] : c.b[3];
  std::uint8_t right = style ? c.a[2] : c.b[2];
  if (sunken) {
    std::swap(left, right);
  }
  gui_raised_box(gfx, x, y, w, h, fill, left, right);
  return {x, y, x + w - 1, y + h - 1};
}

Rect DefaultSkin::sunken_box(const Viewport& gfx, int x, int y, int w, int h) {
  const GuiColours& c = res_.colours;
  gui_raised_box(gfx, x, y, w, h, c.b[1], c.b[2], c.b[3]);
  return {x, y, x + w - 1, y + h - 1};
}

Rect DefaultSkin::bevel_frame(const Viewport& gfx, int x, int y, int w, int h, int bx, int by, bool titled) {
  // gui_draw_bevel_frame (0x4300d)
  const GuiColours& c = res_.colours;
  const int border = metrics().border;
  const int x1 = x + w - 1;
  const int y1 = y + h - 1;
  if (titled) {
    fill_rect(gfx, x + border, y + border, x1 + border, y1 + border, c.b[0]);
  }
  gui_raised_box(gfx, x, y, w, h, c.a[1], c.a[3], c.a[2]);
  const int ix0 = x + bx + 1;
  const int iy0 = y + by + 1;
  const int ix1 = x1 - (bx + 1);
  const int iy1 = y1 - (by + 1);
  gui_raised_box(gfx, ix0, iy0, ix1 - ix0 + 1, iy1 - iy0 + 1, c.b[1], c.b[2], c.b[3]);
  return {x, y, x1 + (titled ? border : 0), y1 + (titled ? border : 0)};
}

void DefaultSkin::screen_caret(Dialog& dialog, int x, int y, int width, std::uint8_t colour) {
  const Viewport screen = Viewport::full(dialog.gui().screen());
  for (int row = 0; row < 2; ++row) {
    if (width > 0) {
      draw_line(screen, x, y + row, x + width - 1, y + row, colour);
    }
  }
}

Rect DefaultSkin::dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) {
  // gui_draw_dialog_frame (0x40c6d)
  return bevel_frame(gfx, x, y, w, h, 5, 5, titled);
}

Rect DefaultSkin::label(const Viewport& gfx, int x, int y, FontSlot slot, std::string_view s, bool use_colours,
                        const Font* custom) {
  // gui_draw_label (0x40c9d)
  return text(gfx, x, y, font(slot, custom), s, use_colours ? res_.table : res_.identity);
}

Rect DefaultSkin::group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view caption) {
  // gui_draw_group_box (0x40d5d)
  const GuiColours& c = res_.colours;
  const std::uint8_t dark = c.b[3];
  const std::uint8_t light = c.b[2];
  draw_line(gfx, x0, y0, x1, y0, dark);
  draw_line(gfx, x0 + 1, y0 + 1, x1 - 1, y0 + 1, dark);
  draw_line(gfx, x1, y0, x1, y1, light);
  draw_line(gfx, x1 - 1, y0 + 1, x1 - 1, y1 - 1, light);
  draw_line(gfx, x1, y1, x0, y1, light);
  draw_line(gfx, x1 - 1, y1 - 1, x0 + 1, y1 - 1, light);
  draw_line(gfx, x0, y1, x0, y0, dark);
  draw_line(gfx, x0 + 1, y1 - 1, x0 + 1, y0 + 1, dark);
  if (!caption.empty()) {
    const Font& f = font(FontSlot::kSmall);
    const int g = f.glyph_width('A') / 2;
    draw_line(gfx, x0 + 3, y0, x0 + 5 + f.text_width(caption), y0, c.b[1]);
    y0 -= g;
    text(gfx, x0 + 5, y0, f, caption, res_.identity);
  }
  return {x0, y0, x1, y1};
}

Rect DefaultSkin::line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) {
  // gui_draw_line (0x40d00)
  draw_line(gfx, x0, y0, x1, y1, res_.colours.b.at(static_cast<std::size_t>(colour)));
  return {x0, y0, x1, y1};
}

Rect DefaultSkin::title_bar(const Viewport& gfx, int w, int, std::string_view caption, bool has_close_box) {
  // gui_draw_title_bar (0x40eec)
  const GuiColours& c = res_.colours;
  fill_rect(gfx, 6, 6, w - 7, 37, c.b[1]);
  draw_line(gfx, 6, 38, w - 7, 38, c.b[2]);
  draw_line(gfx, 7, 39, w - 8, 39, c.b[2]);
  fill_rect(gfx, 10, 10, w - 11, 33, c.a[1]);
  const int off = has_close_box ? metrics().close_box_width : 0;
  const Font& f = font(FontSlot::kLarge);
  // Only the caption is reported dirty: gui_draw_text overwrites the rect.
  return text(gfx, off + (w - off - f.text_width(caption)) / 2, 15, f, caption, res_.table);
}

Rect DefaultSkin::close_box(const Viewport& gfx, int x, int y, bool pressed) {
  // gui_draw_close_box (0x41054)
  const GuiColours& c = res_.colours;
  const int x1 = x + metrics().close_box_width - 1;
  const int y1 = y + metrics().close_box_height - 1;
  fill_rect(gfx, x, y, x1, y1, c.a[2]);
  fill_rect(gfx, x + 2, y + 2, x1 - 2, y1 - 2, pressed ? c.a[2] : c.a[1]);
  return {x, y, x1, y1};
}

Rect DefaultSkin::text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, FontSlot slot,
                             std::string_view s, int fill, TextAlign align, bool use_colours, const Font* custom) {
  // gui_draw_text_field (0x4115d)
  const std::uint8_t colour = fill == -1 ? res_.colours.b[1] : static_cast<std::uint8_t>(fill);
  const int w = x1 - x0 + 1;
  gui_fill_rect(gfx, x0, y0, w, y1 - y0 + 1, colour);
  const Font& f = font(slot, custom);
  text(gfx, x0 + text_align_offset(f, s, x0, x1, align), y0, f, s, use_colours ? res_.table : res_.identity);
  return {x0, y0, x1, y1};
}

Rect DefaultSkin::button(const Viewport& gfx, int x, int y, int w, std::string_view s, bool pressed,
                         bool highlighted, bool focused, bool disabled) {
  // gui_draw_button (0x41289)
  const GuiColours& c = res_.colours;
  const Font& f = font(FontSlot::kSmall);
  const int bh = metrics().button_height;
  const int tw = f.text_width(s);
  const int tx = (w - tw) / 2;
  // A sunken 2-pixel frame around the button.
  const int sx = x - 2;
  const int sy = y - 2;
  const int fx1 = x + w + 1;
  const int fy1 = sy + bh + 4 - 1;
  draw_line(gfx, sx, sy, fx1, sy, c.b[3]);
  draw_line(gfx, sx + 1, sy + 1, fx1 - 1, sy + 1, c.b[3]);
  draw_line(gfx, fx1, sy, fx1, fy1, c.b[2]);
  draw_line(gfx, fx1 - 1, sy + 1, fx1 - 1, fy1 - 1, c.b[2]);
  draw_line(gfx, fx1, fy1, sx, fy1, c.b[2]);
  draw_line(gfx, fx1 - 1, fy1 - 1, sx + 1, fy1 - 1, c.b[2]);
  draw_line(gfx, sx, fy1, sx, sy, c.b[3]);
  draw_line(gfx, sx + 1, fy1 - 1, sx + 1, sy + 1, c.b[3]);
  raised_box(gfx, x, y, w, bh, pressed, !disabled);
  const int px = pressed ? x + 1 : x;
  const int py = pressed ? y + 1 : y;
  if (focused) {
    gui_focus_rect(gfx, px + tx - 2, py + 4, tw + 4, bh - 8, c.a[2], true);
  }
  {
    // A disabled label patches the GUI table but is drawn through the
    // identity table, as in the original.
    const std::uint8_t shade = c.a[2];
    const std::uint8_t patched = disabled ? c.b[2] : highlighted ? c.b[4] : res_.table[shade];
    TablePatch patch(res_.table, shade, patched);
    text(gfx, px + tx, py + 7, f, s, disabled ? res_.identity : res_.table);
  }
  return {sx, sy, sx + (w + 4) + 3, sy + (bh + 4) + 3};
}

Rect DefaultSkin::check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused,
                            std::string_view s) {
  // gui_draw_check_box (0x41504)
  const GuiColours& c = res_.colours;
  const int lw = metrics().check_box_label_width;
  const int h = metrics().check_box_height;
  if (!s.empty()) {
    const Font& f = font(FontSlot::kSmall);
    const int lx = x + 24;
    raised_box(gfx, lx, y, lw, h, pressed, checked);
    const int tw = f.text_width(s);
    const int tx = (lw - tw) / 2;
    text(gfx, (pressed ? lx + 1 : lx) + tx, (pressed ? y + 1 : y) + 4, f, s, res_.table);
    const std::uint8_t fc = checked ? (focused ? c.a[2] : c.a[1]) : (focused ? c.b[2] : c.b[1]);
    gui_focus_rect(gfx, lx + tx - 2, y + 2, tw + 4, h - 4, fc, true);
  }
  const int x0 = x;
  const int y0 = y;
  const int x1 = x + h - 1;
  const int y1 = y + h - 1;
  fill_rect(gfx, x0, y0, x1, y1, c.b[1]);
  draw_line(gfx, x0, y0, x1, y0, c.b[2]);
  draw_line(gfx, x0 + 1, y0 + 1, x1 - 1, y0 + 1, c.b[2]);
  draw_line(gfx, x1, y0, x1, y1, c.b[2]);
  draw_line(gfx, x1 - 1, y0 + 1, x1 - 1, y1 - 1, c.b[2]);
  draw_line(gfx, x1, y1, x0, y1, c.b[3]);
  draw_line(gfx, x1 - 1, y1 - 1, x0 + 1, y1 - 1, c.b[3]);
  draw_line(gfx, x0, y1, x0, y0, c.b[3]);
  draw_line(gfx, x0 + 1, y1 - 1, x0 + 1, y0 + 1, c.b[3]);
  if (checked) {
    draw_line(gfx, x0 + 2, y0 + 2, x1 - 2, y1 - 2, c.a[1]);
    draw_line(gfx, x0 + 2, y0 + 3, x1 - 3, y1 - 2, c.a[1]);
    draw_line(gfx, x0 + 3, y0 + 2, x1 - 2, y1 - 3, c.a[1]);
    draw_line(gfx, x1 - 2, y0 + 2, x0 + 2, y1 - 2, c.a[1]);
    draw_line(gfx, x1 - 2, y0 + 3, x0 + 3, y1 - 2, c.a[1]);
    draw_line(gfx, x1 - 3, y0 + 2, x0 + 2, y1 - 3, c.a[1]);
  }
  return {x0, y0, s.empty() ? x1 : x + 24 + lw, y1};
}

int DefaultSkin::check_box_width(std::string_view s) {
  // gui_check_box_width (0x418a5)
  return s.empty() ? metrics().check_box_height : metrics().check_box_label_width + 24;
}

Rect DefaultSkin::radio_button(const Viewport& gfx, int x, int y, bool pressed, bool selected, bool focused,
                               bool disabled, std::string_view s) {
  // gui_draw_radio_button (0x418c7)
  const GuiColours& c = res_.colours;
  const int lw = metrics().check_box_label_width;
  const int h = metrics().check_box_height;
  if (!s.empty()) {
    const Font& f = font(FontSlot::kSmall);
    const int lx = x + 24;
    raised_box(gfx, lx, y, lw, h, pressed || disabled, selected);
    const int tw = f.text_width(s);
    const int tx = (lw - tw) / 2;
    text(gfx, (pressed ? lx + 1 : lx) + tx, (pressed ? y + 1 : y) + 4, f, s,
         selected ? res_.table : res_.identity);
    const std::uint8_t fc = selected ? (focused ? c.a[2] : c.a[1]) : (focused ? c.b[2] : c.b[1]);
    gui_focus_rect(gfx, lx + tx - 2, y + 2, tw + 4, h - 4, fc, true);
  }
  // The hook adds the viewport origin to the centre and the ellipse
  // routine adds it again (an original bug, visible only with a target).
  const int ox = x + gfx.x0;
  const int oy = y + gfx.y0;
  const std::uint8_t c1 = pressed ? c.a[2] : c.b[1];
  fill_ellipse(gfx, ox + 8, oy + 8, 9, 9, c1);
  fill_ellipse(gfx, ox + 8, oy + 8, 8, 8, pressed ? c1 : (selected ? c.a[2] : c.b[2]));
  fill_ellipse(gfx, ox + 8, oy + 8, 7, 7, c.b[1]);
  fill_ellipse(gfx, ox + 8, oy + 8, 3, 3, selected ? c.a[2] : c.b[2]);
  return {ox - 1, oy - 1, s.empty() ? ox + 17 : x + 24 + lw + 1, oy + 20};
}

int DefaultSkin::radio_button_width(std::string_view s) {
  // gui_radio_button_width (0x41aeb)
  return check_box_width(s);
}

Rect DefaultSkin::cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused,
                               bool disabled, std::string_view s) {
  // gui_draw_cycle_button (0x41b0d)
  const GuiColours& c = res_.colours;
  const Font& f = font(FontSlot::kSmall);
  const int h = metrics().cycle_height;
  raised_box(gfx, x, y, w, h, pressed, false);
  const int tw = f.text_width(s);
  const int tx = (w - tw + 1) / 2;
  text(gfx, (pressed ? x + 1 : x) + tx, (pressed ? y + 1 : y) + 5, f, s, disabled ? res_.identity : res_.table);
  gui_focus_rect(gfx, x + tx - 2, y + 3, tw + 4, 16, focused ? c.b[2] : c.b[1], true);
  // y1 is read back after the focus rectangle overwrote it (original bug).
  return {x, y, x + w - 1, (y + 3 + 16 - 1) + h - 1};
}

Rect DefaultSkin::entry_box(const Viewport& gfx, int x, int y, int w) {
  // gui_draw_entry_box (0x41e19)
  const int h = metrics().entry_height;
  gui_fill_rect(gfx, x + 2, y + 2, w - 4, h - 4, res_.colours.b[0]);
  sunken_box(gfx, x, y, w, h);
  return {x, y, x + w - 1, y + h - 1};
}

Rect DefaultSkin::entry_text(const Viewport& gfx, int x, int y, int w, std::string_view s, bool) {
  // gui_draw_entry_text (0x41c06); its read-only patch of the identity
  // table has no visible effect.
  const int h = metrics().entry_height;
  gui_fill_rect(gfx, x + 2, y + 2, w - 4, h - 4, res_.colours.b[0]);
  text(gfx, x + 5, y + 5, font(FontSlot::kSmall), s, res_.table);
  return {x, y, x + w - 1, y + h - 1};
}

void DefaultSkin::entry_caret(Dialog& dialog, int x, int y, std::string_view s, int caret, bool on) {
  // gui_draw_entry_caret (0x41ca1)
  const Font& f = font(FontSlot::kSmall);
  const std::size_t n = static_cast<std::size_t>(caret);
  const std::uint8_t ch = n < s.size() ? static_cast<std::uint8_t>(s[n]) : 0;
  const int gw = f.glyph_width(ch != 0 ? ch : '_');
  const int sx = dialog.x() + x + 5 + f.text_width(s.substr(0, n));
  const int sy = dialog.y() + y + 5 + f.height() - 1;
  screen_caret(dialog, sx, sy, gw - 1, on ? res_.colours.a[3] : res_.colours.a[0]);
}

int DefaultSkin::entry_char_index_at(int click_x, int x, std::string_view s) {
  // gui_entry_char_index_at (0x41d93)
  const Font& f = font(FontSlot::kSmall);
  const int limit = click_x - (x + 5);
  int sum = 0;
  int i = 0;
  for (; i < static_cast<int>(s.size()) && s[static_cast<std::size_t>(i)] != '\0'; ++i) {
    sum += f.glyph_width(static_cast<std::uint8_t>(s[static_cast<std::size_t>(i)]));
    if (sum > limit) {
      break;
    }
  }
  return i;
}

Rect DefaultSkin::spin_entry_box(const Viewport& gfx, int x, int y, int w) {
  // gui_draw_spin_entry_box (0x41e0c)
  return entry_box(gfx, x, y, w + 14);
}

Rect DefaultSkin::spin_up_rect(int x, int y, int w) { return {x + w - 1, y + 1, x + w + 11, y + 10}; }

Rect DefaultSkin::spin_down_rect(int x, int y, int w) { return {x + w - 1, y + 11, x + w + 11, y + 20}; }

Rect DefaultSkin::spin_arrows(const Viewport& gfx, int x, int y, int w, int pressed, bool read_only) {
  // gui_draw_spin_arrows (0x41f0b)
  const GuiColours& c = res_.colours;
  const std::uint8_t ac = read_only ? c.b[2] : c.a[2];
  raised_box(gfx, x + w - 1, y + 2, 13, 10, pressed == 1, !read_only);
  int o = pressed == 1 ? 1 : 0;
  int ax = x + o + w;
  int ay = y + o;
  put_pixel(gfx, ax + 5, ay + 5, ac);
  draw_line(gfx, ax + 4, ay + 6, ax + 6, ay + 6, ac);
  draw_line(gfx, ax + 3, ay + 7, ax + 7, ay + 7, ac);
  raised_box(gfx, x + w - 1, y + 12, 13, 10, pressed == 2, !read_only);
  o = pressed == 2 ? 1 : 0;
  ax = x + o + w;
  ay = y + 10 + o;
  put_pixel(gfx, ax + 5, ay + 7, ac);
  draw_line(gfx, ax + 4, ay + 6, ax + 6, ay + 6, ac);
  draw_line(gfx, ax + 3, ay + 5, ax + 7, ay + 5, ac);
  return {x + w, y, x + w + 12, y + 19};
}

Rect DefaultSkin::textbox_frame(const Viewport& gfx, int x, int y, int w, int h) {
  // gui_draw_textbox_frame (0x420ab)
  sunken_box(gfx, x, y, w + 15, h + 4);
  return {x, y, x + w + 14, y + h + 3};
}

Rect DefaultSkin::textbox_background(const Viewport& gfx, int x, int y, int w, int h) {
  // gui_draw_textbox_background (0x4213b): colour B[*0x11df58], that
  // index being 0.
  gui_fill_rect(gfx, x + 2, y + 2, w + 11, h, res_.colours.b[0]);
  return {x, y, x + w + 14, y + h + 3};
}

void DefaultSkin::textbox_text(const Viewport& gfx, int x, int y, std::string_view s, bool) {
  // gui_draw_textbox_text (0x42193); its highlight patches the identity
  // table, without visible effect.
  text(gfx, x + metrics().textbox_origin_x, y + metrics().textbox_origin_y, font(FontSlot::kSmall), s,
       res_.table);
}

int DefaultSkin::textbox_char_width(std::uint8_t ch) { return font(FontSlot::kSmall).glyph_width(ch); }

void DefaultSkin::textbox_caret(Dialog& dialog, int x, int y, std::uint8_t ch, bool on) {
  // gui_draw_textbox_caret (0x4224b)
  const Font& f = font(FontSlot::kSmall);
  screen_caret(dialog, dialog.x() + x + 5, dialog.y() + y + 5 + f.height() - 2, f.glyph_width(ch) - 1,
               on ? res_.colours.a[3] : res_.colours.a[0]);
}

void DefaultSkin::textbox_text_origin(int& x, int& y) {
  // gui_textbox_text_origin (0x4222c)
  x += metrics().textbox_origin_x;
  y += metrics().textbox_origin_y;
}

Rect DefaultSkin::list_frame(const Viewport& gfx, int x, int y, int w) {
  // gui_draw_list_frame (0x42351)
  const int h = metrics().list_height;
  sunken_box(gfx, x, y, w, h);
  gui_fill_rect(gfx, x + 2, y + 2, w - 4, h - 4, res_.colours.b[0]);
  return {x, y, x + w - 1, y + h - 1};
}

Rect DefaultSkin::list_up_rect(int x, int y, int w) { return {x + w + 6, y + 2, x + w + 25, y + 21}; }

Rect DefaultSkin::list_down_rect(int x, int y, int w) {
  const int t = metrics().list_track;
  return {x + w + 6, y + t + 34, x + w + 25, y + t + 53};
}

Rect DefaultSkin::list_track_rect(int x, int y, int w) {
  return {x + w + 6, y + 28, x + w + 25, y + 28 + metrics().list_track - 1};
}

Rect DefaultSkin::list_item(const Viewport& gfx, int x, int y, int w, std::string_view s, bool selected) {
  // gui_draw_list_item (0x424a2)
  const int rh = metrics().list_row_height;
  if (selected) {
    gui_fill_rect(gfx, x + 2, y + 2, w - 4, rh, res_.colours.a[3]);
  }
  text(gfx, x + 5, y + metrics().list_text_y, font(FontSlot::kSmall), s, res_.table);
  return {x, y, x + w - 1, y + rh - 1};
}

Rect DefaultSkin::list_scrollbar(const Viewport& gfx, int x, int y, int w, int top, int count) {
  // gui_draw_list_scrollbar (0x42524)
  const int track = metrics().list_track;
  const int t = track - 2;
  int pos = 0;
  int len = t;
  if (count != 0) {
    const int half = count / 2;
    len = (metrics().list_rows * t + half) / count;
    pos = (top * t + half) / count;
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
  const int tx = x + w + 6;
  sunken_box(gfx, tx - 1, y + 27, 22, track + 2);
  raised_box(gfx, tx + 1, y + 29 + pos, 18, len, false, true);
  return {tx, y + 28, tx + 19, y + 28 + track - 1};
}

Rect DefaultSkin::list_arrow(const Viewport& gfx, int x, int y, int w, bool down, bool pressed) {
  // gui_draw_list_arrow (0x42656): a button through the hook table.
  const std::string glyph(1, down ? '\x1f' : '\x1e');
  return button(gfx, x + w + 6, down ? y + metrics().list_track + 32 : y, 20, glyph, pressed, false, false, false);
}

Rect DefaultSkin::list_focus(const Viewport& gfx, int x, int y, int w, bool focused) {
  // gui_draw_list_focus (0x426a7)
  const int h = metrics().list_height + 2;
  gui_focus_rect(gfx, x - 1, y - 1, w + 2, h, focused ? res_.colours.b[2] : res_.colours.b[1], false);
  return {x - 1, y - 1, x + w, y - 1 + h - 1};
}

Rect DefaultSkin::slider_frame(const Viewport& gfx, int x, int y, int w, bool) {
  // gui_draw_slider_frame (0x426e6); the rect is larger than the box.
  raised_box(gfx, x - 16, y - 8, w + 32, 30, false, false);
  return {x - 16, y - 8, x + w + 31, y + 29};
}

Rect DefaultSkin::slider_left_rect(int x, int y, int) { return {x - 14, y - 6, x - 3, y + 19}; }

Rect DefaultSkin::slider_right_rect(int x, int y, int w) { return {x + w + 2, y - 6, x + w + 13, y + 19}; }

Rect DefaultSkin::slider_track_rect(int x, int y, int w) { return {x, y, x + w - 1, y + 13}; }

Rect DefaultSkin::slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool) {
  // gui_draw_slider_track (0x4280e)
  const int pos = ((w - 26) * permille) / 1000;
  raised_box(gfx, x - 2, y - 2, w + 4, 18, true, false);
  raised_box(gfx, x + pos, y, 26, 14, false, true);
  return {x - 2, y - 2, x + w + 3, y + 17};
}

Rect DefaultSkin::slider_arrow(const Viewport& gfx, int x, int y, int w, bool right, bool pressed, bool) {
  // gui_draw_slider_arrow (0x428a8)
  const Rect r = right ? slider_right_rect(x, y, w) : slider_left_rect(x, y, w);
  const std::uint8_t colour = pressed ? res_.colours.a[4] : res_.colours.a[1];
  if (right) {
    fill_triangle(gfx, {r.x0 + 3, r.y0 + 7}, {r.x0 + 8, r.y0 + 12}, {r.x0 + 3, r.y0 + 17}, colour);
  } else {
    fill_triangle(gfx, {r.x0 + 3, r.y0 + 12}, {r.x0 + 8, r.y0 + 7}, {r.x0 + 8, r.y0 + 17}, colour);
  }
  return r;
}

Rect DefaultSkin::slider_focus(const Viewport& gfx, int x, int y, int w, bool focused) {
  // gui_draw_slider_focus (0x42986)
  gui_focus_rect(gfx, x - 13, y - 4, w + 26, 22, focused ? res_.colours.b[2] : res_.colours.b[1], true);
  return {x - 13, y - 4, x + w + 12, y + 17};
}

Rect DefaultSkin::dial(const Viewport& gfx, const Dial& d, std::string_view entry_text) {
  // gui_draw_dial (0x429bb)
  Rect rect{};
  if (res_.sprites.frame_count() > 0) {
    const SpriteFrame& frame = res_.sprites.frame(0);
    draw_sprite_clipped(gfx, frame, d.x, d.y);
    // sprite_frame_bbox: the tight box of the drawn pixels, x1 exclusive.
    const DecodedFrame decoded = frame.decode();
    int x0 = decoded.opaque.width();
    int y0 = decoded.opaque.height();
    int x1 = -1;
    int y1 = -1;
    for (int py = 0; py < decoded.opaque.height(); ++py) {
      for (int px = 0; px < decoded.opaque.width(); ++px) {
        if (decoded.opaque.at(px, py) != 0) {
          x0 = std::min(x0, px);
          y0 = std::min(y0, py);
          x1 = std::max(x1, px);
          y1 = std::max(y1, py);
        }
      }
    }
    if (x1 >= 0) {
      rect = {d.x + frame.x0 + x0, d.y + frame.y0 + y0, d.x + frame.x0 + x1 + 1, d.y + frame.y0 + y1};
    }
  }
  // The needle: (0, -21) from the centre rotated by the bearing
  // (point_rotate_scale, 0xae4d9, at scale 1).
  const auto tip = [&](int degrees) {
    const fx::Point2 p = fx::point_rotate_scale({d.x, d.y - 21}, {d.x, d.y}, Angle10(degrees * 10).normalized());
    return Point{p.x, p.y};
  };
  if (d.second != nullptr) {
    const Point p = tip(*d.second);
    draw_line(gfx, d.x, d.y, p.x, p.y, res_.colours.b[4]);
  }
  const Point p = tip(std::atoi(std::string(entry_text).c_str()));
  draw_line(gfx, d.x, d.y, p.x, p.y, d.alternate_colour ? res_.colours.b[2] : res_.colours.a[2]);
  return rect;
}

Rect DefaultSkin::grid_cell(const Viewport& gfx, const GridCell& cell, bool highlighted) {
  // gui_draw_grid_cell (0x42b0a)
  const int h = metrics().grid_cell_height;
  raised_box(gfx, cell.x, cell.y, cell.w, h, false, highlighted && !cell.disabled);
  const Font& f = font(FontSlot::kSmall);
  const Rect t = text(gfx, cell.x + (cell.w - f.text_width(cell.label)) / 2, cell.y + 5, f, cell.label,
                      cell.disabled ? res_.identity : res_.table);
  // y1 is read back after the text call overwrote it (original bug).
  return {cell.x, cell.y, cell.x + cell.w - 1, t.y1 + h - 1};
}

}  // namespace sh::ui
