#include "synthetic/encoders.h"

#include <algorithm>
#include <stdexcept>

#include "assets/compressed.h"

namespace sh::test {

namespace {

constexpr std::uint32_t kLzwMaxBits = 11;

// Appends codes LSB first, as the game's LZW reader takes them.
class BitWriter {
 public:
  void put(std::uint32_t code, std::uint32_t bits) {
    for (std::uint32_t i = 0; i < bits; ++i) {
      if (bit_ == 0) {
        bytes_.push_back(0);
      }
      bytes_.back() = static_cast<std::uint8_t>(bytes_.back() | (((code >> i) & 1u) << bit_));
      bit_ = (bit_ + 1) & 7;
      ++total_;
    }
  }
  std::uint64_t total_bits() const { return total_; }
  std::vector<std::uint8_t>& bytes() { return bytes_; }

 private:
  std::vector<std::uint8_t> bytes_;
  std::uint32_t bit_ = 0;
  std::uint64_t total_ = 0;
};

// Writes one RLE row of a sprite frame: transparent skips, runs of three
// or more equal pixels, literals.
void sprite_row(Bytes& out, const SpriteImage& f, int y) {
  const int w = f.pixels.width();
  int x = 0;
  const auto opaque = [&](int at) { return !f.transparent || f.pixels.at(at, y) != *f.transparent; };
  while (x < w) {
    if (!opaque(x)) {
      int n = 0;
      while (x + n < w && n < 255 && !opaque(x + n)) {
        ++n;
      }
      out.u8(0x01);
      out.u8(static_cast<std::uint8_t>(n));
      x += n;
      continue;
    }
    const std::uint8_t v = f.pixels.at(x, y);
    int run = 1;
    while (x + run < w && run < 127 && opaque(x + run) && f.pixels.at(x + run, y) == v) {
      ++run;
    }
    if (run >= 3) {
      out.u8(static_cast<std::uint8_t>(run << 1));
      out.u8(v);
      x += run;
      continue;
    }
    // A literal up to the next run of three, transparent pixel or 127.
    int n = 0;
    while (x + n < w && n < 127 && opaque(x + n)) {
      const std::uint8_t c = f.pixels.at(x + n, y);
      if (x + n + 2 < w && opaque(x + n + 1) && opaque(x + n + 2) && f.pixels.at(x + n + 1, y) == c &&
          f.pixels.at(x + n + 2, y) == c && n > 0) {
        break;
      }
      ++n;
    }
    out.u8(static_cast<std::uint8_t>(n << 1 | 1));
    for (int i = 0; i < n; ++i) {
      out.u8(f.pixels.at(x + i, y));
    }
    x += n;
  }
  out.u8(0x00);
}

}  // namespace

void Bytes::u16(std::uint16_t v) {
  u8(static_cast<std::uint8_t>(v));
  u8(static_cast<std::uint8_t>(v >> 8));
}

void Bytes::u32(std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    u8(static_cast<std::uint8_t>(v >> (8 * i)));
  }
}

void Bytes::fixed_string(std::string_view s, std::size_t n) {
  const std::size_t len = std::min(s.size(), n - 1);
  data_.insert(data_.end(), s.begin(), s.begin() + static_cast<std::ptrdiff_t>(len));
  zeros(n - len);
}

void Bytes::format_header(std::string_view magic, std::uint8_t version) {
  fixed_string(magic, 5);
  u8(version);
}

void Bytes::patch_u32(std::size_t offset, std::uint32_t v) {
  for (std::size_t i = 0; i < 4; ++i) {
    data_.at(offset + i) = static_cast<std::uint8_t>(v >> (8 * i));
  }
}

std::vector<std::uint8_t> rle90_encode(std::span<const std::uint8_t> data) {
  std::vector<std::uint8_t> out;
  for (std::size_t i = 0; i < data.size();) {
    const std::uint8_t c = data[i];
    std::size_t run = 1;
    while (i + run < data.size() && data[i + run] == c) {
      ++run;
    }
    i += run;
    if (c == kRle90Mark) {
      for (std::size_t k = 0; k < run; ++k) {
        out.push_back(kRle90Mark);
        out.push_back(0);
      }
      continue;
    }
    out.push_back(c);
    std::size_t left = run - 1;
    while (left >= 3) {
      const std::size_t k = std::min<std::size_t>(left, 254);
      out.push_back(kRle90Mark);
      out.push_back(static_cast<std::uint8_t>(k + 1));
      left -= k;
    }
    out.insert(out.end(), left, c);
  }
  return out;
}

