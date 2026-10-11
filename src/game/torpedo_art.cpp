#include "game/torpedo_art.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "assets/dat_tables.h"
#include "gfx/sprite.h"
#include "sim/torpedo.h"

namespace sh {

namespace {

// STATUST.SHP: the Mark 10's still frame; each type has a still and a
// moving frame, and its own 16-colour ramp (Mark 10 16.., Mark 14 32..,
// Mark 18 48..); colour 202 is the propellers.
constexpr std::size_t kFirstTorpedoFrame = 0xc;
constexpr int kRampMark18 = 48;
constexpr int kRampSize = 16;
constexpr std::uint8_t kPropeller = 202;
// Never picked for a recolouring: the original types' ramps.
constexpr int kReservedFirst = 16;
constexpr int kReservedEnd = 64;

struct Rgbf {
  float r, g, b;
};

Rgb colour8(const VgaPalette& pal, int i) {
  const auto c = [&](int k) {
    return static_cast<std::uint8_t>(pal[static_cast<std::size_t>(i * 3 + k)] * 255 / 63);
  };
  return {c(0), c(1), c(2)};
}

float lightness(Rgb c) { return (0.3f * c.r + 0.59f * c.g + 0.11f * c.b) / 255.0f; }

// A palette's unused entries: pure green placeholders.
bool placeholder(Rgb c) { return c.r < 0x10 && c.g > 0xf0 && c.b < 0x10; }

float distance(Rgb p, Rgbf c) {
  const float dr = 2 * (p.r - c.r);
  const float dg = 4 * (p.g - c.g);
  const float db = 3 * (p.b - c.b);
  return dr * dr + dg * dg + db * db;
}

// Returns the entry of the status page's palette nearest to `c`, outside
// the reserved ramps and the placeholders.
std::uint8_t nearest(const VgaPalette& pal, Rgbf c) {
  int best = 0;
  float best_d = 1e30f;
  for (int i = 0; i < 256; ++i) {
    const Rgb p = colour8(pal, i);
    // (0xff is the transparent value of the mirrored draws' scratch.)
    if ((i >= kReservedFirst && i < kReservedEnd) || i == 0xff || placeholder(p)) {
      continue;
    }
    const float d = distance(p, c);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return static_cast<std::uint8_t>(best);
}

Rgbf scaled(Rgb c, float l) { return {std::min(255.0f, c.r * l), std::min(255.0f, c.g * l), std::min(255.0f, c.b * l)}; }

// The marking of a type drawn from the Mark 18 frames: the body tint at
// full light, 2-pixel bands at offsets from the box's left in `band`, and
// the pixels cut from the middle of the body.
struct Marking {
  Rgb tint;
  std::vector<int> bands;
  Rgb band;
  int shorten = 0;
};

const Marking& marking(std::uint32_t id) {
  static const Marking kMark18_1{{150, 190, 255}, {66}, {255, 255, 255}};
  static const Marking kMark18_2{{255, 150, 40}, {62, 68}, {255, 255, 255}};
  static const Marking kMark27{{120, 120, 120}, {20, 23}, {60, 220, 90}, 34};
  return id == kMark18_1Id ? kMark18_1 : id == kMark18_2Id ? kMark18_2 : kMark27;
}

TorpedoPicture plain(const SpriteFrame& f) {
  DecodedFrame d = f.decode();
  return {std::move(d.image), std::move(d.opaque), f.x0, f.y0};
}

TorpedoPicture marked(const SpriteFrame& f, const VgaPalette& pal, const Marking& m) {
  const DecodedFrame d = f.decode();
  constexpr float kGain = 1.15f;
  std::array<std::uint8_t, kRampSize> ramp{};
  for (int k = 0; k < kRampSize; ++k) {
    const float l = std::min(1.0f, lightness(colour8(pal, kRampMark18 + k)) * kGain);
    ramp[static_cast<std::size_t>(k)] = nearest(pal, scaled(m.tint, l));
  }
  const int w = d.image.width() - m.shorten;
  const int h = d.image.height();
  const int cut = d.image.width() / 2 - m.shorten / 2;
  TorpedoPicture p{Image8(w, h), Image8(w, h), f.x0 + m.shorten / 2, f.y0};
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int sx = x < cut ? x : x + m.shorten;
      const std::uint8_t src = d.image.at(sx, y);
      const bool opaque = d.opaque.at(sx, y) != 0;
      std::uint8_t c = src;
      if (opaque && src >= kRampMark18 && src < kRampMark18 + kRampSize) {
        c = ramp[static_cast<std::size_t>(src - kRampMark18)];
      }
      if (opaque && src != kPropeller && std::ranges::any_of(m.bands, [x](int b) { return x == b || x == b + 1; })) {
        // The band takes the body's shading.
        c = nearest(pal, scaled(m.band, std::min(1.0f, lightness(colour8(pal, src)) * 1.6f)));
      }
      p.image.set(x, y, c);
      p.opaque.set(x, y, opaque ? 1 : 0);
    }
  }
  return p;
}

}  // namespace

