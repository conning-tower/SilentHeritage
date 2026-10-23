// Writers of the original CDs' containers for the tests of
// core/arj.h, core/iso9660.h and core/game_media.h: ARJ archives (stored,
// method 1 and method 4 compression) and ISO 9660 images.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sh::test {

// Returns `data` compressed with ARJ method `method`: 0 (stored), 1 to 3
// (one encoder: greedy matches, a Huffman block per 65535 codes) or 4.
std::vector<std::uint8_t> arj_compress(std::span<const std::uint8_t> data, std::uint8_t method);

// One file of an archive.
struct ArjFileSpec {
  std::string path;  // '/' separated (stored with '\\')
  std::vector<std::uint8_t> data;
  std::uint8_t method = 1;
  // The header flags: 0x01 garbled, 0x04 continued in the next volume,
  // 0x08 continuing the previous one (`continued_at` then written).
  std::uint8_t flags = 0;
  std::uint32_t continued_at = 0;
  bool directory = false;
};

// Returns an ARJ archive (one volume) of `files` named `name`, its main
// header's modification date set (the bytes a file header has for its
// packed size), after `prefix` (a self-extractor's code).
std::vector<std::uint8_t> write_arj(const std::string& name, const std::vector<ArjFileSpec>& files,
                                    std::span<const std::uint8_t> prefix = {});

// One file of an image.
struct IsoFileSpec {
  std::string path;  // '/' separated, upper case 8.3 names
  std::vector<std::uint8_t> data;
};

// Returns an ISO 9660 image of `files` (names stored with ";1"), its
// directories made from the paths.
std::vector<std::uint8_t> write_iso(const std::vector<IsoFileSpec>& files);

}  // namespace sh::test
