#include "platform/screenshot.h"

#include <format>
#include <fstream>
#include <stdexcept>

namespace sh {

namespace {

constexpr std::uint32_t kFileHeaderSize = 14;
constexpr std::uint32_t kInfoHeaderSize = 40;
constexpr std::uint32_t kPaletteSize = 256 * 4;

void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xff));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>((v >> shift) & 0xff));
  }
}

}  // namespace

std::vector<std::uint8_t> encode_bmp(const Image8& image, const Palette& palette) {
  const auto width = static_cast<std::uint32_t>(image.width());
  const auto height = static_cast<std::uint32_t>(image.height());
  const std::uint32_t stride = (width + 3) & ~3u;
  const std::uint32_t data_offset = kFileHeaderSize + kInfoHeaderSize + kPaletteSize;
  const std::uint32_t file_size = data_offset + stride * height;

  std::vector<std::uint8_t> out;
  out.reserve(file_size);
  out.push_back('B');
  out.push_back('M');
  put32(out, file_size);
  put32(out, 0);  // reserved
  put32(out, data_offset);

  put32(out, kInfoHeaderSize);
  put32(out, width);
  put32(out, height);  // positive: bottom-up rows
  put16(out, 1);       // planes
  put16(out, 8);       // bits per pixel
  put32(out, 0);       // BI_RGB
  put32(out, stride * height);
  put32(out, 2835);  // 72 dpi
  put32(out, 2835);
  put32(out, 256);  // colours used
  put32(out, 0);    // important colours: all

  for (const Rgb& c : palette.colors) {
    out.push_back(c.b);
    out.push_back(c.g);
    out.push_back(c.r);
    out.push_back(0);
  }
  for (int y = image.height() - 1; y >= 0; --y) {
    const auto row = image.row(y);
    out.insert(out.end(), row.begin(), row.end());
    out.insert(out.end(), stride - width, 0);
  }
  return out;
}

void save_bmp(const std::filesystem::path& path, const Image8& image, const Palette& palette) {
  const std::vector<std::uint8_t> data = encode_bmp(image, palette);
  std::ofstream out(path, std::ios::binary);
  if (!out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()))) {
    throw std::runtime_error(std::format("cannot write {}", path.string()));
  }
}

}  // namespace sh
