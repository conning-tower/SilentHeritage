#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "assets/cat.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "core/error.h"
#include "game_data.h"
#include "gfx/sprite.h"
#include "gfx/text.h"

namespace sh {

namespace {

// FNV-1a over `bytes`: the checksums below were computed the same way on the
// output of an independent reference decoder (transparent pixels as 0).
std::uint32_t fnv1a(std::span<const std::uint8_t> bytes) {
  std::uint32_t h = 0x811c9dc5u;
  for (const std::uint8_t b : bytes) {
    h = (h ^ b) * 0x01000193u;
  }
  return h;
}

int count_nonzero(const Image8& img) {
  int n = 0;
  for (const std::uint8_t p : img.pixels()) {
    n += p != 0;
  }
  return n;
}

// A 4x2 frame with its hotspot at (2, 1) of the box: row 0 is the literal
// 1 2 3 4, row 1 is one transparent pixel then a run of two 7s.
SpriteFrame synthetic_frame() {
  SpriteFrame f;
  f.x0 = -2;
  f.y0 = -1;
  f.x1 = 1;
  f.y1 = 0;
  f.rle = {9, 1, 2, 3, 4, 0, 1, 1, 4, 7, 0};
  return f;
}

std::vector<std::uint8_t> row_of(const Image8& img, int y) {
  const auto r = img.row(y);
  return {r.begin(), r.end()};
}

}  // namespace

TEST_CASE("CAT directory of 3DSHIPS.CAT", "[gfx][cat][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const CatArchive cat = CatArchive::load(install, "ART/3DSHIPS.CAT");
  REQUIRE(cat.entries().size() == 81);
  CHECK(cat.entries()[0].name == "ABOAS.SHP");
  CHECK(cat.entries()[0].flags == 0x2000);
  CHECK(cat.entries()[0].size == 11297);
  CHECK(cat.entries()[0].offset == 2 + 26 * 81);
  CHECK(cat.entries()[1].name == "AGANO.SHP");
  CHECK(cat.entries()[1].offset == 13405);
  CHECK(cat.entries()[80].name == "YUBARIS.SHP");
  CHECK(cat.find("agano.shp") == 1u);
  CHECK_FALSE(cat.find("NOPE.SHP").has_value());
  CHECK(cat.member("yubaris.shp").size() == 11456);
  CHECK_THROWS_AS(cat.member("NOPE.SHP"), DataError);
}

TEST_CASE("SHP from a CAT member matches the reference decoder", "[gfx][shp][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const CatArchive cat = CatArchive::load(install, "ART/3DSHIPS.CAT");
  const Shp shp = Shp::parse(cat.member("ABOAS.SHP"), "ABOAS.SHP");
  REQUIRE(shp.frame_count() == 23);
  CHECK(shp.frame(0).empty());  // box (2^31-1, 2^31-1, -(2^31-1), -(2^31-1))
  const SpriteFrame& f = shp.frame(7);
  CHECK(f.x0 == -98);
  CHECK(f.y0 == -45);
  CHECK(f.width() == 196);
  CHECK(f.height() == 46);
  const DecodedFrame d = f.decode();
  CHECK(count_nonzero(d.opaque) == 3105);
  CHECK(fnv1a(d.image.pixels()) == 0x19f91628u);
  CHECK(fnv1a(shp.frame(11).decode().image.pixels()) == 0xaacb00d2u);
}

TEST_CASE("SHP files: frame boxes, pixels, partial palettes", "[gfx][shp][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp gui = Shp::load(install, "ART/GUISPR.SHP");
  CHECK(gui.version() == "1.10");
  REQUIRE(gui.frame_count() == 1);
  const SpriteFrame& f = gui.frame(0);
  CHECK(f.x0 == -47);
  CHECK(f.y0 == -43);
  CHECK(f.width() == 91);
  CHECK(f.height() == 85);
  const DecodedFrame d = f.decode();
  CHECK(count_nonzero(d.opaque) == 7735);
  CHECK(fnv1a(d.image.pixels()) == 0x9accb8c5u);
  CHECK(d.image.at(45, 40) == 4);

  const Shp sbchart = Shp::load(install, "ART/SBCHART.SHP");
  REQUIRE(sbchart.frame_count() == 45);
  CHECK(sbchart.frame(42).empty());

  const Shp pics = Shp::load(install, "ART/MENUPICS.SHP");
  REQUIRE(pics.frame_count() == 9);
  const SpriteFrame& p = pics.frame(0);
  REQUIRE(p.palette.size() == 110);
  CHECK(p.palette[0].index == 149);
  CHECK(p.palette[0].r == 0x33);
  CHECK(p.palette[0].g == 0x38);
  CHECK(p.palette[0].b == 0x3a);
  VgaPalette screen{};
  sprite_apply_palette(p, screen);
  CHECK(screen[149 * 3] == 0x33);
  CHECK(screen[148 * 3 + 2] == 0x3a);
  CHECK(screen[0] == 0);
}

TEST_CASE("Every original sprite set and font parses like the Python tools", "[gfx][shp][fnt][content]") {
  SH_REQUIRE_GAME_DATA(install);
  auto opaque_pixels = [](const Shp& shp) {
    long n = 0;
    for (const SpriteFrame& f : shp.frames()) {
      n += f.empty() ? 0 : count_nonzero(f.decode().opaque);
    }
    return n;
  };
  std::size_t frames = 0;
  std::size_t empty = 0;
  long opaque = 0;
  for (const std::string& path : install.list("ART", ".SHP")) {
    const Shp shp = Shp::load(install, path);
    frames += shp.frame_count();
    empty += static_cast<std::size_t>(std::ranges::count_if(shp.frames(), &SpriteFrame::empty));
    opaque += opaque_pixels(shp);
  }
  CHECK(frames == 1028);
  CHECK(empty == 1);
  CHECK(opaque == 10643560);

  struct Expected {
    const char* name;
    std::size_t members;
    std::size_t frames;
    long opaque;
  };
  for (const Expected& e : {Expected{"ART/3DSHIPS.CAT", 81, 1947, 3845355}, Expected{"ART/3DAIR.CAT", 5, 245, 600739},
                            Expected{"ART/IDBOOK.CAT", 53, 53, 693959}}) {
    const CatArchive cat = CatArchive::load(install, e.name);
    CHECK(cat.entries().size() == e.members);
    std::size_t n = 0;
    long o = 0;
    for (std::size_t i = 0; i < cat.entries().size(); ++i) {
      const Shp shp = Shp::parse(cat.member(i), cat.entries()[i].name);
      n += shp.frame_count();
      o += opaque_pixels(shp);
    }
    CHECK(n == e.frames);
    CHECK(o == e.opaque);
  }

  const auto fonts = install.list("ART", ".FNT");
  CHECK(fonts.size() == 28);
  long widths = 0;
  for (const std::string& path : fonts) {
    const Font font = Font::load(install, path);
    for (std::size_t c = 0; c < font.glyph_count(); ++c) {
      widths += font.glyph_width(static_cast<std::uint8_t>(c));
    }
  }
  CHECK(widths == 15938);
}

TEST_CASE("SHP parsing rejects rows overrunning the box", "[gfx][shp]") {
  // One frame, box 2x1, row: run of 3.
  std::vector<std::uint8_t> data = {'1', '.', '1', '0', 1, 0, 0, 0, 16, 0, 0, 0, 0, 0, 0, 0};
  const std::vector<std::uint8_t> header = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                            1, 0, 0, 0, 0, 0, 0, 0};
  data.insert(data.end(), header.begin(), header.end());
  data.insert(data.end(), {6, 9, 0});
  CHECK_THROWS_AS(Shp::parse(data, "bad"), DataError);
  data[data.size() - 3] = 4;  // run of 2: fits
  const Shp ok = Shp::parse(data, "ok");
  CHECK(ok.frame(0).decode().image.at(1, 0) == 9);
}

