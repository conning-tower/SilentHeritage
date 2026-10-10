// The original's pseudo-random generator (Watcom C library rand/srand).
#pragma once

#include <cstdint>

namespace sh {

// Reproduces the Watcom `rand` (0xbdb16) and `srand` (0xbdb3a) sequence: a
// 32-bit LCG (seed * 0x41c64e6d + 0x3039) returning bits 16..30. The game
// seeds it from the clock at startup (`seed_random`); a fixed seed makes a run
// reproducible. The default seed is the C library's initial state, 1.
class WatcomRandom {
 public:
  static constexpr int kRandMax = 0x7fff;

  WatcomRandom() = default;
  explicit WatcomRandom(std::uint32_t seed) : seed_(seed) {}

  void srand(std::uint32_t seed) { seed_ = seed; }
  // Returns the generator state, which `srand` restores (the saves keep it).
  std::uint32_t state() const { return seed_; }

  // Returns the next value in [0, 0x7fff].
  int rand() {
    seed_ = seed_ * 0x41c64e6du + 0x3039u;
    return static_cast<int>((seed_ >> 16) & 0x7fffu);
  }

  // Returns rand() % n, or 0 when n is 0 (`random_int`, 0x59078).
  int random_int(int n) { return n != 0 ? rand() % n : 0; }

 private:
  std::uint32_t seed_ = 1;
};

}  // namespace sh