std::vector<std::uint8_t> encode_compressed(std::span<const std::uint8_t> data) {
  Bytes out;
  out.u32(static_cast<std::uint32_t>(data.size()));
  if (data.empty()) {
    return out.take();
  }
  // Literal codes only: every code still adds a dictionary entry, so the
  // code width follows the reader's.
  BitWriter bits;
  std::uint32_t width = 9;
  std::uint32_t mask = 0x1ff;
  std::uint32_t next = 0x100;
  for (const std::uint8_t b : rle90_encode(data)) {
    bits.put(b, width);
    if (++next > mask) {
      ++width;
      mask = mask << 1 | 1;
      if (width > kLzwMaxBits) {
        width = 9;
        mask = 0x1ff;
        next = 0x100;
      }
    }
  }
  // The reader takes its first 8 bits from the header word, then 16-bit
  // words as it needs them.
  const std::uint64_t total = bits.total_bits();
  const std::size_t words = total <= 8 ? 0 : static_cast<std::size_t>((total - 8 + 15) / 16);
  std::vector<std::uint8_t>& stream = bits.bytes();
  stream.resize(1 + 2 * words, 0);
  out.u8(kLzwMaxBits);
  out.bytes(stream);
  return out.take();
}

std::vector<std::uint8_t> encode_pcx(const Image8& image, const VgaPalette& palette) {
  Bytes out;
  out.u8(0x0a);
  out.u8(5);
  out.u8(1);
  out.u8(8);
  out.u16(0);
  out.u16(0);
  out.u16(static_cast<std::uint16_t>(image.width() - 1));
  out.u16(static_cast<std::uint16_t>(image.height() - 1));
  out.u16(72);
  out.u16(72);
  out.zeros(48);
  out.u8(0);
  out.u8(1);
  out.u16(static_cast<std::uint16_t>(image.width()));
  out.u16(1);
  out.zeros(128 - out.size());
  for (int y = 0; y < image.height(); ++y) {
    const auto row = image.row(y);
    for (std::size_t x = 0; x < row.size();) {
      const std::uint8_t v = row[x];
      std::size_t run = 1;
      while (x + run < row.size() && run < 63 && row[x + run] == v) {
        ++run;
      }
      if (run > 1 || v >= 0xc0) {
        out.u8(static_cast<std::uint8_t>(0xc0 | run));
      }
      out.u8(v);
      x += run;
    }
  }
  out.u8(0x0c);
  for (const std::uint8_t c : palette) {
    out.u8(static_cast<std::uint8_t>(c << 2 | c >> 4));
  }
  return out.take();
}

std::vector<std::uint8_t> encode_shp(std::span<const SpriteImage> frames) {
  Bytes out;
  out.fixed_string("1.10", 5);
  out.data().pop_back();  // "1.10" without its NUL
  out.u32(static_cast<std::uint32_t>(frames.size()));
  const std::size_t table = out.size();
  out.zeros(8 * frames.size());
  for (std::size_t i = 0; i < frames.size(); ++i) {
    const SpriteImage& f = frames[i];
    out.patch_u32(table + 8 * i, static_cast<std::uint32_t>(out.size()));
    const int w = f.pixels.width();
    const int h = f.pixels.height();
    const bool empty = w == 0 || h == 0;
    out.u16(static_cast<std::uint16_t>(empty ? 0 : h - 1));
    out.u16(static_cast<std::uint16_t>(empty ? 0 : w - 1));
    out.u32(0);
    out.i32(empty ? 0 : -f.hotspot_x);
    out.i32(empty ? 0 : -f.hotspot_y);
    out.i32(empty ? -1 : w - 1 - f.hotspot_x);
    out.i32(empty ? -1 : h - 1 - f.hotspot_y);
    for (int y = 0; y < h && !empty; ++y) {
      sprite_row(out, f, y);
    }
  }
  for (std::size_t i = 0; i < frames.size(); ++i) {
    if (frames[i].palette.empty()) {
      continue;
    }
    out.patch_u32(table + 8 * i + 4, static_cast<std::uint32_t>(out.size()));
    out.u32(static_cast<std::uint32_t>(frames[i].palette.size()));
    for (const ShpPaletteEntry& e : frames[i].palette) {
      out.u8(e.index);
      out.u8(e.r);
      out.u8(e.g);
      out.u8(e.b);
    }
  }
  return out.take();
}

std::vector<std::uint8_t> encode_fnt(std::span<const GlyphImage> glyphs, std::uint32_t height) {
  Bytes out;
  out.bytes(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("1.\0\0"), 4));
  out.u32(static_cast<std::uint32_t>(glyphs.size()));
  out.u32(height);
  out.u32(0xff);
  std::size_t offset = out.size() + 4 * glyphs.size();
  for (const GlyphImage& g : glyphs) {
    out.u32(static_cast<std::uint32_t>(offset));
    offset += 4 + g.pixels.size();
  }
  for (const GlyphImage& g : glyphs) {
    if (g.pixels.size() != std::size_t{g.width} * height) {
      throw std::invalid_argument("glyph size disagrees with its width and the font height");
    }
    out.u32(g.width);
    out.bytes(g.pixels);
  }
  return out.take();
}

