// The launcher icon tool's PNG encoder, resampler, overlays and game-art
// picture.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "assets/pcx.h"
#include "core/image.h"
#include "core/install.h"
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

// A scratch directory removed at the end of the test.
class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_launcher_icon_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A 432 x 432 foreground with every kind of pixel: opaque gradients, a
// half-transparent band and a fully transparent (but coloured) corner.
sh::RgbaBitmap test_foreground() {
  sh::RgbaBitmap image(432, 432);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const std::uint8_t a = x < 40 && y < 40 ? 0 : (y >= 200 && y < 230 ? 128 : 255);
      image.at(x, y) = {static_cast<std::uint8_t>(x * 255 / 431), static_cast<std::uint8_t>(y * 255 / 431),
                        static_cast<std::uint8_t>((x + y) % 256), a};
    }
  }
  return image;
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

TEST_CASE("launcher icon: overlays drawn over the picture", "[launcher_icon]") {
  SECTION("an opaque overlay replaces the picture") {
    const sh::RgbaBitmap out =
        sh::composite_over(sh::RgbaBitmap(3, 2, {10, 20, 30, 255}), sh::RgbaBitmap(3, 2, {200, 100, 50, 255}));
    CHECK(out.at(0, 0) == sh::Rgba8{200, 100, 50, 255});
    CHECK(out.at(2, 1) == sh::Rgba8{200, 100, 50, 255});
  }
  SECTION("a fully transparent overlay keeps every pixel, transparent ones too") {
    const sh::RgbaBitmap base = test_foreground();
    const sh::RgbaBitmap out = sh::composite_over(base, sh::RgbaBitmap(432, 432, {255, 255, 255, 0}));
    CHECK(std::ranges::equal(out.pixels(), base.pixels()));
  }
  SECTION("a half-transparent overlay blends") {
    // 128 / 255 of the red over the opaque blue.
    const sh::RgbaBitmap out =
        sh::composite_over(sh::RgbaBitmap(1, 1, {0, 0, 255, 255}), sh::RgbaBitmap(1, 1, {255, 0, 0, 128}));
    CHECK(out.at(0, 0) == sh::Rgba8{128, 0, 127, 255});
  }
  SECTION("alpha accumulates over a half-transparent picture") {
    // a = t + b (1 - t): 0.502 + 0.502 * 0.498 = 0.752; the colours by
    // their share of it.
    const sh::RgbaBitmap out =
        sh::composite_over(sh::RgbaBitmap(1, 1, {0, 0, 200, 128}), sh::RgbaBitmap(1, 1, {200, 0, 0, 128}));
    CHECK(out.at(0, 0) == sh::Rgba8{134, 0, 66, 192});
  }
  SECTION("over a transparent picture, the overlay as it is") {
    const sh::RgbaBitmap out =
        sh::composite_over(sh::RgbaBitmap(1, 1, {9, 9, 9, 0}), sh::RgbaBitmap(1, 1, {40, 80, 120, 77}));
    CHECK(out.at(0, 0) == sh::Rgba8{40, 80, 120, 77});
  }
  SECTION("a smaller overlay is resampled to the picture's size") {
    sh::RgbaBitmap overlay(2, 2);
    overlay.at(1, 1) = {255, 255, 255, 255};
    const sh::RgbaBitmap out = sh::composite_over(sh::RgbaBitmap(4, 4, {0, 0, 0, 255}), overlay);
    REQUIRE(out.width() == 4);
    REQUIRE(out.height() == 4);
    for (int y = 0; y < 4; ++y) {
      for (int x = 0; x < 4; ++x) {
        const std::uint8_t v = x >= 2 && y >= 2 ? 255 : 0;
        CHECK(out.at(x, y) == sh::Rgba8{v, v, v, 255});
      }
    }
  }
}

