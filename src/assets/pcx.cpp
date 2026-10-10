#include "assets/pcx.h"

#include <algorithm>
#include <format>
#include <vector>

#include "core/byte_reader.h"
#include "core/error.h"
#include "core/install.h"

namespace sh {

namespace {

constexpr std::size_t kHeader = 128;

}  // namespace

PcxImage decode_pcx(std::span<const std::uint8_t> data, std::string_view context) {
  if (data.size() < kHeader + 768 || data[0] != 0x0a || data[2] != 1 || data[3] != 8 || data[65] != 1) {
    throw DataError(std::format("{}: not an RLE 8-bit single-plane PCX", context));
  }
  // pcx_draw (0xaeb13): rows = ymax - ymin + 1 (u16), bytes_per_line pixels each.
  const std::uint16_t ymin = read_u16le(data, 6);
  const std::uint16_t ymax = read_u16le(data, 10);
  if (ymin > ymax) {
    throw DataError(std::format("{}: PCX ymin {} above ymax {}", context, ymin, ymax));
  }
  const int rows = ymax - ymin + 1;
  const int bpl = read_u16le(data, 0x42);
  // Each row takes at least one byte per 63 pixels: refuse sizes the data
  // cannot hold before allocating the image.
  if (static_cast<std::size_t>(rows) * static_cast<std::size_t>((bpl + 62) / 63) > data.size() - kHeader) {
    throw DataError(std::format("{}: PCX image data runs past the end of the file", context));
  }

  PcxImage out;
  out.image = Image8(bpl, rows);
  std::vector<std::uint8_t> line(static_cast<std::size_t>(bpl));
  std::size_t p = kHeader;
  auto next = [&]() {
    if (p >= data.size()) {
      throw DataError(std::format("{}: PCX image data runs past the end of the file", context));
    }
    return data[p++];
  };
  for (int y = 0; y < rows; ++y) {
    int x = 0;
    while (x < bpl) {
      const std::uint8_t c = next();
      if (c >= 0xc0) {
        const std::uint8_t v = next();
        for (int n = c & 0x3f; n > 0; --n, ++x) {
          if (x < bpl) {
            line[static_cast<std::size_t>(x)] = v;
          }
        }
      } else {
        line[static_cast<std::size_t>(x++)] = c;
      }
    }
    std::ranges::copy(line, out.image.row(y).begin());
  }
  out.palette = pcx_palette(data, context);
  return out;
}

VgaPalette pcx_palette(std::span<const std::uint8_t> data, std::string_view context) {
  if (data.size() < 768) {
    throw DataError(std::format("{}: PCX shorter than its palette", context));
  }
  // pcx_get_palette (0xaeb95): the last 768 bytes, >> 2.
  VgaPalette pal;
  const auto src = data.last(768);
  for (std::size_t i = 0; i < pal.size(); ++i) {
    pal[i] = static_cast<std::uint8_t>(src[i] >> 2);
  }
  return pal;
}

PcxImage load_pcx(const Install& install, std::string_view dos_path) {
  return decode_pcx(install.read(dos_path), dos_path);
}

}  // namespace sh
