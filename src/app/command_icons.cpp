#include "app/command_icons.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "game/stations/nav_panel.h"
#include "gfx/viewport.h"
#include "ui/skin.h"

namespace sh {

namespace {

// GLOBAL.SHP: the status button with the submarine (command bar).
constexpr std::size_t kSubPlateFrame = 8;
// GLOBAL.SHP: the command bar's 1MC microphone, periscope eyepieces,
// torpedo and helm buttons.
constexpr std::size_t kMicrophoneFrame = 10;
constexpr std::size_t kEyepiecesFrame = 12;
constexpr std::size_t kTorpedoFrame = 16;
constexpr std::size_t kHelmFrame = 22;
// CURSORS.SHP: the FIRE cursor of the TDC's tube buttons.
constexpr std::size_t kFireCursorFrame = 16;
// GAUGES.PCX: the engine telegraph's dial (centre and radius).
constexpr int kTelegraphX = 320;
constexpr int kTelegraphY = 254;
constexpr int kTelegraphRadius = 70;
// TBTTDC.SHP: tube N's ready button is frame 2N, its fired one 2N + 1;
// frame 20 is the torpedo selector knob.
constexpr std::size_t kKnobFrame = 20;
// MENU_B.FNT: the red arrows.
constexpr std::uint8_t kArrowUp = 30;
constexpr std::uint8_t kArrowDown = 31;
// The gauges station's depth dial template (`gauges_panel_open`).
constexpr std::uint32_t kDepthDialTemplate = 0x108670;
// The depths the gauge icons show (feet; the manual's preset depths).
constexpr std::array<std::int32_t, kDepthCommandCount> kGaugeDepths{65, 48, 15, 150, 0};

// The icon canvases.
constexpr int kSceneSize = 80;
constexpr int kPlateSize = 64;
constexpr int kTubeSize = 36;
constexpr int kButtonSize = 96;

std::uint32_t argb(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
  return (std::uint32_t{a} << 24) | (std::uint32_t{r} << 16) | (std::uint32_t{g} << 8) | b;
}
std::uint8_t channel(std::uint32_t c, int shift) { return static_cast<std::uint8_t>((c >> shift) & 0xff); }

// Returns colour `index` of a 6-bit palette, opaque.
std::uint32_t vga_colour(const VgaPalette& pal, std::uint8_t index) {
  const auto c8 = [&](std::size_t k) { return static_cast<std::uint8_t>(pal[index * 3u + k] * 255 / 63); };
  return argb(c8(0), c8(1), c8(2));
}

// Returns the colour of `pal` nearest to (r, g, b): the drawn shapes use
// the game's own colours.
std::uint32_t palette_colour(const VgaPalette& pal, int r, int g, int b) {
  std::uint32_t best = 0;
  int best_d = 1 << 30;
  for (int i = 0; i < 256; ++i) {
    const std::uint32_t c = vga_colour(pal, static_cast<std::uint8_t>(i));
    const int dr = channel(c, 16) - r;
    const int dg = channel(c, 8) - g;
    const int db = channel(c, 0) - b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < best_d) {
      best_d = d;
      best = c;
    }
  }
  return best;
}

std::uint32_t blend(std::uint32_t a, std::uint32_t b, float t) {
  const auto mix = [&](int shift) {
    return static_cast<std::uint8_t>(std::lround(channel(a, shift) * (1 - t) + channel(b, shift) * t));
  };
  return argb(mix(16), mix(8), mix(0), channel(a, 24));
}

RgbaImage frame_rgba(const SpriteFrame& frame, const VgaPalette& pal) {
  const DecodedFrame d = frame.decode();
  RgbaImage out(d.image.width(), d.image.height());
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      if (d.opaque.at(x, y) != 0) {
        out.at(x, y) = vga_colour(pal, d.image.at(x, y));
      }
    }
  }
  return out;
}

