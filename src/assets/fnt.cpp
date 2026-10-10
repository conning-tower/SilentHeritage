#include "assets/fnt.h"

#include <algorithm>
#include <cstring>
#include <format>

#include "core/byte_reader.h"
#include "core/error.h"
#include "core/install.h"

namespace sh {

Font Font::parse(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  const auto magic = r.bytes(4);
  if (std::memcmp(magic.data(), "1.\0\0", 4) != 0) {
    r.fail("not a font (bad magic)");
  }
  const std::uint32_t count = r.u32();
  const std::uint32_t height = r.u32();
  Font font;
  r.skip(4);  // +0xc: 0xff, 0 or 0x100; no font code (`font_draw_char` 0xae5d3...) reads it
  if (count > r.remaining() / 4 || height > 0xffff) {
    r.fail("bad font header");
  }
  font.height_ = static_cast<int>(height);
  std::vector<std::uint32_t> offsets(count);
  for (std::uint32_t& o : offsets) {
    o = r.u32();
  }
  font.glyphs_.resize(count);
  for (std::uint32_t code = 0; code < count; ++code) {
    if (offsets[code] != r.pos()) {
      r.fail(std::format("glyph {} at {:#x}, expected {:#x}", code, offsets[code], r.pos()));
    }
    Glyph& g = font.glyphs_[code];
    g.width = r.u32();
    if (g.width > r.remaining() / std::max<std::uint32_t>(height, 1)) {
      r.fail(std::format("glyph {} overruns the file", code));
    }
    const auto pixels = r.bytes(static_cast<std::size_t>(g.width) * height);
    g.pixels.assign(pixels.begin(), pixels.end());
  }
  if (!r.at_end()) {
    r.fail(std::format("{} trailing bytes", r.remaining()));
  }
  return font;
}

Font Font::load(const Install& install, std::string_view dos_path) {
  return parse(install.read(dos_path), dos_path);
}

int Font::text_width(std::string_view text) const {
  int width = 0;
  for (const char c : text) {
    if (c == '\0') {
      break;
    }
    if (c != '\v') {
      width += glyph_width(static_cast<std::uint8_t>(c));
    }
  }
  return width;
}

}  // namespace sh
