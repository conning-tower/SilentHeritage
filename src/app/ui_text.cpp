#include "app/ui_text.h"

#include "core/install.h"
#include "game/shpath.h"
#include "gfx/text.h"

namespace sh {

std::uint8_t nearest_colour(const Palette& palette, Rgb wanted) {
  int best = 0;
  int best_distance = -1;
  for (int i = 0; i < 255; ++i) {
    const Rgb c = palette.colors[static_cast<std::size_t>(i)];
    const int dr = c.r - wanted.r;
    const int dg = c.g - wanted.g;
    const int db = c.b - wanted.b;
    const int d = dr * dr + dg * dg + db * db;
    if (best_distance < 0 || d < best_distance) {
      best = i;
      best_distance = d;
    }
  }
  return static_cast<std::uint8_t>(best);
}

UiColours UiColours::pick(const Palette& palette) {
  return {
      .background = nearest_colour(palette, {0, 0, 0}),
      .text = nearest_colour(palette, {255, 255, 255}),
      .dim = nearest_colour(palette, {150, 150, 150}),
      .accent = nearest_colour(palette, {255, 230, 0}),
      .select = nearest_colour(palette, {20, 40, 140}),
  };
}

UiText::UiText(const Install& install, const ShPaths& paths)
    : font_(Font::load(install, paths.path(GameDir::kArt, "GUI_S.FNT"))) {}

std::string UiText::fit(std::string_view text, int max_width) const {
  if (max_width <= 0 || font_.text_width(text) <= max_width) {
    return std::string(text);
  }
  const int budget = max_width - font_.text_width("...");
  std::string out;
  int w = 0;
  for (const char c : text) {
    const int cw = font_.glyph_width(static_cast<std::uint8_t>(c));
    if (w + cw > budget) {
      break;
    }
    out += c;
    w += cw;
  }
  return out + "...";
}

int UiText::draw(const Viewport& vp, int x, int y, std::string_view text, std::uint8_t colour, int max_width) const {
  GlyphColourTable table;
  table.fill(colour);
  table[0xff] = 0xff;
  const std::string shown = fit(text, max_width);
  return gui_draw_text(vp, x, y, font_, shown, &table).x1;
}

}  // namespace sh