RgbaImage image_rgba(const Image8& image, const VgaPalette& pal, int x0, int y0, int w, int h) {
  RgbaImage out(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (image.contains(x0 + x, y0 + y)) {
        out.at(x, y) = vga_colour(pal, image.at(x0 + x, y0 + y));
      }
    }
  }
  return out;
}

// Draws `src` over `dst` with its top-left corner at (x, y), clipped
// (straight alpha "over").
void blit(RgbaImage& dst, const RgbaImage& src, int x, int y) {
  for (int sy = 0; sy < src.height; ++sy) {
    for (int sx = 0; sx < src.width; ++sx) {
      const int dx = x + sx;
      const int dy = y + sy;
      if (dx < 0 || dy < 0 || dx >= dst.width || dy >= dst.height) {
        continue;
      }
      const std::uint32_t s = src.at(sx, sy);
      const int a = channel(s, 24);
      if (a == 255) {
        dst.at(dx, dy) = s;
      } else if (a != 0) {
        const std::uint32_t d = dst.at(dx, dy);
        const int da = channel(d, 24) * (255 - a) / 255;
        const int oa = a + da;
        const auto mix = [&](int shift) {
          return static_cast<std::uint8_t>((channel(s, shift) * a + channel(d, shift) * da) / oa);
        };
        dst.at(dx, dy) = argb(mix(16), mix(8), mix(0), static_cast<std::uint8_t>(oa));
      }
    }
  }
}

RgbaImage resize_nearest(const RgbaImage& src, int w, int h) {
  RgbaImage out(w, h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      out.at(x, y) = src.at(x * src.width / w, y * src.height / h);
    }
  }
  return out;
}

// Returns `src` rotated by `degrees` (clockwise) about its centre, on a
// canvas holding all of it (nearest neighbour).
RgbaImage rotate(const RgbaImage& src, double degrees) {
  const double a = degrees * std::numbers::pi / 180;
  const double c = std::cos(a);
  const double s = std::sin(a);
  const int w = static_cast<int>(std::ceil(std::abs(src.width * c) + std::abs(src.height * s)));
  const int h = static_cast<int>(std::ceil(std::abs(src.width * s) + std::abs(src.height * c)));
  RgbaImage out(w, h);
  const double scx = src.width / 2.0;
  const double scy = src.height / 2.0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double dx = x + 0.5 - w / 2.0;
      const double dy = y + 0.5 - h / 2.0;
      const auto sx = static_cast<int>(std::floor(dx * c + dy * s + scx));
      const auto sy = static_cast<int>(std::floor(-dx * s + dy * c + scy));
      if (sx >= 0 && sy >= 0 && sx < src.width && sy < src.height) {
        out.at(x, y) = src.at(sx, sy);
      }
    }
  }
  return out;
}

void fill_rect(RgbaImage& img, int x, int y, int w, int h, std::uint32_t c) {
  for (int yy = std::max(0, y); yy < std::min(img.height, y + h); ++yy) {
    for (int xx = std::max(0, x); xx < std::min(img.width, x + w); ++xx) {
      img.at(xx, yy) = c;
    }
  }
}

// Fills the pixels whose centre lies within `r0` <= distance < `r1` of
// (cx, cy) with `colour(dx, dy)`.
template <class F>
void fill_ring(RgbaImage& img, double cx, double cy, double r0, double r1, F&& colour) {
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      const double dx = x + 0.5 - cx;
      const double dy = y + 0.5 - cy;
      const double d = std::sqrt(dx * dx + dy * dy);
      if (d >= r0 && d < r1) {
        img.at(x, y) = colour(dx, dy);
      }
    }
  }
}

