// The procedural building blocks of the synthetic install: a seeded
// generator, smooth noise, palettes and a small bitmap font for labels.
// Nothing here comes from the original game.
#pragma once

#include <cstdint>
#include <string_view>

#include "assets/pal.h"
#include "core/image.h"

namespace sh::test {

// A small seeded generator (SplitMix64), independent of the game's.
class SynthRandom {
 public:
  explicit SynthRandom(std::uint64_t seed) : state_(seed) {}
  // Returns a generator for `name` derived from this one's seed (the same
  // name always gives the same sequence, whatever was drawn before).
  SynthRandom fork(std::string_view name) const;

  std::uint64_t next();
  // Returns a value in [lo, hi] (inclusive).
  std::int32_t range(std::int32_t lo, std::int32_t hi);
  // Returns a value in [0, 1).
  double unit();

 private:
  std::uint64_t state_;
};

// Returns the 32-bit FNV-1a hash of `text` (stable names to numbers).
std::uint32_t name_hash(std::string_view text);

// Returns smooth value noise in [0, 1] at (x, y) (one lattice cell per
// unit), from `seed`.
double value_noise(double x, double y, std::uint32_t seed);
// Returns `octaves` of value noise summed (each at twice the frequency and
// half the weight), in [0, 1].
double fractal_noise(double x, double y, std::uint32_t seed, int octaves);

// Returns the synthetic 6-bit palette `variant`: 0 black, 1-15 greys, then
// fifteen 16-colour ramps of different hues (dark to light), 255 white.
// `red` makes every ramp a shade of red (the red-light palettes).
VgaPalette synthetic_palette(std::uint32_t variant, bool red = false);

// Returns whether pixel (x, y) of the 5 x 7 glyph of `c` is set (blank for
// characters it lacks).
bool font5x7(char c, int x, int y);

// Draws `text` in the 5 x 7 font at (x, y) of `image`, each font pixel
// `scale` x `scale` image pixels, in `colour`; clipped to the image.
void draw_label(Image8& image, int x, int y, std::string_view text, int scale, std::uint8_t colour);

}  // namespace sh::test