TorpedoArt::TorpedoArt(const Shp& statust, const VgaPalette& palette) {
  for (std::uint32_t id = 1; id <= kTypes; ++id) {
    for (std::size_t moving = 0; moving < 2; ++moving) {
      // The Mark 27 is made from the Mark 18-1's frames.
      const std::size_t source = id == kMark27Id ? kMark18_1Id : id;
      const SpriteFrame& f = statust.frame(kFirstTorpedoFrame + 2 * (source - 1) + moving);
      pictures_[id - 1][moving] = id >= kMark18_1Id ? marked(f, palette, marking(id)) : plain(f);
    }
  }
}

const TorpedoPicture& TorpedoArt::picture(std::uint32_t id, bool moving) const {
  if (id < 1 || id > kTypes) {
    throw std::out_of_range("no torpedo picture for weapon id " + std::to_string(id));
  }
  return pictures_[id - 1][moving ? 1 : 0];
}

void TorpedoArt::draw(const Viewport& vp, const TorpedoPicture& p, int x, int y, bool mirror, Image8& scratch) {
  if (!vp.valid()) {
    return;
  }
  const int w = p.image.width();
  const int h = p.image.height();
  if (mirror) {
    // The picture's box as a frame, its pixels in the transform's scratch
    // (0xff transparent).
    SpriteFrame box;
    box.x0 = p.x0;
    box.y0 = p.y0;
    box.x1 = p.x0 + w - 1;
    box.y1 = p.y0 + h - 1;
    box.rle = {0};
    scratch = Image8(w, h, 0xff);
    for (int j = 0; j < h; ++j) {
      for (int i = 0; i < w; ++i) {
        if (p.opaque.at(i, j) != 0) {
          scratch.set(i, j, p.image.at(i, j));
        }
      }
    }
    const Fixed16 one = Fixed16::from_int(1);
    sprite_draw_transformed(vp, box, x, y, scratch, Angle10(0), -one, one, kSpriteScratchReady);
    return;
  }
  Image8& target = *vp.bitmap;
  const ClipRect clip = vp.clip();
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      const int dx = vp.x0 + x + p.x0 + i;
      const int dy = vp.y0 + y + p.y0 + j;
      if (p.opaque.at(i, j) != 0 && clip.contains(dx, dy)) {
        target.set(dx, dy, p.image.at(i, j));
      }
    }
  }
}

Rgb torpedo_type_colour(std::uint32_t id) {
  switch (id) {
    case kMark10Id:
      return {0x94, 0xac, 0xa8};
    case kMark14Id:
      return {0xd4, 0xc4, 0x74};
    case kMark18_1Id:
      return {0x70, 0x9c, 0xc8};
    case kMark18_2Id:
      return {0xfc, 0x8c, 0x00};
    case kMark27Id:
      return {0x40, 0xc8, 0x60};
    default:
      return {0x90, 0x90, 0x90};
  }
}

