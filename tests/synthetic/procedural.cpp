#include "synthetic/procedural.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

namespace sh::test {

namespace {

// The 5 x 7 glyphs: seven rows of five bits (bit 4 the leftmost pixel).
struct Glyph5x7 {
  char c;
  std::array<std::uint8_t, 7> rows;
};

constexpr Glyph5x7 kGlyphs[] = {
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}}, {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}}, {'3', {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}}, {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}}, {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}}, {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}}, {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}}, {'D', {0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}}, {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f}}, {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}}, {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0c}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}}, {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'N', {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}}, {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}}, {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}}, {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04}}, {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}}, {',', {0x00, 0x00, 0x00, 0x00, 0x0c, 0x04, 0x08}},
    {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}}, {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00}}, {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}}, {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}}, {'#', {0x0a, 0x0a, 0x1f, 0x0a, 0x1f, 0x0a, 0x0a}},
    {'?', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}}, {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}}, {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}},
};

std::uint64_t mix(std::uint64_t z) {
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}

double lattice(std::int64_t x, std::int64_t y, std::uint32_t seed) {
  const std::uint64_t h = mix(static_cast<std::uint64_t>(x) * 0x9e3779b97f4a7c15ull ^
                              static_cast<std::uint64_t>(y) * 0xc2b2ae3d27d4eb4full ^ seed);
  return static_cast<double>(h >> 11) / static_cast<double>(1ull << 53);
}

double smooth(double t) { return t * t * (3 - 2 * t); }

}  // namespace

SynthRandom SynthRandom::fork(std::string_view name) const { return SynthRandom(mix(state_ ^ name_hash(name))); }

std::uint64_t SynthRandom::next() {
  state_ += 0x9e3779b97f4a7c15ull;
  return mix(state_);
}

std::int32_t SynthRandom::range(std::int32_t lo, std::int32_t hi) {
  const auto span = static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - lo + 1);
  return static_cast<std::int32_t>(lo + static_cast<std::int64_t>(next() % span));
}

double SynthRandom::unit() { return static_cast<double>(next() >> 11) / static_cast<double>(1ull << 53); }

std::uint32_t name_hash(std::string_view text) {
  std::uint32_t h = 0x811c9dc5u;
  for (const char c : text) {
    h = (h ^ static_cast<std::uint8_t>(c)) * 0x01000193u;
  }
  return h;
}

double value_noise(double x, double y, std::uint32_t seed) {
  const double fx = std::floor(x);
  const double fy = std::floor(y);
  const auto ix = static_cast<std::int64_t>(fx);
  const auto iy = static_cast<std::int64_t>(fy);
  const double tx = smooth(x - fx);
  const double ty = smooth(y - fy);
  const double a = lattice(ix, iy, seed);
  const double b = lattice(ix + 1, iy, seed);
  const double c = lattice(ix, iy + 1, seed);
  const double d = lattice(ix + 1, iy + 1, seed);
  return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

double fractal_noise(double x, double y, std::uint32_t seed, int octaves) {
  double sum = 0;
  double weight = 1;
  double total = 0;
  for (int i = 0; i < octaves; ++i) {
    sum += weight * value_noise(x, y, seed + static_cast<std::uint32_t>(i) * 7919u);
    total += weight;
    x *= 2;
    y *= 2;
    weight /= 2;
  }
  return sum / total;
}

VgaPalette synthetic_palette(std::uint32_t variant, bool red) {
  VgaPalette pal{};
  const auto set = [&pal](int i, double r, double g, double b) {
    const auto c = [](double v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0, 1.0) * 63.0 + 0.5); };
    pal[static_cast<std::size_t>(3 * i)] = c(r);
    pal[static_cast<std::size_t>(3 * i + 1)] = c(g);
    pal[static_cast<std::size_t>(3 * i + 2)] = c(b);
  };
  for (int i = 1; i < 16; ++i) {
    const double v = i / 15.0;
    set(i, v, red ? 0 : v, red ? 0 : v);
  }
  for (int ramp = 0; ramp < 15; ++ramp) {
    // Hues around the wheel, shifted by the variant.
    const double hue = std::fmod(ramp / 15.0 + (variant % 97) / 97.0, 1.0) * 6.0;
    const double r = std::clamp(std::abs(hue - 3.0) - 1.0, 0.0, 1.0);
    const double g = std::clamp(2.0 - std::abs(hue - 2.0), 0.0, 1.0);
    const double b = std::clamp(2.0 - std::abs(hue - 4.0), 0.0, 1.0);
    for (int k = 0; k < 16; ++k) {
      const double light = (k + 1) / 16.0;
      set(16 + ramp * 16 + k, red ? light : r * light, red ? 0 : g * light, red ? 0 : b * light);
    }
  }
  set(255, 1, red ? 0 : 1, red ? 0 : 1);
  return pal;
}

bool font5x7(char c, int x, int y) {
  if (x < 0 || x >= 5 || y < 0 || y >= 7) {
    return false;
  }
  const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (const Glyph5x7& g : kGlyphs) {
    if (g.c == upper) {
      return ((g.rows[static_cast<std::size_t>(y)] >> (4 - x)) & 1) != 0;
    }
  }
  return false;
}

void draw_label(Image8& image, int x, int y, std::string_view text, int scale, std::uint8_t colour) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const int gx = x + static_cast<int>(i) * 6 * scale;
    for (int py = 0; py < 7 * scale; ++py) {
      for (int px = 0; px < 5 * scale; ++px) {
        if (font5x7(text[i], px / scale, py / scale) && image.contains(gx + px, y + py)) {
          image.set(gx + px, y + py, colour);
        }
      }
    }
  }
}

}  // namespace sh::test
