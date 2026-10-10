#include "gfx/text.h"

#include <algorithm>
#include <array>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"

namespace sh {

namespace {

// The six (index, colour) pairs of the GUI configuration (SH.EXE
// 0x102fa0..0x102fcc, 32 bits each).
constexpr std::uint32_t kGuiColourPairs = 0x102fa0;

}  // namespace

GlyphColourTable gui_colour_table() {
  GlyphColourTable t;
  for (std::size_t i = 0; i < t.size(); ++i) {
    t[i] = static_cast<std::uint8_t>(i);
  }
  static const std::array<std::int32_t, 12> pairs = exe_i32_array<12>(game_exe(), kGuiColourPairs);
  for (std::size_t i = 0; i < pairs.size(); i += 2) {
    check_exe_table(pairs[i] >= 0 && pairs[i] < 256 && pairs[i + 1] >= 0 && pairs[i + 1] < 256, "GUI colour",
                    kGuiColourPairs);
    t[static_cast<std::size_t>(pairs[i])] = static_cast<std::uint8_t>(pairs[i + 1]);
  }
  return t;
}

int font_draw_char(const Viewport& vp, int x, int y, const Font& font, std::uint8_t code,
                   const GlyphColourTable* colours) {
  if (!vp.valid()) {
    return -1;
  }
  const ClipRect clip = vp.clip();
  if (clip.empty()) {
    return -2;
  }
  const Glyph* g = font.glyph(code);
  if (g == nullptr) {
    return 0;
  }
  const int w = static_cast<int>(g->width);
  const int h = font.height();
  const int ox = x + vp.x0;
  const int oy = y + vp.y0;
  Image8& dst = *vp.bitmap;
  for (int gy = std::max(0, clip.top - oy); gy < h && oy + gy <= clip.bottom; ++gy) {
    for (int gx = std::max(0, clip.left - ox); gx < w && ox + gx <= clip.right; ++gx) {
      const std::uint8_t c = g->pixels[static_cast<std::size_t>(gy) * static_cast<std::size_t>(w) +
                                       static_cast<std::size_t>(gx)];
      if (colours == nullptr) {
        dst.set(ox + gx, oy + gy, c);
      } else if ((*colours)[c] != 0xff) {
        dst.set(ox + gx, oy + gy, (*colours)[c]);
      }
    }
  }
  return w;
}

TextRect gui_draw_text(const Viewport& vp, int x, int y, const Font& font, std::string_view text,
                       const GlyphColourTable* colours) {
  const int line_y = y + font.height() - 3;
  int pen = x;
  for (std::size_t i = 0; i < text.size() && text[i] != '\0'; ++i) {
    const auto code = static_cast<std::uint8_t>(text[i]);
    if (code != 0x0b) {
      // The original adds the -1 / -2 error codes to the pen too.
      pen += font_draw_char(vp, pen, y, font, code, colours);
      continue;
    }
    // gfx_draw_line_clipped (0xa9f61) from (pen, line_y) to (pen + w - 2, line_y).
    const std::uint8_t next = i + 1 < text.size() ? static_cast<std::uint8_t>(text[i + 1]) : 0;
    const int end = pen + font.glyph_width(next) - 2;
    const std::uint8_t colour = colours != nullptr ? (*colours)[kGuiLineColourIndex] : kGuiLineColourIndex;
    if (vp.valid()) {
      const ClipRect clip = vp.clip();
      const int ly = line_y + vp.y0;
      if (ly >= clip.top && ly <= clip.bottom) {
        const int from = std::max(std::min(pen, end) + vp.x0, clip.left);
        const int to = std::min(std::max(pen, end) + vp.x0, clip.right);
        for (int lx = from; lx <= to; ++lx) {
          vp.bitmap->set(lx, ly, colour);
        }
      }
    }
  }
  return {x, y, pen, line_y};
}

int text_align_offset(const Font& font, std::string_view text, int field_x0, int field_x1, TextAlign align) {
  const int width = field_x1 - field_x0 + 1;
  switch (align) {
    case TextAlign::kRight:
      return static_cast<std::int16_t>(width - font.text_width(text));
    case TextAlign::kLeft:
      return 0;
    case TextAlign::kCentre:
      return static_cast<std::int16_t>((width - font.text_width(text) + 1) / 2);
  }
  return 0;
}

}  // namespace sh
