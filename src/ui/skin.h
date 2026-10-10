// The renderer hook table of the dialog toolkit (0x108974..0x108a20) as an
// interface, its metrics (0x108a24..0x108a88) and the GUI resources that
// `gui_init` loads (fonts, GUISPR.SHP, colour configuration).
//
// Every drawing hook draws into the dialog's gfx viewport (coordinates are
// relative to the dialog) and returns the rectangle it touched (the static
// rect 0x11dee4), which the caller adds to the dialog's dirty list.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "assets/fnt.h"
#include "assets/shp.h"
#include "gfx/text.h"
#include "gfx/viewport.h"

namespace sh {
class Install;
}

namespace sh::ui {

// An inclusive rectangle (x0, y0, x1, y1), as the hooks return them.
struct Rect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  bool operator==(const Rect&) const = default;
};

// The colour configuration of `gui_init` (config words 5..0x10 at
// 0x102fa0): six (index, value) pairs, kept as the two arrays
// A = 0x11dcb0[6] (the values) and B = 0x11dcc8[6] (the indexes). The pairs
// recolour the glyph shades of the GUI fonts (`gui_colour_table`) and the
// arrays double as the palette colours of the default skin.
struct GuiColours {
  std::array<std::uint8_t, 6> a{0, 3, 2, 4, 8, 0};  // 0x11dcb0..0x11dcc4
  std::array<std::uint8_t, 6> b{0, 6, 7, 5, 8, 0};  // 0x11dcc8..0x11dcdc
};

// The fonts, sprites and glyph tables of the GUI (`load_gui_fonts` /
// `gui_init`).
struct GuiResources {
  Font small;    // ART/GUI_S.FNT, font 0 (0x11dc98): the default font
  Font large;    // ART/GUI_L.FNT, font 1 (0x11dc94): titles
  Font font1;    // ART/FONT1.FNT, font 2 (0x11dc9c)
  Font gui1;     // ART/GUI_1.FNT, font 3 (0x11dca0)
  Shp sprites;   // ART/GUISPR.SHP (0x11dcac)
  GuiColours colours;
  // The glyph table 0x11dce0 (`gui_colour_table`), which hooks patch
  // temporarily, and the identity table 0x11dde0.
  GlyphColourTable table = gui_colour_table();
  GlyphColourTable identity = identity_table();

  // Returns the identity glyph table.
  static GlyphColourTable identity_table();
  // Loads the files from `install`; throws `DataError` if one is missing.
  static GuiResources load(const Install& install);
};

// The skin metrics (shorts 0x108a24..0x108a88 of SH.EXE).
struct SkinMetrics {
  int menu_bar_width = 0;  // 0x108a24
  int menu_bar_height = 0;  // 0x108a26
  // 0x108a28 (3, a menu bevel) is read by neither SH.EXE nor SE.EXE: the menu
  // bar and the popups pass a literal 3.
  int popup_width = 0;  // 0x108a2a
  int border = 0;  // 0x108a2c: added around dialogs with flag 1
  int close_box_x = 0;  // 0x108a2e
  int close_box_y = 0;  // 0x108a30
  int close_box_width = 0;  // 0x108a32
  int close_box_height = 0;  // 0x108a34
  int button_height = 0;  // 0x108a36
  int check_box_height = 0;  // 0x108a38
  int check_box_label_width = 0;  // 0x108a3a
  int radio_height = 0;  // 0x108a3c
  int cycle_height = 0;  // 0x108a3e
  int entry_height = 0;  // 0x108a40
  int textbox_origin_x = 0;  // 0x108a6c
  int textbox_origin_y = 0;  // 0x108a6e
  int list_row_height = 0;  // 0x108a74
  int list_rows = 0;  // 0x108a76
  int list_height = 0;  // 0x108a78
  int list_track = 0;  // 0x108a7a
  int list_text_y = 0;  // 0x108a7c: text y offset in a list row (gui_draw_list_item)
  int dial_width = 0;  // 0x108a84
  int dial_height = 0;  // 0x108a86
  int grid_cell_height = 0;  // 0x108a88

