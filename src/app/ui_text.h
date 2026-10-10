// The asset viewer's own text and colours: lines drawn with the game's GUI
// font (ART/GUI_S.FNT) in colours picked from whatever palette is shown.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "assets/fnt.h"
#include "core/image.h"
#include "gfx/viewport.h"

namespace sh {

class Install;
class ShPaths;

// Returns the index of the colour of `palette` closest to `wanted`
// (squared RGB distance; the lowest index on ties), excluding 255: the
// glyph colour tables treat it as transparent.
std::uint8_t nearest_colour(const Palette& palette, Rgb wanted);

// The viewer's colours as indices into the palette on screen.
struct UiColours {
  std::uint8_t background = 0;  // black
  std::uint8_t text = 0;        // white
  std::uint8_t dim = 0;         // grey
  std::uint8_t accent = 0;      // yellow: titles, the selected line
  std::uint8_t select = 0;      // dark blue: the selection bar

  // Returns the colours nearest to the wanted ones in `palette`.
  static UiColours pick(const Palette& palette);
};

// Single-colour text with the GUI font.
class UiText {
 public:
  explicit UiText(const Install& install, const ShPaths& paths);

  const Font& font() const { return font_; }
  int line_height() const { return font_.height(); }
  int width(std::string_view text) const { return font_.text_width(text); }

  // Draws `text` from (x, y) of `vp` in `colour` (`gui_draw_text`, every
  // glyph pixel recoloured), cut with "..." to `max_width` pixels when
  // positive. Returns the x after the text.
  int draw(const Viewport& vp, int x, int y, std::string_view text, std::uint8_t colour, int max_width = 0) const;

  // Returns `text` cut so it fits `max_width` pixels, ending with "..." when
  // cut.
  std::string fit(std::string_view text, int max_width) const;

 private:
  Font font_;
};

}  // namespace sh
