// Screenshots: an indexed frame saved as an 8-bit palettized `.bmp` file.
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/image.h"

namespace sh {

// Returns `image` with `palette` encoded as an uncompressed 8-bit BMP
// (BITMAPINFOHEADER, 256 colours, bottom-up rows padded to 4 bytes).
std::vector<std::uint8_t> encode_bmp(const Image8& image, const Palette& palette);

// Writes `encode_bmp(image, palette)` to `path`; throws std::runtime_error if
// the file cannot be written.
void save_bmp(const std::filesystem::path& path, const Image8& image, const Palette& palette);

}  // namespace sh
