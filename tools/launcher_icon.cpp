#include "tools/launcher_icon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

#include "assets/pcx.h"
#include "core/error.h"
#include "core/image.h"

namespace sh {

namespace {

// The game-art square: the picture's columns 40..639 (the conning tower
// in the centre), with 70 rows of sky above it and 50 of hull below.
constexpr int kArtSize = 600;
constexpr int kArtLeft = 40;
constexpr int kArtSkyRows = 70;
// The sky pixels sampled at the picture's top, and the blue it deepens to
// at the square's top.
constexpr int kSkySampleRows = 6;
constexpr std::array<double, 3> kDeepSky{40, 110, 215};

// The monochrome layer's levels: the luminance range (0..1) mapped to
// alpha 0..1, by source.
struct Levels {
  double low;
  double high;
};
constexpr Levels kDarknessLevels{0.35, 0.75};
constexpr Levels kLightnessLevels{0.20, 0.28};

// The densities: their directory and their scale from dp to pixels.
struct Density {
  const char* name;
  double scale;
};
constexpr std::array<Density, 5> kDensities{{
    {"mdpi", 1.0},
    {"hdpi", 1.5},
    {"xhdpi", 2.0},
    {"xxhdpi", 3.0},
    {"xxxhdpi", 4.0},
}};
constexpr int kFullBleedDp = 108;
constexpr int kLegacyDp = 48;

constexpr std::array<std::uint32_t, 256> make_crc_table() {
  std::array<std::uint32_t, 256> table{};
  for (std::uint32_t n = 0; n < 256; ++n) {
    std::uint32_t c = n;
    for (int k = 0; k < 8; ++k) {
      c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
    }
    table[n] = c;
  }
  return table;
}
constexpr std::array<std::uint32_t, 256> kCrcTable = make_crc_table();

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (const int shift : {24, 16, 8, 0}) {
    out.push_back(static_cast<std::uint8_t>(v >> shift));
  }
}

void put_chunk(std::vector<std::uint8_t>& out, const char (&type)[5], std::span<const std::uint8_t> data) {
  put_u32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  put_u32(out, crc32(std::span(out).subspan(start)));
}

std::uint8_t to_byte(double v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }

// Returns the average of `values` (`count` RGB triples) over the `radius`
// pixels around each, weighted by `weights`, edges clamped.
std::vector<std::array<double, 3>> weighted_blur(const std::vector<std::array<double, 3>>& values,
                                                 const std::vector<double>& weights, int radius) {
  const auto n = static_cast<int>(values.size());
  // Prefix sums of weight * value and of weight over the clamped row.
  std::vector<std::array<double, 4>> sums(values.size() + 1);
  for (int i = 0; i < n; ++i) {
    const auto k = static_cast<std::size_t>(i);
    const double w = weights[k];
    sums[k + 1] = {sums[k][0] + values[k][0] * w, sums[k][1] + values[k][1] * w, sums[k][2] + values[k][2] * w,
                   sums[k][3] + w};
  }
  std::vector<std::array<double, 3>> out(values.size());
  for (int i = 0; i < n; ++i) {
    const auto lo = static_cast<std::size_t>(std::max(0, i - radius));
    const auto hi = static_cast<std::size_t>(std::min(n, i + radius + 1));
    const double w = sums[hi][3] - sums[lo][3];
    for (std::size_t c = 0; c < 3; ++c) {
      out[static_cast<std::size_t>(i)][c] =
          w > 0 ? (sums[hi][c] - sums[lo][c]) / w : values[static_cast<std::size_t>(i)][c];
    }
  }
  return out;
}

// The sky and its clouds are bright blue (blue above green); the masts'
// greys are greener.
bool is_sky(Rgb c) { return c.b >= 225 && c.b > c.g; }

std::array<double, 3> rgb_of(Rgb c) { return {static_cast<double>(c.r), static_cast<double>(c.g), static_cast<double>(c.b)}; }

double luminance(Rgba8 p) { return (0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b) / 255; }

// The weights of the source pixels each destination pixel of an axis
// covers: area averaging from `from` pixels to `to`.
struct Span {
  int first = 0;
  std::vector<double> weights;
};

std::vector<Span> area_spans(int from, int to) {
  std::vector<Span> spans(static_cast<std::size_t>(to));
  const double step = static_cast<double>(from) / to;
  for (int i = 0; i < to; ++i) {
    const double x0 = i * step;
    const double x1 = (i + 1) * step;
    Span& s = spans[static_cast<std::size_t>(i)];
    s.first = static_cast<int>(std::floor(x0));
    const int last = std::min(from - 1, static_cast<int>(std::ceil(x1)) - 1);
    for (int k = s.first; k <= last; ++k) {
      const double covered = std::min<double>(x1, k + 1) - std::max<double>(x0, k);
      s.weights.push_back(std::max(0.0, covered) / step);
    }
  }
  return spans;
}

void write_file(const std::filesystem::path& path, std::span<const std::uint8_t> data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  if (!out) {
    throw std::runtime_error("cannot write " + path.string());
  }
}

}  // namespace