// Returns the glyphs of `text` in `font` (0xff transparent), coloured by
// `pal`.
RgbaImage text_rgba(const Font& font, std::string_view text, const VgaPalette& pal) {
  RgbaImage out(std::max(1, font.text_width(text)), font.height());
  int pen = 0;
  for (const char ch : text) {
    const Glyph* g = font.glyph(static_cast<std::uint8_t>(ch));
    if (g == nullptr || g->width == 0) {
      continue;
    }
    const auto gw = static_cast<int>(g->width);
    for (int y = 0; y < font.height(); ++y) {
      for (int x = 0; x < gw; ++x) {
        const std::uint8_t v = g->pixels[static_cast<std::size_t>(y * gw + x)];
        if (v != 0xff) {
          out.at(pen + x, y) = vga_colour(pal, v);
        }
      }
    }
    pen += gw;
  }
  return out;
}

// Returns `img` cropped to its non-transparent pixels.
RgbaImage trim(const RgbaImage& img) {
  int x0 = img.width;
  int y0 = img.height;
  int x1 = -1;
  int y1 = -1;
  for (int y = 0; y < img.height; ++y) {
    for (int x = 0; x < img.width; ++x) {
      if (channel(img.at(x, y), 24) != 0) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
      }
    }
  }
  if (x1 < 0) {
    return img;
  }
  RgbaImage out(x1 - x0 + 1, y1 - y0 + 1);
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      out.at(x, y) = img.at(x0 + x, y0 + y);
    }
  }
  return out;
}

// Returns `img` darkened (the pressed look of an icon).
RgbaImage darken(const RgbaImage& img) {
  RgbaImage out = img;
  for (std::uint32_t& p : out.pixels) {
    p = blend(p, argb(0, 0, 0, channel(p, 24)), 0.45f);
  }
  return out;
}

// The command bar's submarine cut out of its button plate, and the rows
// that the depth orders put at the waterline.
struct SubSilhouette {
  RgbaImage image;
  int tower_x = 0;    // the conning tower's centre column
  int tower_top = 0;  // its first row below the periscope mast
  int hull_top = 0;   // the first row of the hull
};

// Returns the colour of a command bar button's plate: the most common one
// inside its bevel.
std::uint8_t plate_colour_of(const Image8& image) {
  std::map<std::uint8_t, int> counts;
  for (int y = 2; y < image.height() - 2; ++y) {
    for (int x = 2; x < image.width() - 2; ++x) {
      ++counts[image.at(x, y)];
    }
  }
  return std::ranges::max_element(counts, {}, [](const auto& kv) { return kv.second; })->first;
}

// Returns the picture of a command bar button (GLOBAL.SHP) without its
// plate and bevel: the pixels that differ from the plate, trimmed.
RgbaImage cut_symbol(const SpriteFrame& button, const VgaPalette& pal) {
  const DecodedFrame d = button.decode();
  const std::uint8_t plate_colour = plate_colour_of(d.image);
  RgbaImage out(d.image.width(), d.image.height());
  for (int y = 2; y < d.image.height() - 2; ++y) {
    for (int x = 2; x < d.image.width() - 3; ++x) {
      if (d.image.at(x, y) != plate_colour) {
        out.at(x, y) = vga_colour(pal, d.image.at(x, y));
      }
    }
  }
  return trim(out);
}