TEST_CASE("PCX decode matches the reference decoder, palette as load_pcx", "[gfx][pcx][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const PcxImage m3 = load_pcx(install, "ART/M3.PCX");
  CHECK(m3.image.width() == 640);
  CHECK(m3.image.height() == 480);
  CHECK(fnv1a(m3.image.pixels()) == 0x2c5bceb4u);
  CHECK(m3.image.at(200, 100) == 28);
  // File bytes 00 00 00 ff 55 55 .. eb ef e3, shifted right by 2.
  CHECK(m3.palette[3] == 63);
  CHECK(m3.palette[4] == 21);
  CHECK(m3.palette[255 * 3] == 58);
  CHECK(m3.palette[255 * 3 + 1] == 59);
  CHECK(m3.palette[255 * 3 + 2] == 56);
  CHECK(to_palette(m3.palette).colors[1] == Rgb{255, 85, 85});

  const PcxImage sea = load_pcx(install, "ART/SEA.PCC");
  CHECK(sea.image.width() == 128);
  CHECK(sea.image.height() == 128);
  const VgaPalette red = pcx_palette(install.read("ART/CONN.PAL"), "CONN.PAL");
  CHECK(red != load_pcx(install, "ART/CONN.PCX").palette);
}

TEST_CASE("FNT glyphs and text width", "[gfx][fnt]") {
  SH_REQUIRE_INSTALL(install);
  const Font font = Font::load(install, "ART/GUI_S.FNT");
  CHECK(font.glyph_count() == 127);
  CHECK(font.height() == 15);
  CHECK(font.glyph_width('A') == 14);
  CHECK(font.glyph_width('i') == 4);
  CHECK(font.glyph_width(200) == 0);
  CHECK(font.text_width("Hello World") == 90);
  CHECK(font.text_width("Hel\vlo") == 38);
  CHECK(font.text_width(std::string("Hel\0lo", 6)) == font.text_width("Hel"));
}

