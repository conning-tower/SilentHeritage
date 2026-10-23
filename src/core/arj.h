// ARJ archives (ARJ 2.x, Robert K. Jung), the form the game takes on the
// original CDs before INSTALL.EXE unpacks it: SH/SH.A00, SH.A01... each a
// complete archive (a volume of a set), whose files may continue from one
// volume to the next. Methods 0 (stored), 1 to 3 (LZ77 + Huffman, the
// "-lh6-" family) and 4 (LZ77 + unary codes) are read; garbled
// (password-protected) files are not.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/byte_source.h"

namespace sh {

// One file (or directory) of an archive.
struct ArjEntry {
  std::string path;  // '/' separated, as stored
  std::uint8_t method = 0;
  bool directory = false;
  bool garbled = false;
  // The file continues in the next volume / continues one of the previous
  // volume (starting at `continued_at`, its offset in the whole file).
  bool continues = false;
  bool continuation = false;
  std::uint32_t continued_at = 0;
  std::uint32_t original_size = 0;
  std::uint32_t crc = 0;  // CRC-32 of the original bytes (of this part)
  std::uint64_t data_offset = 0;
  std::uint32_t packed_size = 0;
};

// One volume of an archive, its headers read at construction.
class ArjArchive {
 public:
  // Throws `DataError` when `source` is not an ARJ archive or its headers
  // are corrupt.
  explicit ArjArchive(std::shared_ptr<const ByteSource> source);

  const std::vector<ArjEntry>& entries() const { return entries_; }

  // Returns the original bytes of `entry` (one of `entries()`; for a file
  // split between volumes, the part held here). Throws `DataError` for a
  // garbled file, an unknown method, corrupt data or a CRC mismatch.
  std::vector<std::uint8_t> extract(const ArjEntry& entry) const;

  std::string describe() const { return source_->describe(); }

 private:
  std::shared_ptr<const ByteSource> source_;
  std::vector<ArjEntry> entries_;
};

// Returns the `original_size` bytes `packed` decompresses to with ARJ
// method `method` (0 to 4); throws `DataError` naming `context` when the
// method is unknown or the data is corrupt.
std::vector<std::uint8_t> arj_decompress(std::span<const std::uint8_t> packed, std::uint8_t method,
                                         std::size_t original_size, const std::string& context);

}  // namespace sh