SubSilhouette cut_submarine(const SpriteFrame& plate, const VgaPalette& pal) {
  const DecodedFrame d = plate.decode();
  const int w = d.image.width();
  const int h = d.image.height();
  const std::uint8_t plate_colour = plate_colour_of(d.image);
  // Each row of the submarine spans from its first to its last pixel that
  // differs from the plate.
  RgbaImage sub(w, h);
  std::vector<int> widths(static_cast<std::size_t>(h), 0);
  std::vector<int> centres(static_cast<std::size_t>(h), 0);
  for (int y = 2; y < h - 2; ++y) {
    int x0 = -1;
    int x1 = -1;
    // Inside the bevel (1 pixel on the left, 3 on the right).
    for (int x = 2; x < w - 3; ++x) {
      if (d.image.at(x, y) != plate_colour) {
        x0 = x0 < 0 ? x : x0;
        x1 = x;
      }
    }
    if (x0 < 0) {
      continue;
    }
    for (int x = x0; x <= x1; ++x) {
      sub.at(x, y) = vga_colour(pal, d.image.at(x, y));
    }
    widths[static_cast<std::size_t>(y)] = x1 - x0 + 1;
    centres[static_cast<std::size_t>(y)] = (x0 + x1) / 2;
  }
  // Rows from the top: the mast (narrow), the tower, the hull (wide).
  int top = 0;
  while (top < h && widths[static_cast<std::size_t>(top)] == 0) {
    ++top;
  }
  int tower = top;
  while (tower < h && widths[static_cast<std::size_t>(tower)] < 7) {
    ++tower;
  }
  int hull = tower;
  while (hull < h && widths[static_cast<std::size_t>(hull)] < w / 2) {
    ++hull;
  }
  int left = w;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (channel(sub.at(x, y), 24) != 0) {
        left = std::min(left, x);
      }
    }
  }
  SubSilhouette out;
  out.image = trim(sub);
  out.tower_x = centres[static_cast<std::size_t>(tower)] - left;
  out.tower_top = tower - top;
  out.hull_top = hull - top;
  return out;
}

}  // namespace

struct CommandIconArt::Data {
  VgaPalette menu_palette{};  // MENUBGND.PCX: the command bar and menu fonts
  VgaPalette tdc_palette{};   // TDC.PCX
  Shp tdc_sprites;            // TBTTDC.SHP
  Shp global;                 // GLOBAL.SHP: the command bar's buttons
  Shp cursors;                // CURSORS.SHP
  SubSilhouette sub;
  PcxImage gauges;            // GAUGES.PCX
  Shp gauge_sprites;          // GAUGES.SHP
  Font digits;                // DIGITS.FNT, the gauges' counters
  Font brass;                 // MENU_H.FNT
  Font arrows;                // MENU_B.FNT
  NeedleDial depth_dial;

  // The scene colours, in the command bar's palette.
  std::uint32_t sky = 0;
  std::uint32_t sea = 0;
  std::uint32_t foam = 0;
  std::uint32_t plate = 0;
  std::uint32_t plate_light = 0;
  std::uint32_t plate_dark = 0;
  std::uint32_t brass_colour = 0;
  std::uint32_t brass_light = 0;
  std::uint32_t brass_dark = 0;

  RgbaImage arrow(bool up) const {
    return text_rgba(arrows, std::string(1, static_cast<char>(up ? kArrowUp : kArrowDown)), menu_palette);
  }

  // The submarine (zoomed by `zoom`, tilted by `tilt` degrees, nose down
  // positive) with the waterline `water_row` rows below its top (negative:
  // all of it under water, that far), centred on its tower (`close`) or
  // on itself.
  RgbaImage scene(int water_row, int zoom, double tilt, bool close) const {
    RgbaImage out(kSceneSize, kSceneSize, sea);
    RgbaImage s = resize_nearest(sub.image, sub.image.width * zoom, sub.image.height * zoom);
    if (tilt != 0) {
      s = rotate(s, tilt);
    }
    const int water_y = kSceneSize * 2 / 5;
    fill_rect(out, 0, 0, kSceneSize, water_y, sky);
    const int sx = close && tilt == 0 ? kSceneSize / 2 - sub.tower_x * zoom : (kSceneSize - s.width) / 2;
    const int sy = water_y - water_row * zoom;
    // What is under water takes the sea's tint.
    for (int y = 0; y < s.height; ++y) {
      for (int x = 0; x < s.width; ++x) {
        std::uint32_t& p = s.at(x, y);
        if (channel(p, 24) != 0 && sy + y >= water_y) {
          p = blend(p, sea, 0.6f);
        }
      }
    }
    blit(out, s, sx, sy);
    fill_rect(out, 0, water_y, kSceneSize, 1, foam);
    return out;
  }