RgbaBitmap::RgbaBitmap(int width, int height, Rgba8 fill)
    : width_(width), height_(height),
      pixels_(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), fill) {}

std::uint32_t crc32(std::span<const std::uint8_t> data) {
  std::uint32_t c = 0xffffffffu;
  for (const std::uint8_t b : data) {
    c = kCrcTable[(c ^ b) & 0xffu] ^ (c >> 8);
  }
  return c ^ 0xffffffffu;
}

std::uint32_t adler32(std::span<const std::uint8_t> data) {
  constexpr std::uint32_t kMod = 65521;
  std::uint32_t a = 1;
  std::uint32_t b = 0;
  for (const std::uint8_t v : data) {
    a = (a + v) % kMod;
    b = (b + a) % kMod;
  }
  return (b << 16) | a;
}

std::vector<std::uint8_t> encode_png(const RgbaBitmap& image) {
  // The raw scanlines: filter type 0 then the RGBA bytes.
  std::vector<std::uint8_t> raw;
  raw.reserve(static_cast<std::size_t>(image.height()) * (static_cast<std::size_t>(image.width()) * 4 + 1));
  for (int y = 0; y < image.height(); ++y) {
    raw.push_back(0);
    for (int x = 0; x < image.width(); ++x) {
      const Rgba8 p = image.at(x, y);
      raw.insert(raw.end(), {p.r, p.g, p.b, p.a});
    }
  }
  // The zlib stream: header (deflate, 32 KiB window, no dictionary), stored
  // blocks of at most 65535 bytes (BFINAL on the last), Adler-32.
  std::vector<std::uint8_t> z{0x78, 0x01};
  constexpr std::size_t kMaxStored = 65535;
  std::size_t pos = 0;
  do {
    const std::size_t len = std::min(kMaxStored, raw.size() - pos);
    const bool final = pos + len == raw.size();
    const auto n = static_cast<std::uint16_t>(len);
    const auto nn = static_cast<std::uint16_t>(~n);
    z.insert(z.end(), {static_cast<std::uint8_t>(final ? 1 : 0), static_cast<std::uint8_t>(n & 0xff),
                       static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(nn & 0xff),
                       static_cast<std::uint8_t>(nn >> 8)});
    z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(pos),
             raw.begin() + static_cast<std::ptrdiff_t>(pos + len));
    pos += len;
  } while (pos < raw.size());
  put_u32(z, adler32(raw));

  std::vector<std::uint8_t> out{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  std::vector<std::uint8_t> header;
  put_u32(header, static_cast<std::uint32_t>(image.width()));
  put_u32(header, static_cast<std::uint32_t>(image.height()));
  // 8 bits per sample, colour type 6 (RGBA), deflate, adaptive filtering
  // method, no interlace.
  header.insert(header.end(), {8, 6, 0, 0, 0});
  put_chunk(out, "IHDR", header);
  put_chunk(out, "IDAT", z);
  put_chunk(out, "IEND", {});
  return out;
}

RgbaBitmap resize_area(const RgbaBitmap& image, int width, int height) {
  if (width <= 0 || height <= 0 || image.width() <= 0 || image.height() <= 0) {
    throw std::invalid_argument("resize_area: empty size");
  }
  const std::vector<Span> xs = area_spans(image.width(), width);
  const std::vector<Span> ys = area_spans(image.height(), height);
  // Premultiplied RGBA, resampled across then down.
  using Premul = std::array<double, 4>;
  std::vector<Premul> across(static_cast<std::size_t>(width) * static_cast<std::size_t>(image.height()));
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < width; ++x) {
      const Span& s = xs[static_cast<std::size_t>(x)];
      Premul sum{};
      for (std::size_t k = 0; k < s.weights.size(); ++k) {
        const Rgba8 p = image.at(s.first + static_cast<int>(k), y);
        const double wa = s.weights[k] * p.a;
        sum[0] += p.r * wa;
        sum[1] += p.g * wa;
        sum[2] += p.b * wa;
        sum[3] += wa;
      }
      across[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] = sum;
    }
  }
  RgbaBitmap out(width, height);
  for (int y = 0; y < height; ++y) {
    const Span& s = ys[static_cast<std::size_t>(y)];
    for (int x = 0; x < width; ++x) {
      Premul sum{};
      for (std::size_t k = 0; k < s.weights.size(); ++k) {
        const Premul& p = across[static_cast<std::size_t>(s.first + static_cast<int>(k)) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)];
        for (std::size_t c = 0; c < 4; ++c) {
          sum[c] += p[c] * s.weights[k];
        }
      }
      if (sum[3] > 0) {
        out.at(x, y) = {to_byte(sum[0] / sum[3]), to_byte(sum[1] / sum[3]), to_byte(sum[2] / sum[3]),
                        to_byte(sum[3])};
      }
    }
  }
  return out;
}