TEST_CASE("Glyph blitter: colour table, transparency, clipping", "[gfx][fnt][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Font font = Font::load(install, "ART/GUI_S.FNT");
  const GlyphColourTable gui = gui_colour_table();
  CHECK(gui[5] == 4);
  CHECK(gui[6] == 3);
  CHECK(gui[7] == 2);
  CHECK(gui[8] == 8);
  CHECK(gui[0x3f] == 0x3f);

  // 'A' row 0 is ff ff ff ff ff 07 07 07 ff..: shade 7 -> colour 2.
  Image8 img(40, 20, 99);
  const Viewport vp = Viewport::full(img);
  CHECK(font_draw_char(vp, 3, 2, font, 'A', &gui) == 14);
  CHECK(img.at(3 + 4, 2) == 99);
  CHECK(img.at(3 + 5, 2) == 2);
  CHECK(img.at(3 + 7, 2) == 2);
  // Without a table the glyph is opaque.
  Image8 raw(40, 20, 99);
  font_draw_char(Viewport::full(raw), 0, 0, font, 'A', nullptr);
  CHECK(raw.at(0, 0) == 0xff);
  CHECK(raw.at(5, 0) == 7);
  // Clipped by a viewport whose origin is (10, 5): x = -7 shows columns 7..13.
  Image8 clipped(40, 20, 99);
  const Viewport small{&clipped, 10, 5, 30, 15};
  CHECK(font_draw_char(small, -7, -5, font, 'A', nullptr) == 14);
  CHECK(clipped.at(9, 5) == 99);
  CHECK(clipped.at(10, 5) == 7);  // glyph column 7 of row 5
  CHECK(clipped.at(17, 4) == 99);

  Image8 text(200, 20, 0);
  const TextRect r = gui_draw_text(Viewport::full(text), 5, 1, font, "Hel\vlo", &gui);
  CHECK(r.x0 == 5);
  CHECK(r.x1 == 5 + 38);
  CHECK(r.y1 == 1 + 15 - 3);
  // The '\v' underlines the 'l' after it: from its x for its width - 1.
  const int lx = 5 + font.text_width("Hel");
  CHECK(text.at(lx, 13) == 8);
  CHECK(text.at(lx + font.glyph_width('l') - 2, 13) == 8);

  CHECK(text_align_offset(font, "Hello World", 10, 109, TextAlign::kRight) == 10);
  CHECK(text_align_offset(font, "Hello World", 10, 109, TextAlign::kLeft) == 0);
  CHECK(text_align_offset(font, "Hello World", 10, 109, TextAlign::kCentre) == 5);
}

TEST_CASE("Sprite blitter places the hotspot and clips to the viewport", "[gfx][sprite]") {
  const SpriteFrame f = synthetic_frame();
  CHECK(f.width() == 4);
  CHECK(f.height() == 2);

  Image8 img(8, 6, 0);
  const Viewport vp{&img, 1, 1, 4, 3};  // origin (1, 1), clip x 1..4, y 1..3
  REQUIRE(draw_sprite_clipped(vp, f, 2, 1) == DrawResult::kOk);
  CHECK(row_of(img, 1) == std::vector<std::uint8_t>{0, 1, 2, 3, 4, 0, 0, 0});
  CHECK(row_of(img, 2) == std::vector<std::uint8_t>{0, 0, 7, 7, 0, 0, 0, 0});

  img.fill(0);
  REQUIRE(draw_sprite_clipped(vp, f, 0, 3) == DrawResult::kOk);  // box x -1..2, y 3..4
  CHECK(row_of(img, 3) == std::vector<std::uint8_t>{0, 3, 4, 0, 0, 0, 0, 0});
  CHECK(row_of(img, 4) == std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 0});

  img.fill(0);
  REQUIRE(draw_sprite_clipped(vp, f, 5, 2) == DrawResult::kOk);  // box x 4..7: right clip at 4
  CHECK(row_of(img, 2) == std::vector<std::uint8_t>{0, 0, 0, 0, 1, 0, 0, 0});
  CHECK(row_of(img, 3) == std::vector<std::uint8_t>{0, 0, 0, 0, 0, 0, 0, 0});

  CHECK(draw_sprite_clipped(vp, f, 10, 1) == DrawResult::kOutside);
  CHECK(draw_sprite_clipped(vp, SpriteFrame{}, 2, 1) == DrawResult::kEmptyFrame);
  CHECK(draw_sprite_clipped(Viewport{&img, 5, 5, 2, 2}, f, 2, 1) == DrawResult::kEmptyClip);
  Image8 none;
  CHECK(draw_sprite_clipped(Viewport::full(none), f, 0, 0) == DrawResult::kBadBitmap);

  ColourRemap remap{};
  for (std::size_t i = 0; i < remap.size(); ++i) {
    remap[i] = static_cast<std::uint8_t>(i + 10);
  }
  img.fill(0);
  draw_sprite_remapped(Viewport::full(img), f, 2, 1, remap);
  CHECK(row_of(img, 0) == std::vector<std::uint8_t>{11, 12, 13, 14, 0, 0, 0, 0});
  CHECK(row_of(img, 1) == std::vector<std::uint8_t>{0, 17, 17, 0, 0, 0, 0, 0});
}