  RgbaImage silhouette(PanelCommand command, bool close) const {
    const int zoom = close ? 2 : 1;
    switch (command) {
      case PanelCommand::kPeriscopeDepth:
        // Only the top of the periscope shears above the water.
        return scene(sub.tower_top / 2, zoom, 0, close);
      case PanelCommand::kRadarDepth:
        // The shears and masts.
        return scene(sub.tower_top - 1, zoom, 0, close);
      case PanelCommand::kSurface:
        // Riding on the hull.
        return scene((sub.hull_top + sub.image.height) / 2, zoom, 0, close);
      case PanelCommand::kCrashDive: {
        RgbaImage out = scene(close ? -1 : -3, zoom, 18, close);
        blit(out, arrow(false), 4, kSceneSize - 26);
        return out;
      }
      case PanelCommand::kBlowBallast: {
        RgbaImage out = scene(sub.hull_top + 4, zoom, -18, close);
        // The air from the tanks.
        const int water_y = kSceneSize * 2 / 5;
        for (const auto& [bx, by, r] : std::array<std::array<int, 3>, 6>{
                 {{20, 66, 3}, {30, 58, 2}, {44, 70, 3}, {54, 62, 2}, {36, 74, 2}, {62, 72, 3}}}) {
          fill_ring(out, bx, by, r - 1.0, r + 0.2, [&](double, double) { return foam; });
        }
        blit(out, arrow(true), 4, water_y + 4);
        return out;
      }
      default:
        return {};
    }
  }

  RgbaImage gauge(PanelCommand command) const {
    const auto i = static_cast<std::size_t>(command);
    Image8 dial = gauges.image;
    NeedleDial d = depth_dial;
    d.cap_frame = -1;  // as `gauges_panel_open` sets it
    draw_needle_dial(Viewport::full(dial), gauges.image, d, gauge_sprites, digits, ui::GuiResources::identity_table(),
                     kGaugeDepths[i], std::nullopt, kGaugeDepths[i]);
    // The dial's shallow quarter (0 at the bottom, 150 on the left).
    constexpr int kW = 100;
    RgbaImage out = image_rgba(dial, gauges.palette, d.cx - 80, d.cy - 30, kW, kW);
    if (command == PanelCommand::kCrashDive) {
      blit(out, arrow(false), kW - 18, 4);
    } else if (command == PanelCommand::kBlowBallast) {
      blit(out, arrow(true), kW - 18, 4);
    }
    return out;
  }

  // Returns `text` in the brass font on a `w` x `h` riveted plate.
  RgbaImage labelled_plate(std::string_view text, int w, int h) const {
    RgbaImage out = riveted_plate(w, h);
    const RgbaImage label = trim(text_rgba(brass, text, menu_palette));
    blit(out, label, (w - label.width) / 2, (h - label.height) / 2);
    return out;
  }

  // Returns a "+" (`plus`) or "-" in the brass font on a riveted plate:
  // the font has no such signs, so they are made of its "I" (a brass bar),
  // laid flat for the horizontal stroke.
  RgbaImage sign_plate(bool plus) const {
    RgbaImage out = riveted_plate(kPlateSize, kPlateSize);
    const RgbaImage bar = trim(text_rgba(brass, "I", menu_palette));
    RgbaImage flat(bar.height, bar.width);
    for (int y = 0; y < flat.height; ++y) {
      for (int x = 0; x < flat.width; ++x) {
        flat.at(x, y) = bar.at(y, x);
      }
    }
    blit(out, flat, (kPlateSize - flat.width) / 2, (kPlateSize - flat.height) / 2);
    if (plus) {
      blit(out, bar, (kPlateSize - bar.width) / 2, (kPlateSize - bar.height) / 2);
    }
    return out;
  }

