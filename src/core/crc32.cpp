#include "core/crc32.h"

#include <array>

namespace sh {

namespace {

constexpr std::array<std::uint32_t, 256> make_crc_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t n = 0; n < 256; ++n) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    table[n] = c;
  }
  return table;
}
constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

}  // namespace

std::uint32_t crc32(std::span<const std::uint8_t> data) {
  std::uint32_t c = 0xffffffffu;
  for (const std::uint8_t b : data) {
    c = kCrcTable[(c ^ b) & 0xffu] ^ (c >> 8);
  }
  return c ^ 0xffffffffu;
}

}  // namespace sh
