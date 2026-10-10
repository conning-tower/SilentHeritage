// Text drawing with `.FNT` fonts: the glyph blitter, the GUI text routine
// and its colour table, and the text field alignment.
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "assets/fnt.h"
#include "gfx/viewport.h"

namespace sh {

// A 256-entry glyph colour table: glyph value `c` is drawn as `table[c]`,
// and a result of 0xff leaves the target pixel unchanged.
using GlyphColourTable = std::array<std::uint8_t, 256>;

// Returns the GUI colour table 0x11dce0 as `gui_init` (0x3afc0) builds it:
// the identity plus the six (index, colour) pairs of the GUI configuration
// in SH.EXE (0x102fa0), which recolour the glyph shades 5, 6, 7 to 4, 3, 2.
// Screens recolour text by patching entries (e.g. 0x3f -> 0x3d for the
// awards typewriter and the radio log).
GlyphColourTable gui_colour_table();

// The table entry whose colour draws the '\v' line of `gui_draw_text`
// (configuration word 0x11dcd8).
inline constexpr std::uint8_t kGuiLineColourIndex = 8;

// Draws the glyph of `code` with its top-left corner at (x, y) of `vp`
// (`font_draw_char`, 0xae5d3), clipped to the viewport. Without `colours`
// the glyph is copied opaquely (0xff included); with it, each value goes
// through the table (0xff results are transparent). Returns the glyph width
// (0 for a code the font lacks), or -1 / -2 for a bad bitmap / empty clip.
int font_draw_char(const Viewport& vp, int x, int y, const Font& font, std::uint8_t code,
                   const GlyphColourTable* colours);

// The rectangle `gui_draw_text` reports as drawn (the dirty rectangle
// 0x11dee4): from (x, y) to the final pen position and `y + height - 3`.
struct TextRect {
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
};

// Draws `text` (up to its first NUL) from (x, y) of `vp`, glyph after glyph
// with no spacing (`gui_draw_text`, 0x42f18). A '\v' draws a horizontal line
// at `y + height - 3`, as wide as the next glyph minus one pixel, in colour
// `colours[kGuiLineColourIndex]` (colour 8 without a table), and does not
// move the pen: it underlines the next character.
TextRect gui_draw_text(const Viewport& vp, int x, int y, const Font& font, std::string_view text,
                       const GlyphColourTable* colours);

// The alignments of a GUI text field (control 0x1027d0).
enum class TextAlign : std::uint8_t { kRight = 0, kLeft = 1, kCentre = 2 };

// Returns the x offset of `text` inside a field spanning `field_x0..field_x1`
// (inclusive) for `align` (`gui_draw_text_field`, 0x4115d).
int text_align_offset(const Font& font, std::string_view text, int field_x0, int field_x1, TextAlign align);

}  // namespace sh