  // Returns the default values, read from SH.EXE (`game_exe`).
  static SkinMetrics from_game_exe();
};

// A font slot of the label and text field hooks: 0..3 the GUI fonts, 4 a
// font of the control's own.
enum class FontSlot : std::uint8_t { kSmall = 0, kLarge = 1, kFont1 = 2, kGui1 = 3, kCustom = 4 };

class Dialog;

// One 0x28-byte cell of a button grid: its box (one grid cell height
// high), its label, its neighbours for the arrow keys (cell indexes, -1
// none), its hotkey and its disabled flag.
struct GridCell {
  int x = 0;
  int y = 0;
  int w = 0;
  std::string label;  // +0xc
  int up = -1;        // +0x10
  int down = -1;      // +0x14
  int left = -1;      // +0x18
  int right = -1;     // +0x1c
  std::uint32_t key = 0;   // +0x20
  bool disabled = false;   // +0x24
};

// The bearing dial a spin entry can be linked to (+0x3c): the needle shows
// the entry's value in degrees, and `second` an optional second bearing.
struct Dial {
  int x = 0;
  int y = 0;
  const int* second = nullptr;
  bool alternate_colour = false;  // +0x20
};

// The hook table. The default implementation (`DefaultSkin`) and the MENU
// skin (`MenuSkin`) derive from it; screens that swap a few hooks (the
// awards typewriter, the captain's cabin text box, the interview sliders)
// override those.
class Skin {
 public:
  virtual ~Skin() = default;

  virtual const SkinMetrics& metrics() const = 0;
  // Returns the font of `slot` (`custom` for `FontSlot::kCustom`), as the
  // font globals currently point.
  virtual const Font& font(FontSlot slot, const Font* custom = nullptr) const = 0;