  // Returns a `w` x `h` dark plate with a bevel and brass rivets.
  RgbaImage riveted_plate(int w, int h) const {
    RgbaImage out(w, h, plate);
    fill_rect(out, 0, 0, w, 2, plate_light);
    fill_rect(out, 0, 0, 2, h, plate_light);
    fill_rect(out, 0, h - 2, w, 2, plate_dark);
    fill_rect(out, w - 2, 0, 2, h, plate_dark);
    for (const int rx : {6, w - 7}) {
      for (const int ry : {6, h - 7}) {
        fill_ring(out, rx + 0.5, ry + 0.5, 0, 2.6, [&](double dx, double dy) {
          return dx + dy < 0 ? brass_light : brass_dark;
        });
      }
    }
    return out;
  }

  RgbaImage plate_icon(PanelCommand command) const {
    static constexpr std::string_view kLetters = "PRSCB";
    return labelled_plate(kLetters.substr(static_cast<std::size_t>(command), 1), kPlateSize, kPlateSize);
  }

  // Returns a round button: a brass ring lit from the top left around
  // `face` (or the dark plate when `face` is null), with `symbol` centred
  // at the largest whole zoom that fits two thirds of it.
  RgbaImage ring_button(const RgbaImage& symbol, const RgbaImage* face = nullptr) const {
    RgbaImage out(kButtonSize, kButtonSize);
    constexpr double c = kButtonSize / 2.0;
    if (face != nullptr) {
      fill_ring(out, c, c, 0, c - 6, [&](double dx, double dy) {
        const int fx = std::clamp(static_cast<int>((dx + c) * face->width / kButtonSize), 0, face->width - 1);
        const int fy = std::clamp(static_cast<int>((dy + c) * face->height / kButtonSize), 0, face->height - 1);
        return face->at(fx, fy);
      });
    } else {
      fill_ring(out, c, c, 0, c - 6, [&](double, double) { return plate; });
    }
    fill_ring(out, c, c, c - 7, c, [&](double dx, double dy) {
      const double light = -(dx + dy) / (std::sqrt(dx * dx + dy * dy) * std::numbers::sqrt2);
      return light > 0.35 ? brass_light : light < -0.35 ? brass_dark : brass_colour;
    });
    if (symbol.width > 0) {
      const int room = kButtonSize * 2 / 3;
      const int zoom = std::max(1, std::min(room / symbol.width, room / symbol.height));
      const RgbaImage big = resize_nearest(symbol, symbol.width * zoom, symbol.height * zoom);
      blit(out, big, (kButtonSize - big.width) / 2, (kButtonSize - big.height) / 2);
    }
    return out;
  }
};

CommandIconArt::CommandIconArt(const Install& install, const ShPaths& paths) : data_(std::make_unique<Data>()) {
  const auto art = [&](std::string_view file) { return paths.path(GameDir::kArt, file); };
  Data& d = *data_;
  d.menu_palette = load_pcx(install, art("MENUBGND.PCX")).palette;
  d.tdc_palette = load_pcx(install, art("TDC.PCX")).palette;
  d.tdc_sprites = Shp::load(install, art("TBTTDC.SHP"));
  d.global = Shp::load(install, art("GLOBAL.SHP"));
  d.cursors = Shp::load(install, art("CURSORS.SHP"));
  d.sub = cut_submarine(d.global.frame(kSubPlateFrame), d.menu_palette);
  d.gauges = load_pcx(install, art("GAUGES.PCX"));
  d.gauge_sprites = Shp::load(install, art("GAUGES.SHP"));
  d.digits = Font::load(install, art("DIGITS.FNT"));
  d.brass = Font::load(install, art("MENU_H.FNT"));
  d.arrows = Font::load(install, art("MENU_B.FNT"));
  d.depth_dial = NeedleDial::read(ExeImage::load(install), kDepthDialTemplate);
  const VgaPalette& p = d.menu_palette;
  // The sky of the bridge picture, the sea of the periscope's.
  const VgaPalette bridge = load_pcx(install, art("BRIDGE.PCX")).palette;
  d.sky = palette_colour(bridge, 160, 185, 210);
  d.foam = palette_colour(bridge, 230, 238, 245);
  d.sea = palette_colour(load_pcx(install, art("SCOPE.PCX")).palette, 24, 52, 96);
  d.plate = palette_colour(p, 38, 42, 40);
  d.plate_light = palette_colour(p, 92, 98, 94);
  d.plate_dark = palette_colour(p, 14, 16, 15);
  d.brass_colour = palette_colour(p, 170, 140, 72);
  d.brass_light = palette_colour(p, 225, 200, 120);
  d.brass_dark = palette_colour(p, 100, 78, 36);
}