RgbaBitmap crop(const RgbaBitmap& image, int x, int y, int width, int height) {
  if (x < 0 || y < 0 || width < 0 || height < 0 || x + width > image.width() || y + height > image.height()) {
    throw std::invalid_argument("crop: the area is outside the image");
  }
  RgbaBitmap out(width, height);
  for (int yy = 0; yy < height; ++yy) {
    for (int xx = 0; xx < width; ++xx) {
      out.at(xx, yy) = image.at(x + xx, y + yy);
    }
  }
  return out;
}

RgbaBitmap round_mask(const RgbaBitmap& image) {
  // Coverage by 4 x 4 samples per pixel.
  constexpr int kSamples = 4;
  const double cx = image.width() / 2.0;
  const double cy = image.height() / 2.0;
  const double r = std::min(cx, cy);
  RgbaBitmap out = image;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      int inside = 0;
      for (int sy = 0; sy < kSamples; ++sy) {
        for (int sx = 0; sx < kSamples; ++sx) {
          const double dx = x + (sx + 0.5) / kSamples - cx;
          const double dy = y + (sy + 0.5) / kSamples - cy;
          inside += dx * dx + dy * dy <= r * r ? 1 : 0;
        }
      }
      Rgba8& p = out.at(x, y);
      p.a = static_cast<std::uint8_t>(p.a * inside / (kSamples * kSamples));
    }
  }
  return out;
}

RgbaBitmap decode_pam(std::span<const std::uint8_t> data, std::string_view context) {
  const std::string text(reinterpret_cast<const char*>(data.data()), std::min<std::size_t>(data.size(), 256));
  const std::size_t end = text.find("ENDHDR\n");
  if (!text.starts_with("P7\n") || end == std::string::npos) {
    throw DataError(std::string(context) + ": not a PAM file");
  }
  int width = 0;
  int height = 0;
  int depth = 0;
  int maxval = 0;
  std::string tuple;
  std::size_t line_start = 3;
  while (line_start < end) {
    const std::size_t line_end = text.find('\n', line_start);
    const std::string line = text.substr(line_start, line_end - line_start);
    const std::size_t space = line.find(' ');
    const std::string key = line.substr(0, space);
    const std::string value = space == std::string::npos ? "" : line.substr(space + 1);
    if (key == "WIDTH") {
      width = std::stoi(value);
    } else if (key == "HEIGHT") {
      height = std::stoi(value);
    } else if (key == "DEPTH") {
      depth = std::stoi(value);
    } else if (key == "MAXVAL") {
      maxval = std::stoi(value);
    } else if (key == "TUPLTYPE") {
      tuple = value;
    }
    line_start = line_end + 1;
  }
  const bool rgba = tuple == "RGB_ALPHA" && depth == 4;
  const bool rgb = tuple == "RGB" && depth == 3;
  if (width <= 0 || height <= 0 || maxval != 255 || !(rgba || rgb)) {
    throw DataError(std::string(context) + ": unsupported PAM (8-bit RGB_ALPHA or RGB expected)");
  }
  const std::size_t offset = end + 7;
  const auto stride = static_cast<std::size_t>(depth);
  if (data.size() - offset < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * stride) {
    throw DataError(std::string(context) + ": truncated PAM");
  }
  RgbaBitmap out(width, height);
  std::size_t i = offset;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      out.at(x, y) = {data[i], data[i + 1], data[i + 2], rgba ? data[i + 3] : std::uint8_t{255}};
      i += stride;
    }
  }
  return out;
}

