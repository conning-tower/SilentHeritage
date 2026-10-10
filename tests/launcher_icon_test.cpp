// The launcher icon tool's PNG encoder, resampler and game-art picture.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

#include "assets/pcx.h"
#include "core/image.h"
#include "game_data.h"
#include "tools/launcher_icon.h"

namespace {

std::uint32_t be32(std::span<const std::uint8_t> d, std::size_t at) {
  return (std::uint32_t{d[at]} << 24) | (std::uint32_t{d[at + 1]} << 16) | (std::uint32_t{d[at + 2]} << 8) |
         d[at + 3];
}

// A bitwise CRC-32 and Adler-32, independent of the table-driven ones.
std::uint32_t slow_crc32(std::span<const std::uint8_t> data) {
  std::uint32_t c = 0xffffffffu;
  for (const std::uint8_t b : data) {
    c ^= b;
    for (int k = 0; k < 8; ++k) {
      c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
  }
  return ~c;
}

std::uint32_t slow_adler32(std::span<const std::uint8_t> data) {
  std::uint64_t a = 1;
  std::uint64_t b = 0;
  for (const std::uint8_t v : data) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  return static_cast<std::uint32_t>((b << 16) | a);
}

// What a minimal PNG reader finds in an encoded image.
struct DecodedPng {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> ihdr_tail;  // bit depth .. interlace
  std::vector<std::uint8_t> raw;        // the inflated scanlines
  int stored_blocks = 0;
};

// Reads the chunks (checking each CRC and the order IHDR, IDAT..., IEND)
// and inflates the stored deflate blocks (checking the zlib header,
// LEN/NLEN and the Adler-32).
DecodedPng read_png(std::span<const std::uint8_t> png) {
  const std::vector<std::uint8_t> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  REQUIRE(std::vector(png.begin(), png.begin() + 8) == signature);
  DecodedPng out;
  std::vector<std::uint8_t> z;
  std::vector<std::string> types;
  std::size_t at = 8;
  while (at < png.size()) {
    const std::uint32_t len = be32(png, at);
    const std::string type(png.begin() + static_cast<std::ptrdiff_t>(at + 4),
                           png.begin() + static_cast<std::ptrdiff_t>(at + 8));
    const auto body = png.subspan(at + 8, len);
    CHECK(be32(png, at + 8 + len) == slow_crc32(png.subspan(at + 4, len + 4)));
    types.push_back(type);
    if (type == "IHDR") {
      REQUIRE(len == 13);
      out.width = be32(body, 0);
      out.height = be32(body, 4);
      out.ihdr_tail.assign(body.begin() + 8, body.end());
    } else if (type == "IDAT") {
      z.insert(z.end(), body.begin(), body.end());
    }
    at += 12 + len;
  }
  CHECK(at == png.size());
  REQUIRE(types.size() >= 3);
  CHECK(types.front() == "IHDR");
  CHECK(types.back() == "IEND");
  REQUIRE(z.size() >= 6);
  CHECK((z[0] * 256 + z[1]) % 31 == 0);
  CHECK((z[0] & 0x0f) == 8);
  std::size_t pos = 2;
  bool final = false;
  while (!final) {
    final = (z[pos] & 1) != 0;
    REQUIRE((z[pos] >> 1) == 0);  // BTYPE 00: stored
    const auto len = static_cast<std::size_t>(z[pos + 1] | (z[pos + 2] << 8));
    const auto nlen = static_cast<std::size_t>(z[pos + 3] | (z[pos + 4] << 8));
    CHECK((len ^ 0xffff) == nlen);
    out.raw.insert(out.raw.end(), z.begin() + static_cast<std::ptrdiff_t>(pos + 5),
                   z.begin() + static_cast<std::ptrdiff_t>(pos + 5 + len));
    pos += 5 + len;
    ++out.stored_blocks;
  }
  REQUIRE(pos + 4 == z.size());
  CHECK(be32(z, pos) == slow_adler32(out.raw));
  return out;
}

}  // namespace

TEST_CASE("launcher icon: PNG encoder round trip", "[launcher_icon]") {
  // 200 x 100 RGBA: 80100 bytes of scanlines, two stored blocks.
  sh::RgbaBitmap image(200, 100);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.at(x, y) = {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y), static_cast<std::uint8_t>(x ^ y),
                        static_cast<std::uint8_t>(255 - x)};
    }
  }
  const std::vector<std::uint8_t> png = sh::encode_png(image);
  const DecodedPng d = read_png(png);
  CHECK(d.width == 200);
  CHECK(d.height == 100);
  CHECK(d.ihdr_tail == std::vector<std::uint8_t>{8, 6, 0, 0, 0});
  CHECK(d.stored_blocks == 2);
  REQUIRE(d.raw.size() == 100u * (1 + 200 * 4));
  bool same = true;
  for (int y = 0; y < 100; ++y) {
    const std::size_t row = static_cast<std::size_t>(y) * 801;
    same = same && d.raw[row] == 0;
    for (int x = 0; x < 200; ++x) {
      const sh::Rgba8 p = image.at(x, y);
      const std::size_t i = row + 1 + static_cast<std::size_t>(x) * 4;
      same = same && d.raw[i] == p.r && d.raw[i + 1] == p.g && d.raw[i + 2] == p.b && d.raw[i + 3] == p.a;
    }
  }
  CHECK(same);

  // The checksums against the published check values.
  const std::vector<std::uint8_t> digits{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  CHECK(sh::crc32(digits) == 0xcbf43926u);
  const std::vector<std::uint8_t> wiki{'W', 'i', 'k', 'i', 'p', 'e', 'd', 'i', 'a'};
  CHECK(sh::adler32(wiki) == 0x11e60398u);

  // A 1 x 1 picture: one final block.
  CHECK(read_png(sh::encode_png(sh::RgbaBitmap(1, 1, {1, 2, 3, 4}))).stored_blocks == 1);
}

