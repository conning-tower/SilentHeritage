// The default hook table of the dialog toolkit (static data at 0x108974,
// functions 0x40c6d..0x42b0a): bevelled boxes in the GUI colours, text in
// GUI_S / GUI_L, GUISPR.SHP for the dial.
#pragma once

#include "ui/skin.h"

namespace sh::ui {

class DefaultSkin : public Skin {
 public:
  // `resources` must outlive the skin; its glyph tables are patched while
  // drawing.
  explicit DefaultSkin(GuiResources& resources);

  const SkinMetrics& metrics() const override { return metrics_; }
  const Font& font(FontSlot slot, const Font* custom = nullptr) const override;

  Rect dialog_frame(const Viewport& gfx, int x, int y, int w, int h, bool titled) override;
  Rect label(const Viewport& gfx, int x, int y, FontSlot font, std::string_view text, bool use_colours,
             const Font* custom) override;
  Rect group_box(const Viewport& gfx, int x0, int y0, int x1, int y1, std::string_view caption) override;
  Rect line(const Viewport& gfx, int x0, int y0, int x1, int y1, int colour) override;
  Rect title_bar(const Viewport& gfx, int w, int h, std::string_view text, bool has_close_box) override;
  Rect close_box(const Viewport& gfx, int x, int y, bool pressed) override;
  Rect text_field(const Viewport& gfx, int x0, int y0, int x1, int y1, FontSlot font, std::string_view text,
                  int fill, TextAlign align, bool use_colours, const Font* custom) override;
  Rect button(const Viewport& gfx, int x, int y, int w, std::string_view label, bool pressed, bool highlighted,
              bool focused, bool disabled) override;
  Rect check_box(const Viewport& gfx, int x, int y, bool pressed, bool checked, bool focused, bool disabled,
                 std::string_view label) override;
  int check_box_width(std::string_view label) override;
  Rect radio_button(const Viewport& gfx, int x, int y, bool pressed, bool selected, bool focused, bool disabled,
                    std::string_view label) override;
  int radio_button_width(std::string_view label) override;
  Rect cycle_button(const Viewport& gfx, int x, int y, int w, bool pressed, bool focused, bool disabled,
                    std::string_view text) override;
  Rect entry_box(const Viewport& gfx, int x, int y, int w) override;
  Rect entry_text(const Viewport& gfx, int x, int y, int w, std::string_view text, bool read_only) override;
  void entry_caret(Dialog& dialog, int x, int y, std::string_view text, int caret, bool on) override;
  int entry_char_index_at(int click_x, int x, std::string_view text) override;
  Rect spin_entry_box(const Viewport& gfx, int x, int y, int w) override;
  Rect spin_up_rect(int x, int y, int w) override;
  Rect spin_down_rect(int x, int y, int w) override;
  Rect spin_arrows(const Viewport& gfx, int x, int y, int w, int pressed, bool read_only) override;
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
  Rect slider_frame(const Viewport& gfx, int x, int y, int w, bool disabled) override;
  Rect slider_left_rect(int x, int y, int w) override;
  Rect slider_right_rect(int x, int y, int w) override;
  Rect slider_track_rect(int x, int y, int w) override;
  Rect slider_track(const Viewport& gfx, int x, int y, int w, int permille, bool disabled) override;
  Rect slider_arrow(const Viewport& gfx, int x, int y, int w, bool right, bool pressed, bool disabled) override;
  Rect slider_focus(const Viewport& gfx, int x, int y, int w, bool focused) override;
  Rect dial(const Viewport& gfx, const Dial& dial, std::string_view entry_text) override;
  Rect grid_cell(const Viewport& gfx, const GridCell& cell, bool highlighted) override;

 protected:
  // `gui_draw_raised_box` (0x42d91) with its colour choice: style 0 the
  // plain colours (fill B1, left/top B3, right/bottom B2), else the
  // highlighted ones (A1, A3, A2); `sunken` swaps the edges.
  Rect raised_box(const Viewport& gfx, int x, int y, int w, int h, bool sunken, bool style);
  // The same with the globals B2 and B3 swapped around the call (the
  // sunken look of the entry, text box and list frames).
  Rect sunken_box(const Viewport& gfx, int x, int y, int w, int h);
  // `gui_draw_bevel_frame` (0x4300d).
  Rect bevel_frame(const Viewport& gfx, int x, int y, int w, int h, int bx, int by, bool titled);
  // Draws `text` through `table` and returns the text rectangle.
  Rect text(const Viewport& gfx, int x, int y, const Font& font, std::string_view text,
            const GlyphColourTable& table);
  // Draws a caret row of `width` pixels at (x, y) and (x, y + 1) of the
  // screen.
  static void screen_caret(Dialog& dialog, int x, int y, int width, std::uint8_t colour);

  GuiResources& res_;
  SkinMetrics metrics_;
};

}  // namespace sh::ui