TEST_CASE("launcher icon: the icons without the extended mark are unchanged", "[launcher_icon]") {
  // The classic packages draw no overlay; a transparent one changes no
  // byte of any resource either.
  const sh::RgbaBitmap base = test_foreground();
  const TempDir plain;
  const TempDir overlaid;
  sh::write_launcher_icons(base, sh::MonochromeSource::kLightness, plain.path());
  sh::write_launcher_icons(sh::composite_over(base, sh::RgbaBitmap(108, 108)), sh::MonochromeSource::kLightness,
                           overlaid.path());
  int files = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(plain.path())) {
    if (entry.is_regular_file()) {
      const std::filesystem::path relative = std::filesystem::relative(entry.path(), plain.path());
      CHECK(sh::read_file(entry.path()) == sh::read_file(overlaid.path() / relative));
      ++files;
    }
  }
  // Five densities, four PNGs each.
  CHECK(files == 20);
}

TEST_CASE("launcher icon: the extended mark reaches every layer", "[launcher_icon]") {
  // An opaque white band over the lower part, as the ribbon: it shows in
  // the foreground, the monochrome layer (the light parts) and the legacy
  // icons.
  const sh::RgbaBitmap base(432, 432, {24, 48, 44, 255});
  sh::RgbaBitmap band(432, 432);
  for (int y = 290; y < 330; ++y) {
    for (int x = 0; x < 432; ++x) {
      band.at(x, y) = {238, 230, 206, 255};
    }
  }
  const sh::RgbaBitmap marked = sh::composite_over(base, band);
  CHECK(marked.at(216, 310) == sh::Rgba8{238, 230, 206, 255});
  CHECK(marked.at(216, 100) == base.at(216, 100));
  const sh::RgbaBitmap mono = sh::monochrome(marked, sh::MonochromeSource::kLightness);
  CHECK(mono.at(216, 310).a == 255);
  CHECK(mono.at(216, 100).a == 0);
  const TempDir plain;
  const TempDir extended;
  sh::write_launcher_icons(base, sh::MonochromeSource::kLightness, plain.path());
  sh::write_launcher_icons(marked, sh::MonochromeSource::kLightness, extended.path());
  for (const char* name : {"ic_launcher_foreground.png", "ic_launcher_monochrome.png", "ic_launcher.png",
                           "ic_launcher_round.png"}) {
    const std::filesystem::path png = std::filesystem::path("mipmap-xxxhdpi") / name;
    CHECK(sh::read_file(plain.path() / png) != sh::read_file(extended.path() / png));
  }
}

TEST_CASE("launcher icon: Windows icon file", "[launcher_icon]") {
  const std::vector<sh::RgbaBitmap> images{sh::RgbaBitmap(16, 16, {1, 2, 3, 4}),
                                           sh::RgbaBitmap(256, 256, {5, 6, 7, 8})};
  const std::vector<std::uint8_t> ico = sh::encode_ico(images);
  const auto le16 = [&ico](std::size_t at) { return ico[at] | (ico[at + 1] << 8); };
  const auto le32 = [&](std::size_t at) { return static_cast<std::uint32_t>(le16(at) | (le16(at + 2) << 16)); };
  // ICONDIR: reserved, type 1 (icon), 2 entries.
  CHECK(le16(0) == 0);
  CHECK(le16(2) == 1);
  CHECK(le16(4) == 2);
  // Each ICONDIRENTRY: its size (256 written 0), 1 plane, 32 bits, its
  // PNG's length and offset; the PNGs follow, in order.
  std::uint32_t offset = 6 + 2 * 16;
  for (std::size_t i = 0; i < images.size(); ++i) {
    const std::size_t entry = 6 + 16 * i;
    const std::vector<std::uint8_t> png = sh::encode_png(images[i]);
    CHECK(ico[entry] == (images[i].width() == 256 ? 0 : images[i].width()));
    CHECK(ico[entry + 1] == ico[entry]);
    CHECK(le16(entry + 4) == 1);
    CHECK(le16(entry + 6) == 32);
    CHECK(le32(entry + 8) == png.size());
    CHECK(le32(entry + 12) == offset);
    CHECK(std::equal(png.begin(), png.end(), ico.begin() + offset));
    offset += static_cast<std::uint32_t>(png.size());
  }
  CHECK(offset == ico.size());
  const std::vector<sh::RgbaBitmap> too_big{sh::RgbaBitmap(257, 257)};
  CHECK_THROWS_AS(sh::encode_ico(too_big), std::invalid_argument);
}