TEST_CASE("launcher icon: area resampling", "[launcher_icon]") {
  SECTION("whole-pixel reduction averages each block") {
    sh::RgbaBitmap image(4, 2, {0, 0, 0, 255});
    image.at(0, 0) = {200, 100, 40, 255};
    image.at(3, 1) = {255, 255, 255, 255};
    const sh::RgbaBitmap out = sh::resize_area(image, 2, 1);
    CHECK(out.at(0, 0) == sh::Rgba8{50, 25, 10, 255});
    CHECK(out.at(1, 0) == sh::Rgba8{64, 64, 64, 255});
  }
  SECTION("fractional coverage weights the shared pixel") {
    sh::RgbaBitmap image(3, 1);
    image.at(0, 0) = {0, 0, 0, 255};
    image.at(1, 0) = {90, 0, 0, 255};
    image.at(2, 0) = {180, 0, 0, 255};
    const sh::RgbaBitmap out = sh::resize_area(image, 2, 1);
    // [0, 1.5) and [1.5, 3): (0 + 90 / 2) / 1.5 and (90 / 2 + 180) / 1.5.
    CHECK(out.at(0, 0).r == 30);
    CHECK(out.at(1, 0).r == 150);
  }
  SECTION("transparent pixels do not darken the colour") {
    sh::RgbaBitmap image(2, 1);
    image.at(0, 0) = {255, 0, 0, 255};
    const sh::RgbaBitmap out = sh::resize_area(image, 1, 1);
    CHECK(out.at(0, 0) == sh::Rgba8{255, 0, 0, 128});
  }
  SECTION("a uniform picture keeps its colour at any ratio") {
    const sh::RgbaBitmap out = sh::resize_area(sh::RgbaBitmap(600, 600, {12, 34, 56, 255}), 162, 162);
    CHECK(out.width() == 162);
    CHECK(out.height() == 162);
    CHECK(out.at(0, 0) == sh::Rgba8{12, 34, 56, 255});
    CHECK(out.at(161, 97) == sh::Rgba8{12, 34, 56, 255});
  }
}

TEST_CASE("launcher icon: game-art square", "[launcher_icon]") {
  SH_REQUIRE_GAME_DATA(install);
  const sh::RgbaBitmap square = sh::game_art_foreground(install);
  REQUIRE(square.width() == 600);
  REQUIRE(square.height() == 600);
  // The picture's columns 40..639 lie at rows 70..549 unchanged.
  const sh::PcxImage pcx = sh::load_pcx(install, "ART/RTNPORT.PCX");
  const sh::Palette pal = sh::to_palette(pcx.palette);
  for (const auto [x, y] : {std::pair{0, 0}, std::pair{317, 300}, std::pair{599, 479}}) {
    const sh::Rgb c = pal.colors[pcx.image.at(40 + x, y)];
    CHECK(square.at(x, 70 + y) == sh::Rgba8{c.r, c.g, c.b, 255});
  }
  // Above, the sky at the top right is blue; the tallest mast (column
  // 349 of the picture) continues to the top.
  const sh::Rgba8 sky = square.at(560, 0);
  CHECK(sky.b > sky.r + 60);
  const sh::Rgb mast = pal.colors[pcx.image.at(349, 0)];
  CHECK(square.at(309, 0) == sh::Rgba8{mast.r, mast.g, mast.b, 255});
}
