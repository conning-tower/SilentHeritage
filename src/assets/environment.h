// `.ENV` environments (`DATA/ENV-01..14.ENV`): the 3D view's world palette,
// sky panorama, sea colours and distance fades. Layout from `world_load_aenv`
// (0x90d89), which reads the header to 0x16d014; field uses from
// `world_build_env_color_table` (0x9292b), `world_draw_sea_plane` (0x92513),
// `world_apply_daylight_palette` and `veh_draw_ship_sprite`. docs/formats.md ".ENV"
// and "3D view".
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace sh {

// The sea state (0x170f28): picks the `.ENV` sea colours.
enum class SeaState : std::int32_t { kCalm = 0, kModerate = 1, kHeavy = 2 };
inline constexpr int kSeaStateCount = 3;

// A parsed `.ENV` file. The header is kept whole; its known fields have
// accessors, the rest is not understood (mostly zero).
struct Environment {
  static constexpr std::size_t kHeaderSize = 0x1158;
  static constexpr std::size_t kSunSize = 0x3c1;   // 31 x 31
  static constexpr std::size_t kMoonSize = 0x441;  // 33 x 33
  static constexpr int kDistanceRemapCount = 6;

  std::uint8_t version = 0;  // 9 shipped; >= 8 accepted
  std::array<std::uint8_t, kHeaderSize> header{};
  // The sky/cloud panorama (1280 x 65): a PCX file kept as raw bytes.
  std::vector<std::uint8_t> sky_pcx;
  // A 31 x 31 bitmap (0x16b6b0) the sky code draws at the sun's direction
  // (0x170f8c); its encoding is not decoded.
  std::array<std::uint8_t, kSunSize> sun{};
  // Four 33 x 33 bitmaps (0x16ba74, version >= 9) drawn at the moon's
  // direction, one per moon phase (0x170fbc, 4 = no moon); not decoded.
  std::vector<std::array<std::uint8_t, kMoonSize>> moon_phases;

  // Returns the 256-colour 6-bit VGA palette at header 0 (only 16-63 and
  // 80-129 are used).
  std::span<const std::uint8_t, 0x300> palette_vga6() const {
    return std::span<const std::uint8_t, 0x300>(header.data(), 0x300);
  }

  // Returns distance remap table `i` (0..5, header 0x3ea): a 256-entry colour
  // remap applied to ship sprites at distance band `i`.
  std::span<const std::uint8_t, 256> distance_remap(int i) const;

  // Returns the lower distance bound of remap band `i` (0..5, header 0x9ec),
  // in world units at scale shift 0; band `i` applies from it to the next.
  std::uint32_t distance_threshold(int i) const;

  // Returns the distance band (index into `flat_sea_band_colors`) of each of
  // the 130 screen rows below the horizon (header 0x368), used to fill a flat
  // sea when the sea texture is off.
  std::span<const std::uint8_t, 130> flat_sea_row_bands() const {
    return std::span<const std::uint8_t, 130>(header.data() + 0x368, 130);
  }

  // Returns the palette index of each flat-sea distance band (header 0xbe5).
  std::span<const std::uint8_t, 6> flat_sea_band_colors() const {
    return std::span<const std::uint8_t, 6>(header.data() + 0xbe5, 6);
  }

  // Returns ramp `ramp` (0..3) of the four 30-entry palette-index ramps of
  // `state` (header 0xa04 + 120 * state), from which the sea colour table is
  // built.
  std::span<const std::uint8_t, 30> sea_color_ramp(SeaState state, int ramp) const;

  // Returns the 30 6-bit VGA colours for palette indices 130-159 under
  // `state` (header 0xbeb + 90 * state).
  std::span<const std::uint8_t, 90> sea_state_colors(SeaState state) const;

  // Returns the ambient light minimum in percent (header 0x1153): the
  // darkest a night makes the shaded palette ranges.
  std::uint8_t ambient_minimum_pct() const { return header[0x1153]; }

  // Writes the world palette entries that `world_load_aenv` sets into the
  // 6-bit VGA palette `vga6`: 16-63 and 80-129 from the header palette and
  // 130-159 from the `state` sea colours. Other entries are left unchanged.
  void apply_world_palette(std::span<std::uint8_t, 0x300> vga6, SeaState state) const;
};

// Parses a whole `.ENV` file; `context` names it in errors. Throws
// `DataError` if it is malformed, including a sea texture shift of 32 or more
// or a flat-sea row band past the 6 band colours (both index the drawing).
Environment parse_environment(std::span<const std::uint8_t> data, std::string_view context);

}  // namespace sh