TEST_CASE("launcher icon: macOS icon file", "[launcher_icon]") {
  const std::vector<sh::RgbaBitmap> images{sh::RgbaBitmap(16, 16, {1, 2, 3, 4}),
                                           sh::RgbaBitmap(32, 32, {5, 6, 7, 8})};
  const std::vector<std::uint8_t> icns = sh::encode_icns(images);
  // The header: "icns" and the file's length.
  REQUIRE(icns.size() > 8);
  CHECK(std::string(icns.begin(), icns.begin() + 4) == "icns");
  CHECK(be32(icns, 4) == icns.size());
  // The entries of those sizes, in kIcnsEntries' order: icp4 (16), icp5 and
  // ic11 (32), each its type, its length with the header, its PNG.
  const std::vector<std::pair<std::string, int>> expected{{"icp4", 16}, {"icp5", 32}, {"ic11", 32}};
  std::size_t at = 8;
  for (const auto& [type, size] : expected) {
    REQUIRE(at + 8 <= icns.size());
    CHECK(std::string(icns.begin() + static_cast<std::ptrdiff_t>(at), icns.begin() + static_cast<std::ptrdiff_t>(at + 4)) ==
          type);
    const std::vector<std::uint8_t> png = sh::encode_png(sh::RgbaBitmap(size, size, size == 16 ? sh::Rgba8{1, 2, 3, 4}
                                                                                                 : sh::Rgba8{5, 6, 7, 8}));
    CHECK(be32(icns, at + 4) == png.size() + 8);
    CHECK(std::equal(png.begin(), png.end(), icns.begin() + static_cast<std::ptrdiff_t>(at + 8)));
    at += png.size() + 8;
  }
  CHECK(at == icns.size());
  const std::vector<sh::RgbaBitmap> not_square{sh::RgbaBitmap(16, 8)};
  CHECK_THROWS_AS(sh::encode_icns(not_square), std::invalid_argument);
}

TEST_CASE("launcher icon: desktop icons", "[launcher_icon]") {
  const TempDir dir;
  sh::write_desktop_icons(test_foreground(), dir.path());
  for (const int size : sh::kDesktopIconSizes) {
    const DecodedPng png = read_png(sh::read_file(dir.path() / ("icon-" + std::to_string(size) + ".png")));
    CHECK(png.width == static_cast<std::uint32_t>(size));
    CHECK(png.height == static_cast<std::uint32_t>(size));
  }
  const std::vector<std::uint8_t> ico = sh::read_file(dir.path() / "icon.ico");
  REQUIRE(ico.size() > 6);
  CHECK((ico[4] | (ico[5] << 8)) == static_cast<int>(sh::kDesktopIconSizes.size()));
  // The 432 px foreground's central part is 288 px: the macOS entries up to
  // 256 px, no enlargement.
  const std::vector<std::uint8_t> icns = sh::read_file(dir.path() / "icon.icns");
  REQUIRE(icns.size() > 8);
  CHECK(be32(icns, 4) == icns.size());
  std::vector<std::string> types;
  for (std::size_t at = 8; at + 8 <= icns.size(); at += be32(icns, at + 4)) {
    types.emplace_back(icns.begin() + static_cast<std::ptrdiff_t>(at), icns.begin() + static_cast<std::ptrdiff_t>(at + 4));
  }
  CHECK(types == std::vector<std::string>{"icp4", "icp5", "ic11", "ic12", "ic07", "ic13", "ic08"});
}

TEST_CASE("launcher icon: game-art square", "[launcher_icon][content]") {
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
