// Sound effects (`SFX/<id>.MEL`) and sample banks (`SFX/<id>.BNK`), the
// digital samples of the MicroProse sound API (MEL) over Miles AIL 3. See
// docs/formats.md.
//
//   .MEL: 0x18-byte `MelHeader`, then unsigned 8-bit mono PCM to the end.
//   .BNK: 0x24-byte records {i32 id, u32 size, u32 pointer, MelHeader},
//         ended by a free record (id -1) which must be the last one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/byte_reader.h"

namespace sh {

inline constexpr std::size_t kMelHeaderSize = 0x18;
inline constexpr std::size_t kBankEntrySize = 0x24;
// `MelHeader::rate` value meaning a random rate per play in [rate_min, rate_max).
inline constexpr std::int32_t kMelRandomRate = -1;

// The sample header shared by `.MEL` files and `.BNK` records.
struct MelHeader {
  // Playback rate in Hz, or `kMelRandomRate`.
  std::int32_t rate = 0;
  std::int32_t rate_max = 0;  // Only meaningful for a random rate.
  std::int32_t rate_min = 0;
  // AIL loop count: 1 plays once, 2 twice, -1 loops until stopped.
  std::int32_t loop_count = 1;
  // Added to the requested volume when the effect starts (0 or -20).
  std::int32_t volume_offset = 0;

  bool random_rate() const { return rate == kMelRandomRate; }
  // Returns the fixed rate, or the middle of the random range.
  std::int32_t nominal_rate() const { return random_rate() ? (rate_min + rate_max) / 2 : rate; }
};

// One `.MEL` sample.
struct MelSample {
  MelHeader header;
  std::vector<std::uint8_t> pcm;  // Unsigned 8-bit mono.
};

// One `.BNK` record (the in-memory sample-table entry as saved in the file).
struct BankEntry {
  std::int32_t sample_id = 0;
  std::uint32_t size = 0;     // 0 in the file.
  std::uint32_t pointer = 0;  // 0 in the file.
  MelHeader header;           // Stale copy: the loader overwrites it from the .MEL.
};

// Reads a sample header at the reader's position. Throws `DataError` if the
// rate is outside 4000..48000 Hz or a random range is not 0 < min < max.
MelHeader read_mel_header(ByteReader& r);

// Returns the sample stored in the `.MEL` file `data`.
MelSample parse_mel(std::span<const std::uint8_t> data, std::string_view context = ".MEL");

// Returns the records of the `.BNK` file `data` up to (excluding) the
// terminating free record.
std::vector<BankEntry> parse_bnk(std::span<const std::uint8_t> data, std::string_view context = ".BNK");

// Returns the install path of effect `id` in `sfx_dir` ("SFX/" by default),
// as api_resource_path (0xb697b) builds it: "<dir><id>.MEL".
std::string mel_path(std::int32_t id, std::string_view sfx_dir = "SFX/");
// Returns the install path of bank `id`: "<dir><id>.BNK".
std::string bnk_path(std::int32_t id, std::string_view sfx_dir = "SFX/");

}  // namespace sh
