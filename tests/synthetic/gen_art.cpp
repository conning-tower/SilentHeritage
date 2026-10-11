// The pictures, sprites and fonts of the synthetic install: the sizes,
// frame boxes and glyph widths of the manifest, filled with procedural
// patterns and labels.
#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

#include "synthetic/encoders.h"
#include "synthetic/generators.h"

namespace sh::test::synth {

namespace {

// A full-screen picture: a gradient in one ramp of the palette, a grid,
// a framed panel and the file's name.
Image8 picture(const std::string& name, int width, int height) {
  const std::uint32_t h = name_hash(name);
  Image8 img(width, height);
  const int ramp = static_cast<int>(h % 15);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double shade = 0.25 + 0.5 * y / std::max(1, height) +
                           0.25 * fractal_noise(x / 64.0, y / 64.0, h, 3);
      auto c = static_cast<std::uint8_t>(16 + ramp * 16 + std::clamp(static_cast<int>(shade * 15), 0, 15));
      if (x % 32 == 0 || y % 32 == 0) {
        c = static_cast<std::uint8_t>(16 + ((ramp + 7) % 15) * 16 + 4);
      }
      img.set(x, y, c);
    }
  }
  const std::string label = name.substr(name.rfind('/') + 1);
  const int scale = std::max(1, std::min(4, width / (static_cast<int>(label.size()) * 6 + 2)));
  draw_label(img, 4, 4, label, scale, 255);
  return img;
}

// A small texture: noise over a ramp.
Image8 texture(const std::string& name, int width, int height) {
  const std::uint32_t h = name_hash(name);
  Image8 img(width, height);
  const int ramp = static_cast<int>(h % 15);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const double v = fractal_noise(x / 16.0, y / 16.0, h, 4);
      img.set(x, y, static_cast<std::uint8_t>(16 + ramp * 16 + std::clamp(static_cast<int>(v * 16), 0, 15)));
    }
  }
  return img;
}

// One sprite frame: a filled shape with a border in a colour of the
// frame, transparent around an ellipse for the larger ones, numbered when
// there is room.
SpriteImage sprite_frame(const SpriteManifest::Frame& f, std::uint32_t seed, std::size_t index) {
  SpriteImage out;
  if (f.empty) {
    out.pixels = Image8(0, 0);
    return out;
  }
  const int w = static_cast<int>(std::int64_t{f.x1} - f.x0 + 1);
  const int h = static_cast<int>(std::int64_t{f.y1} - f.y0 + 1);
  out.pixels = Image8(w, h);
  out.hotspot_x = -f.x0;
  out.hotspot_y = -f.y0;
  const std::uint32_t k = seed + static_cast<std::uint32_t>(index) * 31u;
  const auto fill = static_cast<std::uint8_t>(16 + (k % 15) * 16 + 9);
  const auto border = static_cast<std::uint8_t>(16 + (k % 15) * 16 + 3);
  const bool round = w >= 8 && h >= 8 && (k & 1) != 0;
  constexpr std::uint8_t kClear = 0;
  out.transparent = round ? std::optional<std::uint8_t>(kClear) : std::nullopt;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double dx = (x + 0.5) / w - 0.5;
      const double dy = (y + 0.5) / h - 0.5;
      const bool inside = !round || dx * dx + dy * dy <= 0.25;
      const bool edge = x == 0 || y == 0 || x == w - 1 || y == h - 1;
      out.pixels.set(x, y, !inside ? kClear : edge ? border : fill);
    }
  }
  if (w >= 14 && h >= 9) {
    draw_label(out.pixels, 2, 1, std::to_string(index), 1, 255);
  }
  if (f.palette_entries > 0) {
    const std::uint32_t n = std::min<std::uint32_t>(f.palette_entries, 256);
    const std::uint32_t base = n > 240 ? 256 - n : 16;
    for (std::uint32_t i = 0; i < n; ++i) {
      const auto v = static_cast<std::uint8_t>((i * 63) / std::max<std::uint32_t>(n - 1, 1));
      out.palette.push_back({static_cast<std::uint8_t>(base + i), v, static_cast<std::uint8_t>(63 - v),
                             static_cast<std::uint8_t>((v + 21) % 64)});
    }
  }
  return out;
}

