// The MENU skin of the front-end screens (`menu_load_fonts` 0x570b6,
// `menu_skin_install` 0x57110, hooks 0x575ce..0x588bb): MENU.SHP buttons
// and check boxes, MENUBGND.PCX dialog backgrounds, MENU_*.FNT text, and
// the larger metrics. Hooks it does not replace (label, close box, radio
// buttons, spin entry, sliders, dial, grid) stay the default ones, drawing
// with the swapped fonts and metrics.
//
//   ui::MenuResources menu = ui::MenuResources::load(install);
//   ui::MenuSkin skin(gui_resources, menu);
//   gui.set_skin(skin);          // menu_skin_install
//   ...
//   gui.set_skin(default_skin);  // menu_skin_restore_default
#pragma once

#include <functional>
#include <string>

#include "assets/pcx.h"
#include "ui/default_skin.h"

namespace sh::ui {

// The files of `menu_load_fonts`.
struct MenuResources {
  Shp sprites;     // ART/MENU.SHP (0x12430c)
  Font bold;       // ART/MENU_B.FNT (0x124310): buttons and check boxes
  Font large;      // ART/MENU_L.FNT (0x124314): font 1, lists and entries
  Font medium;     // ART/MENU_M.FNT (0x124318): font 2, cycle buttons
  Font small;      // ART/MENU_S.FNT (0x12431c): font 0, text boxes
  Font heading;    // ART/MENU_H.FNT (0x124320): titles
  // ART/MENUBGND.PCX, which the frame hook reloads on every call.
  Image8 background;

  // Loads the files from `install`; throws `DataError` if one is missing.
  static MenuResources load(const Install& install);
};

// Appends the filler glyphs 1 (and 2 for the last pixels) to `text` until
// its last line, measured from its last '\n' included, is `width` pixels
// wide in `font` (`menu_pad_text_to_width`, 0x57551).
void menu_pad_text_to_width(std::string& text, int width, const Font& font);

class MenuSkin : public DefaultSkin {
 public:
  // Both resources must outlive the skin.
  MenuSkin(GuiResources& gui, const MenuResources& menu);

  // The click sound of the buttons, check boxes, cycle buttons and list
  // arrows when pressed (`sound_play_sfx(1, 5, -1)`); none by default.
  std::function<void()> click_sound;

  const Font& font(FontSlot slot, const Font* custom = nullptr) const override;

  // `menu_draw_frame` (0x588bb): the bevelled frame the menu screens also
  // draw around their pictures.
  void draw_frame(const Viewport& gfx, int x, int y, int w, int h) { frame(gfx, x, y, w, h); }

  Rect dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) override;
  Rect group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view caption) override;
  Rect line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) override;
  Rect title_bar(const Viewport& gfx, int w, int h, std::string_view text, bool has_close_box) override;
  Rect text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, FontSlot font, std::string_view text,
                  int fill, TextAlign align, bool use_colours, const Font* custom) override;
  Rect button(const Viewport& gfx, int x, int y, int w, std::string_view label, bool pressed, bool highlighted,
              bool focused, bool disabled) override;
  Rect check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused,
                 std::string_view label) override;
  int check_box_width(std::string_view label) override;
  Rect cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused, bool disabled,
                    std::string_view text) override;
  Rect entry_box(const Viewport& gfx, int x, int y, int w) override;
  Rect entry_text(const Viewport& gfx, int x, int y, int w, std::string_view text, bool read_only) override;
  void entry_caret(Dialog& dialog, int x, int y, std::string_view text, int caret, bool on) override;
  int entry_char_index_at(int click_x, int x, std::string_view text) override;
  Rect textbox_frame(const Viewport& gfx, int x, int y, int w, int h) override;
  Rect textbox_background(const Viewport& gfx, int x, int y, int w, int h) override;
  void textbox_text(const Viewport& gfx, int x, int y, std::string_view text, bool highlight) override;
  int textbox_char_width(std::uint8_t ch) override;
  void textbox_caret(Dialog& dialog, int x, int y, std::uint8_t ch, bool on) override;
  void textbox_text_origin(int& x, int& y) override;
  Rect list_frame(const Viewport& gfx, int x, int y, int w) override;
  Rect list_up_rect(int x, int y, int w) override;
  Rect list_down_rect(int x, int y, int w) override;
  Rect list_track_rect(int x, int y, int w) override;
  Rect list_item(const Viewport& gfx, int x, int y, int w, std::string_view text, bool selected) override;
  Rect list_scrollbar(const Viewport& gfx, int x, int y, int w, int top, int count) override;
  Rect list_arrow(const Viewport& gfx, int x, int y, int w, bool down, bool pressed) override;
  Rect list_focus(const Viewport& gfx, int x, int y, int w, bool focused) override;

 protected:
  // `menu_draw_bevel` (0x58796): the double bevel of (x0, y0)-(x1, y1),
  // raised or sunken (also drawn by the interview slider).
  void bevel(const Viewport& gfx, int x0, int y0, int x1, int y1, bool raised);

 private:
  // `menu_draw_frame` (0x588bb).
  void frame(const Viewport& gfx, int x, int y, int w, int h);
  void click();

  const MenuResources& menu_;
};

}  // namespace sh::ui
