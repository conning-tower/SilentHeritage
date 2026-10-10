// `.FNT` bitmap fonts (`ART/*.FNT`). Layout in docs/formats.md. Drawing is
// in gfx/text.h.
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sh {

class Install;

// One character: `width` x the font height palette indices, row-major
// (0xff is transparent when drawn through a colour table).
struct Glyph {
  std::uint32_t width = 0;  // 0: undefined character
  std::vector<std::uint8_t> pixels;
};

// A parsed font: one glyph per character code 0..glyph_count-1, all of the
// same height.
class Font {
 public:
  // Parses the font in `data`; throws `DataError` naming `context` on a bad
  // header, or glyphs that overrun the data or do not tile it exactly.
  static Font parse(std::span<const std::uint8_t> data, std::string_view context);
  // Reads and parses the `.FNT` file at `dos_path` of `install`.
  static Font load(const Install& install, std::string_view dos_path);

  // Returns the pixel rows of every glyph (`font_height`, 0xae5a0).
  int height() const { return height_; }
  std::size_t glyph_count() const { return glyphs_.size(); }

  // Returns the glyph of `code`, or nullptr when the font has no such code
  // (the original reads past its offset table there).
  const Glyph* glyph(std::uint8_t code) const {
    return code < glyphs_.size() ? &glyphs_[code] : nullptr;
  }
  // Returns the width of `code` (`font_glyph`, 0xae5b3), 0 if undefined.
  int glyph_width(std::uint8_t code) const {
    const Glyph* g = glyph(code);
    return g == nullptr ? 0 : static_cast<int>(g->width);
  }

  // Returns the pixel width of `text` (`text_width`, 0x42fd3): the sum of
  // its glyph widths up to the first NUL, ignoring the line code 0x0b.
  int text_width(std::string_view text) const;

 private:
  int height_ = 0;
  std::vector<Glyph> glyphs_;
};

}  // namespace sh