// A font of the manifest's glyph widths: the 5 x 7 letters scaled into
// each glyph, ink 1 on transparent 0xff.
std::vector<std::uint8_t> font_file(const FileManifest& m) {
  if (m.numbers.empty()) {
    throw std::runtime_error(m.path + ": font manifest without a height");
  }
  const auto height = static_cast<std::uint32_t>(m.numbers[0]);
  std::vector<GlyphImage> glyphs;
  for (std::size_t c = 1; c < m.numbers.size(); ++c) {
    GlyphImage g;
    g.width = static_cast<std::uint32_t>(m.numbers[c]);
    g.pixels.assign(std::size_t{g.width} * height, 0xff);
    const char ch = static_cast<char>(c - 1);
    const int inner = std::max(1, static_cast<int>(g.width) - 1);
    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < g.width; ++x) {
        const int fx = static_cast<int>(x) * 5 / inner;
        const int fy = static_cast<int>(y) * 7 / std::max<int>(1, static_cast<int>(height));
        if (static_cast<int>(x) < inner && font5x7(ch, fx, fy)) {
          g.pixels[y * g.width + x] = 1;
        }
      }
    }
    glyphs.push_back(std::move(g));
  }
  return encode_fnt(glyphs, height);
}

// A `.CAT` whose members take the frame structures of the manifest's
// archive in turn, named `names`.
std::vector<std::uint8_t> cat_file(const FileManifest& m, const std::vector<std::string>& names) {
  if (m.sprites.empty()) {
    throw std::runtime_error(m.path + ": archive manifest without members");
  }
  std::vector<CatMember> members;
  for (std::size_t i = 0; i < names.size(); ++i) {
    const SpriteManifest& s = m.sprites[i % m.sprites.size()];
    members.push_back({names[i], sprite_file(s, name_hash(names[i]))});
  }
  return encode_cat(members);
}

}  // namespace

std::vector<std::uint8_t> sprite_file(const SpriteManifest& sprite, std::uint32_t seed) {
  std::vector<SpriteImage> frames;
  for (std::size_t i = 0; i < sprite.frames.size(); ++i) {
    frames.push_back(sprite_frame(sprite.frames[i], seed, i));
  }
  return encode_shp(frames);
}

void generate_art(Context& context) {
  for (const FileManifest* m : context.entries("ART/")) {
    const std::string name = m->path.substr(4);
    if (m->kind == "pcx") {
      const auto w = static_cast<int>(m->numbers.at(0));
      const auto h = static_cast<int>(m->numbers.at(1));
      const bool small = w <= 256 && h <= 256;
      const bool red = m->path.ends_with(".PAL");
      context.files[m->path] =
          encode_pcx(small ? texture(m->path, w, h) : picture(m->path, w, h), synthetic_palette(name_hash(name), red));
    } else if (m->kind == "shp") {
      context.files[m->path] = sprite_file(m->sprites.at(0), name_hash(name));
    } else if (m->kind == "fnt") {
      context.files[m->path] = font_file(*m);
    }
  }
  // The archives of the classes' models (generate_tables names them).
  std::vector<std::string> ships;
  std::vector<std::string> idbook;
  for (const Model::ShipClassNames& s : context.model.ships) {
    ships.push_back(s.sprite);
    if (!s.silhouette.empty()) {
      ships.push_back(s.silhouette);
    }
    idbook.push_back(s.idbook);
  }
  context.files["ART/3DSHIPS.CAT"] = cat_file(context.entry("ART/3DSHIPS.CAT"), ships);
  context.files["ART/IDBOOK.CAT"] = cat_file(context.entry("ART/IDBOOK.CAT"), idbook);
  context.files["ART/3DAIR.CAT"] = cat_file(context.entry("ART/3DAIR.CAT"), context.model.aircraft_sprites);
}

}  // namespace sh::test::synth
