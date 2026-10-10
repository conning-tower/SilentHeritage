// The header shared by the game's own formats (`AWLD`, `AOBJ`, `AENV`,
// `ASCN`): a 4-character magic, a NUL and a `u8` version.
#pragma once

#include <cstdint>
#include <format>
#include <string_view>

#include "core/byte_reader.h"

namespace sh {

// Reads the 6-byte header at the reader's position and returns its version.
// Throws `DataError` unless it holds `magic` (4 characters) and a version of
// at least `min_version` (the loaders' own minimum).
inline std::uint8_t read_format_header(ByteReader& reader, std::string_view magic,
                                       std::uint8_t min_version) {
  const auto raw = reader.bytes(5);
  if (std::string_view(reinterpret_cast<const char*>(raw.data()), 4) != magic || raw[4] != 0) {
    reader.fail(std::format("missing {} magic", magic));
  }
  const std::uint8_t version = reader.u8();
  if (version < min_version) {
    reader.fail(std::format("unsupported {} version {}", magic, version));
  }
  return version;
}

}  // namespace sh
