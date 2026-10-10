// The game's generic compressed blocks: an RLE90 stream packed with 9-11 bit
// LZW, read by `dfile_read_compressed` (0x39355); docs/formats.md
// "Compressed blocks".
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/byte_reader.h"

namespace sh {

// The RLE90 escape byte: `c 0x90 n` repeats `c` to `n` copies in total and
// `0x90 0x00` is a literal 0x90.
inline constexpr std::uint8_t kRle90Mark = 0x90;

// The output of `lzw_rle90_decode`.
struct DecodedStream {
  std::vector<std::uint8_t> bytes;
  // Offset in the input of the first byte the game's reader leaves unread.
  std::size_t consumed = 0;
};

// Decodes `size` bytes from `stream`, which starts at the `max_bits` byte of
// a block (just past its `u32 size`). The code stream has no length of its
// own: decoding stops after `size` output bytes. Throws `DataError` if the
// stream ends early or is malformed.
DecodedStream lzw_rle90_decode(std::span<const std::uint8_t> stream, std::size_t size);

// Reads one compressed block (`u32 size` + code stream) at the reader's
// position and returns its decompressed bytes; the reader is left where the
// game's reader would stop. A zero size is an empty block with no stream.
std::vector<std::uint8_t> read_compressed(ByteReader& reader);

// Expands `data` with the RLE90 scheme alone (no LZW layer), as the `.WLD`
// chart rows are stored. Throws `DataError` on a run with nothing before it
// or a trailing escape byte.
std::vector<std::uint8_t> rle90_decode(std::span<const std::uint8_t> data);

}  // namespace sh