std::vector<std::uint8_t> encode_cat(std::span<const CatMember> members) {
  Bytes out;
  out.u16(static_cast<std::uint16_t>(members.size()));
  std::uint32_t offset = 2 + 26 * static_cast<std::uint32_t>(members.size());
  for (const CatMember& m : members) {
    if (m.name.size() > 12) {
      throw std::invalid_argument("CAT member name longer than 12 characters");
    }
    out.bytes(std::span(reinterpret_cast<const std::uint8_t*>(m.name.data()), m.name.size()));
    out.zeros(12 - m.name.size());
    out.u16(0x2000);
    out.u32(0);
    out.u32(static_cast<std::uint32_t>(m.data.size()));
    out.u32(offset);
    offset += static_cast<std::uint32_t>(m.data.size());
  }
  for (const CatMember& m : members) {
    out.bytes(m.data);
  }
  return out.take();
}

std::vector<std::uint8_t> encode_world(const World& world) {
  Bytes out;
  out.format_header("AWLD", world.version);
  if (world.version >= 6) {
    out.fixed_string(world.region_name, 31);
  }
  out.u32(world.scale_shift);
  out.u16(static_cast<std::uint16_t>(world.locations.size()));
  for (const Location& l : world.locations) {
    out.i32(static_cast<std::int32_t>(l.type));
    out.i32(l.x);
    out.i32(l.z);
    out.i32(l.y);
    out.i32(l.heading.tenths());
    out.u32(0);
  }
  out.i32(world.grid.width);
  out.i32(world.grid.height);
  out.bytes(encode_compressed(world.grid.pixels));
  out.u32(0);
  out.bytes(world.grid_dac);
  // The chart: rows of u32 size (with itself) and RLE90 pixels.
  Bytes rows;
  for (std::int32_t y = 0; y < world.chart.height; ++y) {
    const auto row = std::span(world.chart.pixels)
                         .subspan(static_cast<std::size_t>(y) * static_cast<std::size_t>(world.chart.width),
                                  static_cast<std::size_t>(world.chart.width));
    const std::vector<std::uint8_t> packed = rle90_encode(row);
    rows.u32(static_cast<std::uint32_t>(packed.size() + 4));
    rows.bytes(packed);
  }
  out.i32(world.chart.width - 1);
  out.i32(world.chart.height);
  out.u32(static_cast<std::uint32_t>(rows.size()));
  out.bytes(encode_compressed(rows.data()));
  out.bytes(world.chart_dac);
  if (world.version >= 5) {
    out.u32(static_cast<std::uint32_t>(world.coast_tiles_shp.size()));
    out.bytes(world.coast_tiles_shp);
  }
  if (world.version >= 7) {
    const MapBounds b = world.bounds.value_or(MapBounds{});
    out.i32(b.lat_north.raw());
    out.i32(b.lat_south.raw());
    out.i32(b.lon_west.raw());
    out.i32(b.lon_east.raw());
  }
  return out.take();
}

std::vector<std::uint8_t> encode_objects(std::span<const MapObject> objects) {
  Bytes out;
  out.format_header("AOBJ", 2);
  out.u16(static_cast<std::uint16_t>(objects.size()));
  for (const MapObject& o : objects) {
    out.u32(static_cast<std::uint32_t>(o.render_type));
    out.u32(static_cast<std::uint32_t>(o.shp.size()));
    out.u32(0);
    out.i32(o.size);
    out.u32(0);
  }
  for (const MapObject& o : objects) {
    out.bytes(encode_compressed(o.shp));
  }
  return out.take();
}

std::vector<std::uint8_t> encode_environment(const Environment& env) {
  Bytes out;
  out.format_header("AENV", env.version);
  std::array<std::uint8_t, Environment::kHeaderSize> header = env.header;
  const auto size = static_cast<std::uint32_t>(env.sky_pcx.size());
  for (std::size_t i = 0; i < 4; ++i) {
    header[0x300 + i] = static_cast<std::uint8_t>(size >> (8 * i));
  }
  out.bytes(header);
  out.bytes(env.sky_pcx);
  out.bytes(env.sun);
  if (env.version >= 9) {
    for (std::size_t i = 0; i < 4; ++i) {
      if (i < env.moon_phases.size()) {
        out.bytes(env.moon_phases[i]);
      } else {
        out.zeros(Environment::kMoonSize);
      }
    }
  }
  return out.take();
}

}  // namespace sh::test
