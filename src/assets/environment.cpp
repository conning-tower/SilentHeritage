#include "assets/environment.h"

#include <algorithm>
#include <format>

#include "assets/exe_tables.h"
#include "assets/format_header.h"
#include "assets/game_exe.h"
#include "core/byte_reader.h"
#include "core/error.h"

namespace sh {
namespace {

constexpr std::size_t kPcxSizeOffset = 0x300;
constexpr std::size_t kSeaShiftOffset = 0x304;  // texture scale shift of the sea
constexpr std::size_t kDistanceRemapOffset = 0x3ea;
constexpr std::size_t kDistanceThresholdOffset = 0x9ec;
constexpr std::size_t kSeaRampOffset = 0xa04;
constexpr std::size_t kSeaColorsOffset = 0xbeb;

std::size_t sea_index(SeaState state) {
  const auto s = static_cast<std::int32_t>(state);
  if (s < 0 || s >= kSeaStateCount) {
    throw DataError(std::format("invalid sea state {}", s));
  }
  return static_cast<std::size_t>(s);
}

// Copies palette entries `first`..`last` (inclusive) of `src` into `dst`.
void copy_range(std::span<std::uint8_t, 0x300> dst, std::span<const std::uint8_t> src, int first, int last) {
  const auto begin = static_cast<std::size_t>(first) * 3;
  const auto count = static_cast<std::size_t>(last - first + 1) * 3;
  std::ranges::copy(src.subspan(begin, count), dst.begin() + static_cast<std::ptrdiff_t>(begin));
}

}  // namespace

std::span<const std::uint8_t, 256> Environment::distance_remap(int i) const {
  if (i < 0 || i >= kDistanceRemapCount) {
    throw DataError(std::format("distance remap {} out of range", i));
  }
  return std::span<const std::uint8_t, 256>(header.data() + kDistanceRemapOffset + 256 * static_cast<std::size_t>(i), 256);
}

std::uint32_t Environment::distance_threshold(int i) const {
  if (i < 0 || i >= kDistanceRemapCount) {
    throw DataError(std::format("distance threshold {} out of range", i));
  }
  return read_u32le(header, kDistanceThresholdOffset + 4 * static_cast<std::size_t>(i));
}

std::span<const std::uint8_t, 30> Environment::sea_color_ramp(SeaState state, int ramp) const {
  if (ramp < 0 || ramp >= 4) {
    throw DataError(std::format("sea colour ramp {} out of range", ramp));
  }
  const std::size_t offset = kSeaRampOffset + 120 * sea_index(state) + 30 * static_cast<std::size_t>(ramp);
  return std::span<const std::uint8_t, 30>(header.data() + offset, 30);
}

std::span<const std::uint8_t, 90> Environment::sea_state_colors(SeaState state) const {
  return std::span<const std::uint8_t, 90>(header.data() + kSeaColorsOffset + 90 * sea_index(state), 90);
}

void Environment::apply_world_palette(std::span<std::uint8_t, 0x300> vga6, SeaState state) const {
  // The first three ranges of palette_copy_range's table (SH.EXE 0x10febc,
  // first..last pairs), the ones world_load_aenv copies.
  constexpr std::uint32_t kRanges = 0x10febc;
  const auto ranges = exe_i32_array<6>(game_exe(), kRanges);
  for (std::size_t i = 0; i < 6; i += 2) {
    check_exe_table(ranges[i] >= 0 && ranges[i] <= ranges[i + 1] && ranges[i + 1] < 256, "palette range", kRanges);
    copy_range(vga6, palette_vga6(), ranges[i], ranges[i + 1]);
  }
  std::ranges::copy(sea_state_colors(state), vga6.begin() + 130 * 3);
}

Environment parse_environment(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  Environment env;
  env.version = read_format_header(r, "AENV", 8);
  std::ranges::copy(r.bytes(Environment::kHeaderSize), env.header.begin());
  // The fields the sea drawing uses as indices and as a shift.
  if (const std::uint8_t shift = env.header[kSeaShiftOffset]; shift >= 32) {
    r.fail(std::format("sea texture shift {} out of range", shift));
  }
  for (const std::uint8_t band : env.flat_sea_row_bands()) {
    if (band >= env.flat_sea_band_colors().size()) {
      r.fail(std::format("flat sea band {} out of range", band));
    }
  }
  const std::uint32_t pcx_size = read_u32le(env.header, kPcxSizeOffset);
  const auto pcx = r.bytes(pcx_size);
  env.sky_pcx.assign(pcx.begin(), pcx.end());
  std::ranges::copy(r.bytes(Environment::kSunSize), env.sun.begin());
  if (env.version >= 9) {
    env.moon_phases.resize(4);
    for (auto& t : env.moon_phases) {
      std::ranges::copy(r.bytes(Environment::kMoonSize), t.begin());
    }
  }
  return env;
}

}  // namespace sh