bool on_button_bezel(int x, int y, int w, int h) {
  // The lamp fills the inner 72 % of the button's radius.
  const float dx = static_cast<float>(x) + 0.5f - static_cast<float>(w) / 2;
  const float dy = static_cast<float>(y) + 0.5f - static_cast<float>(h) / 2;
  const float r = static_cast<float>(std::min(w, h)) / 2;
  return std::sqrt(dx * dx + dy * dy) >= r * 0.72f;
}

bool on_button_notch(std::uint32_t id, int x, int y, int w) {
  if (y >= w / 5) {
    return false;
  }
  const int cx = w / 2;
  if (id == kMark18_1Id) {
    return x == cx || x == cx - 1;
  }
  if (id == kMark18_2Id) {
    return x == cx - 4 || x == cx - 3 || x == cx + 3 || x == cx + 4;
  }
  return false;
}

std::string torpedo_short_label(const std::string& name) {
  return name.starts_with("MARK") ? "MK" + name.substr(4) : name;
}

void draw_marked_button(const Viewport& vp, const SpriteFrame& frame, int x, int y, std::uint32_t id, bool dim,
                        const VgaPalette& palette, std::span<const std::uint8_t> allowed) {
  if (!vp.valid() || allowed.empty()) {
    return;
  }
  const auto near_allowed = [&](Rgbf c) {
    std::uint8_t best = allowed.front();
    float best_d = 1e30f;
    for (const std::uint8_t i : allowed) {
      const float d = distance(colour8(palette, i), c);
      if (d < best_d) {
        best_d = d;
        best = i;
      }
    }
    return best;
  };
  const Rgb tint = torpedo_type_colour(id);
  const float k = dim ? 0.5f : 1.0f;
  std::array<std::uint8_t, 256> ring{};
  for (int i = 0; i < 256; ++i) {
    ring[static_cast<std::size_t>(i)] =
        near_allowed(scaled(tint, std::min(1.3f, lightness(colour8(palette, i)) * 1.6f) * k));
  }
  const std::uint8_t notch = near_allowed({250 * k, 250 * k, 250 * k});
  const DecodedFrame d = frame.decode();
  const int w = d.image.width();
  const int h = d.image.height();
  Image8& target = *vp.bitmap;
  const ClipRect clip = vp.clip();
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      if (d.opaque.at(i, j) == 0) {
        continue;
      }
      std::uint8_t c = d.image.at(i, j);
      const Rgb src = colour8(palette, c);
      const bool lamp = src.r > 2 * src.g && src.r > 2 * src.b;
      if (!lamp && on_button_bezel(i, j, w, h)) {
        c = on_button_notch(id, i, j, w) ? notch : ring[c];
      }
      const int dx = vp.x0 + x + frame.x0 + i;
      const int dy = vp.y0 + y + frame.y0 + j;
      if (clip.contains(dx, dy)) {
        target.set(dx, dy, c);
      }
    }
  }
}

void add_tbt_type_colours(VgaPalette& palette) {
  static constexpr std::array<std::uint8_t, 6> kEntries{223, 238, 239, 240, 241, 242};
  static constexpr std::array<Rgb, 6> kColours{Rgb{0x38, 0x50, 0x70}, Rgb{0xa8, 0xc8, 0xf0}, Rgb{0x80, 0x40, 0x00},
                                              Rgb{0xfc, 0xa8, 0x30}, Rgb{0x18, 0x60, 0x28}, Rgb{0x70, 0xe8, 0x90}};
  for (std::size_t i = 0; i < kEntries.size(); ++i) {
    const int e = kEntries[i];
    if (!placeholder(colour8(palette, e))) {
      continue;
    }
    const auto u = static_cast<std::size_t>(e) * 3;
    palette[u] = static_cast<std::uint8_t>(kColours[i].r >> 2);
    palette[u + 1] = static_cast<std::uint8_t>(kColours[i].g >> 2);
    palette[u + 2] = static_cast<std::uint8_t>(kColours[i].b >> 2);
  }
}

}  // namespace sh