CommandIconArt::~CommandIconArt() = default;
CommandIconArt::CommandIconArt(CommandIconArt&&) noexcept = default;
CommandIconArt& CommandIconArt::operator=(CommandIconArt&&) noexcept = default;

RgbaImage CommandIconArt::depth_icon(PanelCommand command, DepthIconStyle style) const {
  switch (style) {
    case DepthIconStyle::kSilhouette:
      return data_->silhouette(command, false);
    case DepthIconStyle::kSilhouetteClose:
      return data_->silhouette(command, true);
    case DepthIconStyle::kGauge:
      return data_->gauge(command);
    case DepthIconStyle::kPlate:
      return data_->plate_icon(command);
  }
  return {};
}

RgbaImage CommandIconArt::tube_icon(std::size_t tube, bool pressed) const {
  const RgbaImage button = frame_rgba(data_->tdc_sprites.frame(tube * 2 + (pressed ? 1 : 0)), data_->tdc_palette);
  RgbaImage out(kTubeSize, kTubeSize);
  blit(out, button, (kTubeSize - button.width) / 2, (kTubeSize - button.height) / 2);
  return out;
}

RgbaImage CommandIconArt::compression_icon(bool up) const { return data_->sign_plate(up); }

RgbaImage CommandIconArt::button_icon(ButtonIconStyle style) const {
  const Data& d = *data_;
  const auto symbol = [&](std::size_t frame) { return cut_symbol(d.global.frame(frame), d.menu_palette); };
  switch (style) {
    case ButtonIconStyle::kKnob:
      return frame_rgba(d.tdc_sprites.frame(kKnobFrame), d.tdc_palette);
    case ButtonIconStyle::kBrassRing:
      return d.ring_button(d.sub.image);
    case ButtonIconStyle::kMicrophone:
      return d.ring_button(symbol(kMicrophoneFrame));
    case ButtonIconStyle::kHelm:
      return d.ring_button(symbol(kHelmFrame));
    case ButtonIconStyle::kEyepieces:
      return d.ring_button(symbol(kEyepiecesFrame));
    case ButtonIconStyle::kTorpedo:
      return d.ring_button(symbol(kTorpedoFrame));
    case ButtonIconStyle::kFireCursor:
      return d.ring_button(frame_rgba(d.cursors.frame(kFireCursorFrame), d.menu_palette));
    case ButtonIconStyle::kTelegraph: {
      const RgbaImage face =
          image_rgba(d.gauges.image, d.gauges.palette, kTelegraphX - kTelegraphRadius, kTelegraphY - kTelegraphRadius,
                     2 * kTelegraphRadius, 2 * kTelegraphRadius);
      return d.ring_button({}, &face);
    }
    case ButtonIconStyle::kOrdersPlate:
      return d.labelled_plate("ORDERS", 2 * kPlateSize, kPlateSize);
  }
  return {};
}