RgbaBitmap game_art_foreground(const Install& install) {
  const PcxImage pcx = load_pcx(install, "ART/RTNPORT.PCX");
  const Palette pal = to_palette(pcx.palette);
  const Image8& pic = pcx.image;
  if (pic.width() < kArtLeft + kArtSize || pic.height() > kArtSize) {
    throw DataError("ART/RTNPORT.PCX: unexpected size");
  }
  const int hull_rows = kArtSize - kArtSkyRows - pic.height();
  const auto colour = [&](int x, int y) { return pal.colors[pic.at(kArtLeft + x, y)]; };
  RgbaBitmap out(kArtSize, kArtSize);
  const auto put = [&](int x, int y, const std::array<double, 3>& c) {
    out.at(x, y) = {to_byte(c[0]), to_byte(c[1]), to_byte(c[2]), 255};
  };
  for (int y = 0; y < pic.height(); ++y) {
    for (int x = 0; x < kArtSize; ++x) {
      put(x, kArtSkyRows + y, rgb_of(colour(x, y)));
    }
  }
  // Above: the columns whose top pixel is not sky (the masts, the radio
  // mast) continue; elsewhere the sky's colour, averaged over the sky
  // pixels of the top rows, spreads wider and deepens with the height.
  std::vector<std::array<double, 3>> sky(kArtSize);
  std::vector<double> sky_weight(kArtSize);
  for (int x = 0; x < kArtSize; ++x) {
    std::array<double, 3> sum{};
    int n = 0;
    for (int y = 0; y < kSkySampleRows; ++y) {
      if (is_sky(colour(x, y))) {
        const std::array<double, 3> c = rgb_of(colour(x, y));
        for (std::size_t k = 0; k < 3; ++k) {
          sum[k] += c[k];
        }
        ++n;
      }
    }
    const auto i = static_cast<std::size_t>(x);
    sky_weight[i] = n > 0 ? 1 : 0;
    for (std::size_t k = 0; k < 3; ++k) {
      sky[i][k] = n > 0 ? sum[k] / n : 0;
    }
  }
  for (int d = 1; d <= kArtSkyRows; ++d) {
    const double t = static_cast<double>(d) / kArtSkyRows;
    const auto blurred = weighted_blur(sky, sky_weight, static_cast<int>(std::lround(6 + 1.2 * d)));
    for (int x = 0; x < kArtSize; ++x) {
      if (!is_sky(colour(x, 0))) {
        put(x, kArtSkyRows - d, rgb_of(colour(x, 0)));
        continue;
      }
      std::array<double, 3> c = blurred[static_cast<std::size_t>(x)];
      for (std::size_t k = 0; k < 3; ++k) {
        c[k] = c[k] * (1 - 0.5 * t) + kDeepSky[k] * 0.5 * t;
      }
      put(x, kArtSkyRows - d, c);
    }
  }
  // Below: the hull's last row, its streaks spreading and darkening.
  std::vector<std::array<double, 3>> hull(kArtSize);
  for (int x = 0; x < kArtSize; ++x) {
    hull[static_cast<std::size_t>(x)] = rgb_of(colour(x, pic.height() - 1));
  }
  const std::vector<double> ones(kArtSize, 1.0);
  for (int d = 1; d <= hull_rows; ++d) {
    const double t = static_cast<double>(d) / hull_rows;
    const auto blurred = weighted_blur(hull, ones, static_cast<int>(std::lround(1 + 0.5 * d)));
    for (int x = 0; x < kArtSize; ++x) {
      std::array<double, 3> c = blurred[static_cast<std::size_t>(x)];
      for (double& v : c) {
        v *= 1 - 0.25 * t;
      }
      put(x, kArtSkyRows + pic.height() - 1 + d, c);
    }
  }
  return out;
}

RgbaBitmap monochrome(const RgbaBitmap& image, MonochromeSource source) {
  const Levels levels = source == MonochromeSource::kDarkness ? kDarknessLevels : kLightnessLevels;
  RgbaBitmap out(image.width(), image.height());
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const Rgba8 p = image.at(x, y);
      const double l = luminance(p);
      const double v = source == MonochromeSource::kDarkness ? 1 - l : l;
      const double a = std::clamp((v - levels.low) / (levels.high - levels.low), 0.0, 1.0) * p.a;
      out.at(x, y) = {255, 255, 255, to_byte(a)};
    }
  }
  return out;
}

void write_launcher_icons(const RgbaBitmap& foreground, MonochromeSource source,
                          const std::filesystem::path& res_dir) {
  const int size = foreground.width();
  if (foreground.height() != size || size < kFullBleedDp * 4) {
    throw std::invalid_argument("write_launcher_icons: the foreground must be square, 432 px or more");
  }
  const RgbaBitmap mono = monochrome(foreground, source);
  // The launchers' visible part: the central 72 of the 108 dp.
  const int visible = size * 2 / 3;
  const RgbaBitmap centre = crop(foreground, (size - visible) / 2, (size - visible) / 2, visible, visible);
  for (const Density& density : kDensities) {
    const std::filesystem::path dir = res_dir / (std::string("mipmap-") + density.name);
    std::filesystem::create_directories(dir);
    const auto full = static_cast<int>(kFullBleedDp * density.scale);
    const auto legacy = static_cast<int>(kLegacyDp * density.scale);
    write_file(dir / "ic_launcher_foreground.png", encode_png(resize_area(foreground, full, full)));
    write_file(dir / "ic_launcher_monochrome.png", encode_png(resize_area(mono, full, full)));
    const RgbaBitmap small = resize_area(centre, legacy, legacy);
    write_file(dir / "ic_launcher.png", encode_png(small));
    write_file(dir / "ic_launcher_round.png", encode_png(round_mask(small)));
  }
}

}  // namespace sh