TEST_CASE("Transformed sprites: identity, mirror, scale", "[gfx][sprite]") {
  const SpriteFrame f = synthetic_frame();
  const Fixed16 one = Fixed16::from_int(1);
  Image8 scratch;

  Image8 a(10, 6, 0);
  Image8 b(10, 6, 0);
  draw_sprite_clipped(Viewport::full(a), f, 4, 2);
  sprite_draw_transformed(Viewport::full(b), f, 4, 2, scratch, Angle10(0), one, one, 0);
  CHECK(a.pixels().size() == b.pixels().size());
  CHECK(std::ranges::equal(a.pixels(), b.pixels()));

  // scale_x = -1 mirrors about the hotspot (show_port_return).
  Image8 m(10, 6, 0);
  REQUIRE(sprite_draw_transformed(Viewport::full(m), f, 4, 2, scratch, Angle10(0), -one, one, 0) ==
          DrawResult::kOk);
  CHECK(scratch.width() == 4);
  CHECK(row_of(m, 1) == std::vector<std::uint8_t>{0, 0, 0, 4, 3, 2, 1, 0, 0, 0});
  CHECK(row_of(m, 2) == std::vector<std::uint8_t>{0, 0, 0, 0, 7, 7, 0, 0, 0, 0});

  // Reusing the rendered scratch gives the same picture.
  Image8 m2(10, 6, 0);
  sprite_draw_transformed(Viewport::full(m2), f, 4, 2, scratch, Angle10(0), -one, one, kSpriteScratchReady);
  CHECK(std::ranges::equal(m.pixels(), m2.pixels()));

  // Scale 2 spans (w - 1) * 2 + 1 = 7 columns and (h - 1) * 2 + 1 = 3 rows.
  Image8 s(12, 8, 0);
  sprite_draw_transformed(Viewport::full(s), f, 6, 4, scratch, Angle10(0), Fixed16::from_int(2),
                          Fixed16::from_int(2), 0);
  int left = 99, right = -1, top = 99, bottom = -1;
  for (int y = 0; y < s.height(); ++y) {
    for (int x = 0; x < s.width(); ++x) {
      if (s.at(x, y) != 0) {
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
      }
    }
  }
  CHECK(left == 2);
  CHECK(right == 8);
  CHECK(top == 2);
  CHECK(bottom == 4);
  CHECK(s.at(2, 2) == 1);
  CHECK(s.at(8, 2) == 4);

  CHECK_THROWS_AS(sprite_draw_transformed(Viewport::full(s), f, 0, 0, scratch, Angle10(0), one, one,
                                          kSpriteRemap),
                  std::invalid_argument);
}

TEST_CASE("Transformed sprites: a quarter turn of a real frame", "[gfx][sprite][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp gui = Shp::load(install, "ART/GUISPR.SHP");
  const SpriteFrame& f = gui.frame(0);
  const DecodedFrame d = f.decode();
  Image8 screen(kScreenWidth, kScreenHeight, 0);
  Image8 scratch;
  const Fixed16 one = Fixed16::from_int(1);
  REQUIRE(sprite_draw_transformed(Viewport::full(screen), f, 320, 240, scratch, Angle10(900), one, one, 0) ==
          DrawResult::kOk);
  // Clockwise on screen: the frame offset (dx, dy) lands at (-dy, dx).
  int same = 0;
  int total = 0;
  for (int y = 0; y < f.height(); ++y) {
    for (int x = 0; x < f.width(); ++x) {
      if (d.opaque.at(x, y) == 0) {
        continue;
      }
      const int dx = x + f.x0;
      const int dy = y + f.y0;
      ++total;
      same += screen.at(320 - dy, 240 + dx) == d.image.at(x, y);
    }
  }
  CHECK(total == 7735);
  CHECK(same == total);
}

}  // namespace sh