CommandIcons build_command_icons(const CommandIconArt& art, const CommandIconDesign& design) {
  CommandIcons out;
  out.button = art.button_icon(design.button);
  for (std::size_t i = 0; i < 2; ++i) {
    out.compression[i] = art.compression_icon(i == 0);
    out.compression_pressed[i] = darken(out.compression[i]);
  }
  for (std::size_t i = 0; i < kPanelCommandCount; ++i) {
    const auto command = static_cast<PanelCommand>(i);
    if (i < kDepthCommandCount) {
      out.icons[i] = art.depth_icon(command, design.depth[i]);
      out.pressed[i] = darken(out.icons[i]);
    } else {
      out.icons[i] = art.tube_icon(i - kDepthCommandCount, false);
      out.pressed[i] = art.tube_icon(i - kDepthCommandCount, true);
    }
  }
  return out;
}

RgbaImage command_icon_sheet(const CommandIconArt& art, int zoom) {
  constexpr int kCell = 96;
  constexpr int kGap = 8;
  const int cell = kCell * zoom;
  const int step = cell + kGap * zoom;
  const int cols = std::max<int>(kDepthIconStyleCount, 10);
  RgbaImage sheet(kGap * zoom + cols * step, kGap * zoom + (kDepthCommandCount + 3) * step, argb(20, 22, 24));
  const auto put = [&](const RgbaImage& icon, int col, int row) {
    const int s = std::max(1, std::min(cell / icon.width, cell / icon.height));
    const RgbaImage big = resize_nearest(icon, icon.width * s, icon.height * s);
    const int x = kGap * zoom + col * step;
    const int y = kGap * zoom + row * step;
    fill_rect(sheet, x, y, cell, cell, argb(52, 58, 54));
    blit(sheet, big, x + (cell - big.width) / 2, y + (cell - big.height) / 2);
  };
  for (std::size_t c = 0; c < kDepthCommandCount; ++c) {
    for (std::size_t s = 0; s < kDepthIconStyleCount; ++s) {
      put(art.depth_icon(static_cast<PanelCommand>(c), static_cast<DepthIconStyle>(s)), static_cast<int>(s),
          static_cast<int>(c));
    }
  }
  const int row = static_cast<int>(kDepthCommandCount);
  for (std::size_t t = 0; t < 10; ++t) {
    put(art.tube_icon(t, false), static_cast<int>(t), row);
    put(art.tube_icon(t, true), static_cast<int>(t), row + 1);
  }
  for (std::size_t b = 0; b < kButtonIconStyleCount && b < static_cast<std::size_t>(cols); ++b) {
    put(art.button_icon(static_cast<ButtonIconStyle>(b)), static_cast<int>(b), row + 2);
  }
  return sheet;
}

RgbaImage command_button_sheet(const CommandIconArt& art) {
  constexpr int kCell = 200;
  constexpr int kGap = 8;
  constexpr int kStep = kCell + kGap;
  const auto cols = static_cast<int>(kButtonIconStyleCount);
  RgbaImage sheet(kGap + cols * kStep, kGap + 2 * kStep, argb(20, 22, 24));
  // The backgrounds of the game's dark and light pictures.
  const std::array<std::uint32_t, 2> backgrounds{argb(40, 44, 42), argb(150, 180, 200)};
  for (int i = 0; i < cols; ++i) {
    const RgbaImage icon = art.button_icon(static_cast<ButtonIconStyle>(i));
    const int zoom = std::max(1, std::min((kCell - 8) / icon.width, (kCell - 8) / icon.height));
    const RgbaImage big = resize_nearest(icon, icon.width * zoom, icon.height * zoom);
    for (int row = 0; row < 2; ++row) {
      const int x = kGap + i * kStep;
      const int y = kGap + row * kStep;
      fill_rect(sheet, x, y, kCell, kCell, backgrounds[static_cast<std::size_t>(row)]);
      blit(sheet, big, x + (kCell - big.width) / 2, y + (kCell - big.height) / 2);
    }
  }
  return sheet;
}

}  // namespace sh
