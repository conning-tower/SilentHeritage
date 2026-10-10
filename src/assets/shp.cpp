#include "assets/shp.h"

#include <algorithm>
#include <format>

#include "core/byte_reader.h"
#include "core/error.h"
#include "core/install.h"

namespace sh {

namespace {

constexpr std::size_t kFrameHeader = 0x18;

// Calls `run(x, n, literal, p)` for each run or literal of a row (`p` points
// at the run byte or the literal bytes) and returns the position after the
// row's end marker. Throws `DataError` if the row overruns `width` or `data`.
template <typename Run>
std::size_t walk_row(std::span<const std::uint8_t> data, std::size_t p, std::int64_t width, Run&& run,
                     std::string_view context) {
  std::int64_t x = 0;
  for (;;) {
    if (p >= data.size()) {
      throw DataError(std::format("{}: sprite row runs past the end of the data", context));
    }
    const std::uint8_t b = data[p++];
    const int n = b >> 1;
    const bool literal = (b & 1) != 0;
    if (n == 0) {
      if (!literal) {
        return p;
      }
      if (p >= data.size()) {
        throw DataError(std::format("{}: sprite row runs past the end of the data", context));
      }
      x += data[p++];
      continue;
    }
    const std::size_t len = literal ? static_cast<std::size_t>(n) : 1;
    if (x + n > width || p + len > data.size()) {
      throw DataError(std::format("{}: sprite row overruns its frame", context));
    }
    run(static_cast<int>(x), n, literal, p);
    p += len;
    x += n;
  }
}

}  // namespace

DecodedFrame SpriteFrame::decode() const {
  DecodedFrame out{Image8(width(), height()), Image8(width(), height())};
  std::size_t p = 0;
  for (int y = 0; y < height(); ++y) {
    p = walk_row(
        rle, p, width(),
        [&](int x, int n, bool literal, std::size_t at) {
          for (int i = 0; i < n; ++i) {
            out.image.set(x + i, y, rle[literal ? at + static_cast<std::size_t>(i) : at]);
            out.opaque.set(x + i, y, 1);
          }
        },
        "sprite frame");
  }
  return out;
}

Shp Shp::parse(std::span<const std::uint8_t> data, std::string_view context) {
  ByteReader r(data, context);
  Shp shp;
  shp.version_ = std::string(reinterpret_cast<const char*>(r.bytes(4).data()), 4);
  if (!shp.version_.starts_with("1.")) {
    r.fail(std::format("not a sprite set (version \"{}\")", shp.version_));
  }
  const std::uint32_t count = r.u32();
  if (count > r.remaining() / 8) {
    r.fail("frame table runs past the end of the data");
  }
  std::vector<std::uint32_t> offsets(count);
  std::vector<std::uint32_t> palettes(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    offsets[i] = r.u32();
    palettes[i] = r.u32();
  }
  // A frame's data ends where the next frame (in file order) starts.
  std::vector<std::uint32_t> starts = offsets;
  std::ranges::sort(starts);
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

  shp.frames_.resize(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    SpriteFrame& f = shp.frames_[i];
    const std::size_t off = offsets[i];
    const auto next = std::ranges::upper_bound(starts, offsets[i]);
    const std::size_t end = next == starts.end() ? data.size() : *next;
    ByteReader h(data, context);
    h.seek(off);
    h.skip(8);
    f.x0 = h.i32();
    f.y0 = h.i32();
    f.x1 = h.i32();
    f.y1 = h.i32();
    const std::int64_t width = static_cast<std::int64_t>(f.x1) - f.x0 + 1;
    const std::int64_t height = static_cast<std::int64_t>(f.y1) - f.y0 + 1;
    if (width > 0 && height > 0 && end > off + kFrameHeader) {
      // A frame with rows must have a 16-bit box; one without rows draws
      // nothing whatever its box (`SBCHART.SHP` #42's lies near 2^31).
      constexpr std::int32_t kLimit = 0x7fff;
      const auto outside = [](std::int32_t v) { return v < -kLimit || v > kLimit; };
      if (outside(f.x0) || outside(f.y0) || outside(f.x1) || outside(f.y1) || width > kLimit || height > kLimit) {
        h.fail(std::format("frame {} box ({}, {})-({}, {}) is out of range", i, f.x0, f.y0, f.x1, f.y1));
      }
      const auto rows = data.subspan(off + kFrameHeader, std::min(end, data.size()) - off - kFrameHeader);
      std::size_t p = 0;
      for (std::int64_t y = 0; y < height; ++y) {
        p = walk_row(rows, p, width, [](int, int, bool, std::size_t) {}, context);
      }
      f.rle.assign(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(p));
    }
    if (palettes[i] != 0) {
      ByteReader pr(data, context);
      pr.seek(palettes[i]);
      const std::uint32_t n = pr.u32();
      if (n > pr.remaining() / 4) {
        pr.fail("frame palette runs past the end of the data");
      }
      f.palette.resize(n);
      for (ShpPaletteEntry& e : f.palette) {
        e.index = pr.u8();
        e.r = pr.u8();
        e.g = pr.u8();
        e.b = pr.u8();
      }
    }
  }
  return shp;
}

Shp Shp::load(const Install& install, std::string_view dos_path) {
  return parse(install.read(dos_path), dos_path);
}

}  // namespace sh
