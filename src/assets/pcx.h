// ZSoft PCX images (`.PCX`, and the `.PCC` textures and `.PAL` red-light
// palettes, which are plain PCX files too), read the way the game's
// `load_pcx` (0x589cc) does with `pcx_draw` (0xaeb13) and `pcx_get_palette`
// (0xaeb95).
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "assets/pal.h"
#include "core/image.h"

namespace sh {

class Install;

// A decoded PCX: the picture and its palette as the game stores it.
struct PcxImage {
  // `bytes_per_line` x `ymax - ymin + 1` pixels: the game draws whole
  // decoded lines (for every original file, `bytes_per_line` equals
  // `xmax - xmin + 1`).
  Image8 image;
  // The last 768 bytes of the file shifted right by 2 (8-bit to 6-bit VGA),
  // as `load_pcx` loads them into the screen palette; the 0x0C marker is
  // not checked, like the original.
  VgaPalette palette{};
};

// Decodes the 8-bit single-plane RLE PCX in `data`; throws `DataError`
// naming `context` for other variants or truncated data. Each line is
// decoded on its own: a run spilling past `bytes_per_line` is cut there.
PcxImage decode_pcx(std::span<const std::uint8_t> data, std::string_view context);

// Returns the 6-bit palette of the PCX in `data` (`pcx_get_palette`).
VgaPalette pcx_palette(std::span<const std::uint8_t> data, std::string_view context);

// Reads and decodes the PCX file at `dos_path` of `install`.
PcxImage load_pcx(const Install& install, std::string_view dos_path);

}  // namespace sh
