// The game's working palette format: 256 VGA DAC entries of 6 bits.
//
// The original keeps every screen palette (e.g. 0x11a274) as 768 bytes of
// 6-bit DAC values: `load_pcx` stores a PCX palette shifted right by 2, the
// sprite frame palettes and the `.ENV`/`.WLD` palettes are 6-bit already, and
// the palette effects work on these values. Only uploading to the display
// scales them to 8 bits (`Palette::from_vga6`, `v * 255 / 63`).
#pragma once

#include <array>
#include <cstdint>

#include "core/image.h"

namespace sh {

// 256 RGB triplets of 6-bit VGA DAC values (0..63), index-major.
using VgaPalette = std::array<std::uint8_t, 768>;

// Returns `pal` as the 8-bit palette the display shows.
inline Palette to_palette(const VgaPalette& pal) { return Palette::from_vga6(pal); }

}  // namespace sh