  // 0x108974: the dialog frame (`flags` bit 1), `titled` for flag bit 0.
  virtual Rect dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) = 0;
  // 0x108978: a label, its text drawn through the GUI glyph table when
  // `use_colours` (+0x20), else unchanged.
  virtual Rect label(const Viewport& gfx, int x, int y, FontSlot font, std::string_view text, bool use_colours,
                     const Font* custom) = 0;
  // 0x10897c: a group box with an optional caption.
  virtual Rect group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view caption) = 0;
  // 0x108980: a line in colour `B[colour]`.
  virtual Rect line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) = 0;
  // 0x108984: the title bar of a `w` x `h` dialog.
  virtual Rect title_bar(const Viewport& gfx, int w, int h, std::string_view text, bool has_close_box) = 0;
  // 0x108988: the close box at (x, y).
  virtual Rect close_box(const Viewport& gfx, int x, int y, bool pressed) = 0;
  // 0x10898c: a text field filled with `fill` (-1 for the default), its
  // text drawn through the GUI glyph table when `use_colours`.
  virtual Rect text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, FontSlot font,
                          std::string_view text, int fill, TextAlign align, bool use_colours,
                          const Font* custom) = 0;
  // 0x108990: a push button `w` wide.
  virtual Rect button(const Viewport& gfx, int x, int y, int w, std::string_view label, bool pressed,
                      bool highlighted, bool focused, bool disabled) = 0;
  // 0x108994 / 0x108998: a check box and the width of its hotspot.
  virtual Rect check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused,
                         std::string_view label) = 0;
  virtual int check_box_width(std::string_view label) = 0;
  // 0x10899c / 0x1089a0: a radio button and the width of its hotspot.
  virtual Rect radio_button(const Viewport& gfx, int x, int y, bool pressed, bool selected, bool focused,
                            bool disabled, std::string_view label) = 0;
  virtual int radio_button_width(std::string_view label) = 0;
  // 0x1089a4: a cycle button showing `text`.
  virtual Rect cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused, bool disabled,
                            std::string_view text) = 0;
  // 0x1089a8..0x1089b4: the text entry box, its text, its caret below
  // character `caret` (drawn straight onto the screen at the dialog's
  // position, outside the dirty list), and the character index under the
  // click x `click_x` (dialog coordinates) of an entry at `x`.
  virtual Rect entry_box(const Viewport& gfx, int x, int y, int w) = 0;
  virtual Rect entry_text(const Viewport& gfx, int x, int y, int w, std::string_view text, bool read_only) = 0;
  virtual void entry_caret(Dialog& dialog, int x, int y, std::string_view text, int caret, bool on) = 0;
  virtual int entry_char_index_at(int click_x, int x, std::string_view text) = 0;
  // 0x1089b8..0x1089c4: the spin entry box, its arrow rectangles (dialog
  // coordinates) and its arrows (`pressed` 0 none, 1 up, 2 down).
  virtual Rect spin_entry_box(const Viewport& gfx, int x, int y, int w) = 0;
  virtual Rect spin_up_rect(int x, int y, int w) = 0;
  virtual Rect spin_down_rect(int x, int y, int w) = 0;
  virtual Rect spin_arrows(const Viewport& gfx, int x, int y, int w, int pressed, bool read_only) = 0;
  // 0x1089c8..0x1089dc: the text box frame and background, one run of
  // text, the width of a character, the caret under character `ch` (onto
  // the screen like the entry caret), and the offset from a layout
  // position to the drawn text.
  virtual Rect textbox_frame(const Viewport& gfx, int x, int y, int w, int h) = 0;
  virtual Rect textbox_background(const Viewport& gfx, int x, int y, int w, int h) = 0;
  virtual void textbox_text(const Viewport& gfx, int x, int y, std::string_view text, bool highlight) = 0;
  virtual int textbox_char_width(std::uint8_t ch) = 0;
  virtual void textbox_caret(Dialog& dialog, int x, int y, std::uint8_t ch, bool on) = 0;
  virtual void textbox_text_origin(int& x, int& y) = 0;
  // 0x1089e0..0x1089fc: the list frame, its arrow and track rectangles
  // (dialog coordinates), one row, the scroll bar, an arrow button and the
  // focus frame.
  virtual Rect list_frame(const Viewport& gfx, int x, int y, int w) = 0;
  virtual Rect list_up_rect(int x, int y, int w) = 0;
  virtual Rect list_down_rect(int x, int y, int w) = 0;
  virtual Rect list_track_rect(int x, int y, int w) = 0;
  virtual Rect list_item(const Viewport& gfx, int x, int y, int w, std::string_view text, bool selected) = 0;
  virtual Rect list_scrollbar(const Viewport& gfx, int x, int y, int w, int top, int count) = 0;
  virtual Rect list_arrow(const Viewport& gfx, int x, int y, int w, bool down, bool pressed) = 0;
  virtual Rect list_focus(const Viewport& gfx, int x, int y, int w, bool focused) = 0;
  // 0x108a00..0x108a18: the slider frame, its arrow and track rectangles
  // (dialog coordinates), the track with its thumb at `permille` of the
  // range, an arrow (`right` false = left) and the focus frame.
  virtual Rect slider_frame(const Viewport& gfx, int x, int y, int w, bool disabled) = 0;
  virtual Rect slider_left_rect(int x, int y, int w) = 0;
  virtual Rect slider_right_rect(int x, int y, int w) = 0;
  virtual Rect slider_track_rect(int x, int y, int w) = 0;
  virtual Rect slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool disabled) = 0;
  virtual Rect slider_arrow(const Viewport& gfx, int x, int y, int w, bool right, bool pressed, bool disabled) = 0;
  virtual Rect slider_focus(const Viewport& gfx, int x, int y, int w, bool focused) = 0;
  // 0x108a1c: the bearing dial linked to a spin entry whose text is
  // `entry_text`.
  virtual Rect dial(const Viewport& gfx, const Dial& dial, std::string_view entry_text) = 0;
  // 0x108a20: a button grid cell.
  virtual Rect grid_cell(const Viewport& gfx, const GridCell& cell, bool highlighted) = 0;
};

}  // namespace sh::ui
