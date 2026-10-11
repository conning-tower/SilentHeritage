#include "game/stations/chart_tools.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numbers>
#include <utility>

#include "core/byte_reader.h"
#include "gfx/text.h"
#include "sim/state_io.h"
#include "ui/draw.h"

namespace sh {

namespace {

// The greys of the CHART.SHP buttons (CONN.PCX's colours 0..15).
constexpr std::uint8_t kBlack = 0;
constexpr std::uint8_t kBevelLight = 0x05;
constexpr std::uint8_t kEngraveLight = 0x0a;
constexpr std::uint8_t kPlate = 0x0b;
constexpr std::uint8_t kBevelDark = 0x0e;

// The world units in a yard and the yards in a nautical mile.
constexpr double kWorldPerYard = 48.0;
constexpr double kYardsPerMile = 2025.4;

// The version of the "CHRT" save section.
constexpr std::uint32_t kSectionVersion = 1;

// The inks' colours (6-bit DAC values).
constexpr std::array<std::array<int, 3>, kChartInkCount> kInkRgb{{{0, 0, 0}, {52, 0, 0}, {63, 54, 0}, {63, 63, 63}}};

GlyphColourTable solid_table(std::uint8_t colour) {
  GlyphColourTable t;
  t.fill(colour);
  t[0xff] = 0xff;
  return t;
}

void draw_circle(const Viewport& vp, int cx, int cy, int r, std::uint8_t colour) {
  int x = r;
  int y = 0;
  int err = 1 - r;
  while (x >= y) {
    for (const auto& [dx, dy] : {std::pair{x, y}, {y, x}, {-y, x}, {-x, y}, {-x, -y}, {-y, -x}, {y, -x}, {x, -y}}) {
      ui::put_pixel(vp, cx + dx, cy + dy, colour);
    }
    ++y;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      --x;
      err += 2 * (y - x) + 1;
    }
  }
}

// The plate of a button `w` x `h` at (x, y), raised or sunk.
void draw_plate(const Viewport& vp, int x, int y, int w, int h, bool down) {
  ui::fill_rect(vp, x, y, x + w - 1, y + h - 1, kPlate);
  const std::uint8_t lit = down ? kBevelDark : kBevelLight;
  const std::uint8_t shade = down ? kBevelLight : kBevelDark;
  ui::draw_line(vp, x, y, x + w - 1, y, lit);
  ui::draw_line(vp, x, y, x, y + h - 1, lit);
  ui::draw_line(vp, x, y + h - 1, x + w - 1, y + h - 1, shade);
  ui::draw_line(vp, x + w - 1, y + 1, x + w - 1, y + h - 1, shade);
}

// Draws the engraved segments `lines` (x0, y0, x1, y1 from the plate's
// corner): a dark groove lit on its lower right.
void engrave(const Viewport& vp, int x, int y, std::initializer_list<std::array<int, 4>> lines) {
  for (const std::array<int, 4>& l : lines) {
    ui::draw_line(vp, x + l[0] + 1, y + l[1] + 1, x + l[2] + 1, y + l[3] + 1, kEngraveLight);
  }
  for (const std::array<int, 4>& l : lines) {
    ui::draw_line(vp, x + l[0], y + l[1], x + l[2], y + l[3], kBevelDark);
  }
}

void engrave_symbol(const Viewport& vp, int x, int y, ChartSymbol symbol) {
  draw_chart_symbol(vp, x + 1, y + 1, symbol, kEngraveLight);
  draw_chart_symbol(vp, x, y, symbol, kBevelDark);
}

}  // namespace

ChartInks ChartInks::nearest(const VgaPalette& palette) {
  ChartInks inks;
  for (std::size_t k = 0; k < kChartInkCount; ++k) {
    int best = 1 << 30;
    for (std::size_t i = 0; i < 256; ++i) {
      int d = 0;
      for (std::size_t c = 0; c < 3; ++c) {
        const int delta = palette[i * 3 + c] - kInkRgb[k][c];
        d += delta * delta;
      }
      if (d < best) {
        best = d;
        inks.index[k] = static_cast<std::uint8_t>(i);
      }
    }
  }
  return inks;
}

void ChartAnnotations::add(ChartAnnotation a) {
  undo_ = items_;
  items_.push_back(std::move(a));
}

void ChartAnnotations::erase(std::size_t index) {
  if (index < items_.size()) {
    undo_ = items_;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
  }
}

void ChartAnnotations::clear() {
  if (!items_.empty()) {
    undo_ = items_;
    items_.clear();
  }
}

bool ChartAnnotations::undo() {
  if (!undo_) {
    return false;
  }
  items_ = std::move(*undo_);
  undo_.reset();
  return true;
}

void ChartAnnotations::assign(std::vector<ChartAnnotation> items) {
  items_ = std::move(items);
  undo_.reset();
}

std::vector<std::uint8_t> ChartAnnotations::encode() const {
  ByteWriter out;
  out.u32(kSectionVersion);
  out.u32(static_cast<std::uint32_t>(items_.size()));
  for (const ChartAnnotation& a : items_) {
    out.u8(static_cast<std::uint8_t>(a.kind));
    out.u8(static_cast<std::uint8_t>(a.ink));
    out.u8(static_cast<std::uint8_t>(a.symbol));
    out.u32(static_cast<std::uint32_t>(a.points.size()));
    for (const geom::WorldXZ& p : a.points) {
      out.i32(p.x);
      out.i32(p.z);
    }
    const std::size_t length = std::min(a.text.size(), kChartNoteMaxLength);
    out.u8(static_cast<std::uint8_t>(length));
    out.bytes(std::span(reinterpret_cast<const std::uint8_t*>(a.text.data()), length));
  }
  return out.take();
}

std::vector<ChartAnnotation> ChartAnnotations::decode(std::span<const std::uint8_t> data) {
  ByteReader in(data, "chart annotations");
  if (in.u32() != kSectionVersion) {
    in.fail("unknown chart annotations version");
  }
  const std::uint32_t count = in.u32();
  std::vector<ChartAnnotation> items;
  for (std::uint32_t i = 0; i < count; ++i) {
    ChartAnnotation a;
    const std::uint8_t kind = in.u8();
    const std::uint8_t ink = in.u8();
    const std::uint8_t symbol = in.u8();
    if (kind > static_cast<std::uint8_t>(ChartAnnotation::Kind::kNote) || ink >= kChartInkCount ||
        symbol >= kChartSymbolCount) {
      in.fail("bad chart annotation");
    }
    a.kind = static_cast<ChartAnnotation::Kind>(kind);
    a.ink = static_cast<ChartInk>(ink);
    a.symbol = static_cast<ChartSymbol>(symbol);
    const std::uint32_t points = in.u32();
    if (in.remaining() < std::size_t{points} * 8) {
      in.fail("truncated chart annotation");
    }
    for (std::uint32_t k = 0; k < points; ++k) {
      const std::int32_t x = in.i32();
      a.points.push_back({x, in.i32()});
    }
    const std::size_t need =
        a.kind == ChartAnnotation::Kind::kMark || a.kind == ChartAnnotation::Kind::kNote ? 1 : 2;
    if (a.points.size() < need) {
      in.fail("bad chart annotation");
    }
    const std::span<const std::uint8_t> text = in.bytes(in.u8());
    a.text.assign(text.begin(), text.end());
    items.push_back(std::move(a));
  }
  if (!in.at_end()) {
    in.fail("trailing chart annotation bytes");
  }
  return items;
}

void draw_chart_symbol(const Viewport& vp, int x, int y, ChartSymbol symbol, std::uint8_t colour) {
  switch (symbol) {
    case ChartSymbol::kCross:
      ui::draw_line(vp, x - 3, y - 3, x + 3, y + 3, colour);
      ui::draw_line(vp, x - 3, y + 3, x + 3, y - 3, colour);
      ui::draw_line(vp, x - 2, y - 3, x + 3, y + 2, colour);
      ui::draw_line(vp, x - 3, y + 2, x + 2, y - 3, colour);
      break;
    case ChartSymbol::kCircle:
      draw_circle(vp, x, y, 4, colour);
      draw_circle(vp, x, y, 3, colour);
      break;
    case ChartSymbol::kTriangle:
      ui::draw_line(vp, x, y - 4, x - 4, y + 3, colour);
      ui::draw_line(vp, x, y - 4, x + 4, y + 3, colour);
      ui::draw_line(vp, x - 4, y + 3, x + 4, y + 3, colour);
      break;
    case ChartSymbol::kSquare:
      ui::draw_line(vp, x - 3, y - 3, x + 3, y - 3, colour);
      ui::draw_line(vp, x - 3, y + 3, x + 3, y + 3, colour);
      ui::draw_line(vp, x - 3, y - 3, x - 3, y + 3, colour);
      ui::draw_line(vp, x + 3, y - 3, x + 3, y + 3, colour);
      break;
  }
}

namespace {

struct Point {
  double x = 0;
  double y = 0;
};

// The viewport positions of world points: the view's own transform when
// the point is drawable, else its linear map in doubles (a point far off
// the view at a high zoom).
class ScreenMap {
 public:
  explicit ScreenMap(const ChartView& view)
      : view_(view), w_(view.x1() - view.x0() + 1), h_(view.y1() - view.y0() + 1) {
    const std::array<std::int32_t, 2> a = view.screen_to_world(0, 0);
    const std::array<std::int32_t, 2> b = view.screen_to_world(w_ - 1, h_ - 1);
    ox_ = a[0];
    oz_ = a[1];
    sx_ = (w_ - 1) / static_cast<double>(b[0] - a[0]);
    sz_ = (h_ - 1) / static_cast<double>(b[1] - a[1]);
  }

  Point operator()(geom::WorldXZ p) const {
    std::int32_t x = 0;
    std::int32_t y = 0;
    if (view_.point_on_screen(p.x, p.z, x, y)) {
      return {static_cast<double>(x), static_cast<double>(y)};
    }
    return {(p.x - ox_) * sx_, (p.z - oz_) * sz_};
  }
  // Returns the pixels per world unit (the larger of the two axes).
  double scale() const { return std::max(std::abs(sx_), std::abs(sz_)); }
  int width() const { return w_; }
  int height() const { return h_; }

 private:
  const ChartView& view_;
  int w_;
  int h_;
  double ox_ = 0;
  double oz_ = 0;
  double sx_ = 1;
  double sz_ = 1;
};

// Clips segment `a`-`b` to the viewport and a margin (Liang-Barsky);
// returns false when nothing is left.
bool clip(const ScreenMap& map, Point& a, Point& b) {
  constexpr double kMargin = 8;
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  double t0 = 0;
  double t1 = 1;
  const auto edge = [&](double p, double q) {
    if (p == 0) {
      return q >= 0;
    }
    const double r = q / p;
    if (p < 0) {
      if (r > t1) {
        return false;
      }
      t0 = std::max(t0, r);
    } else {
      if (r < t0) {
        return false;
      }
      t1 = std::min(t1, r);
    }
    return true;
  };
  if (!edge(-dx, a.x + kMargin) || !edge(dx, map.width() + kMargin - a.x) || !edge(-dy, a.y + kMargin) ||
      !edge(dy, map.height() + kMargin - a.y)) {
    return false;
  }
  b = {a.x + t1 * dx, a.y + t1 * dy};
  a = {a.x + t0 * dx, a.y + t0 * dy};
  return true;
}

int px(double v) { return static_cast<int>(std::lround(v)); }

// Draws segment `a`-`b`, clipped; returns the visible part when there is
// one.
std::optional<std::array<Point, 2>> draw_segment(const Viewport& vp, const ScreenMap& map, Point a, Point b,
                                                 std::uint8_t colour) {
  if (!clip(map, a, b)) {
    return std::nullopt;
  }
  ui::draw_line(vp, px(a.x), px(a.y), px(b.x), px(b.y), colour);
  return std::array{a, b};
}

double yards_between(geom::WorldXZ a, geom::WorldXZ b) {
  return std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.z) - a.z) / kWorldPerYard;
}

std::string bearing_text(geom::WorldXZ from, geom::WorldXZ to) {
  int bearing = static_cast<int>(
      std::lround(std::atan2(static_cast<double>(to.x) - from.x, static_cast<double>(to.z) - from.z) * 180.0 /
                  std::numbers::pi));
  bearing = (bearing + 360) % 360;
  std::string b = std::to_string(bearing);
  b.insert(0, 3 - b.size(), '0');
  return b + "\x1a";
}

// The screen box of what was drawn, for the eraser's frame.
struct Box {
  int x0 = std::numeric_limits<int>::max();
  int y0 = std::numeric_limits<int>::max();
  int x1 = std::numeric_limits<int>::min();
  int y1 = std::numeric_limits<int>::min();
  void add(int x, int y) {
    x0 = std::min(x0, x);
    y0 = std::min(y0, y);
    x1 = std::max(x1, x);
    y1 = std::max(y1, y);
  }
  bool empty() const { return x1 < x0; }
};

void draw_label(const Viewport& vp, int x, int y, const Font& font, const std::string& text, std::uint8_t colour,
                Box& box) {
  const GlyphColourTable table = solid_table(colour);
  gui_draw_text(vp, x, y, font, text, &table);
  box.add(x, y);
  box.add(x + font.text_width(text), y + font.height() - 3);
}

// The points of circle `a` on the screen, closed (the first repeated), or
// none when it is too large to draw.
std::vector<Point> circle_points(const ScreenMap& map, const ChartAnnotation& a) {
  const double radius = std::hypot(static_cast<double>(a.points[1].x) - a.points[0].x,
                                   static_cast<double>(a.points[1].z) - a.points[0].z);
  const double r = radius * map.scale();
  if (r > 1e7) {
    return {};
  }
  const int n = std::clamp(static_cast<int>(r / 2), 24, 4096);
  std::vector<Point> points;
  points.reserve(static_cast<std::size_t>(n) + 1);
  for (int k = 0; k <= n; ++k) {
    const double t = 2 * std::numbers::pi * k / n;
    points.push_back(map({a.points[0].x + static_cast<std::int32_t>(std::lround(radius * std::cos(t))),
                          a.points[0].z + static_cast<std::int32_t>(std::lround(radius * std::sin(t)))}));
  }
  return points;
}

// Draws `a` and returns the box of what was drawn.
Box draw_one(const Viewport& vp, const ScreenMap& map, const ChartAnnotation& a, const ChartInks& inks,
             const Font& font) {
  const std::uint8_t colour = inks[a.ink];
  Box box;
  switch (a.kind) {
    case ChartAnnotation::Kind::kStroke:
    case ChartAnnotation::Kind::kLine: {
      const bool labelled = a.kind == ChartAnnotation::Kind::kLine;
      double total = 0;
      for (std::size_t i = 1; i < a.points.size(); ++i) {
        const Point pa = map(a.points[i - 1]);
        const Point pb = map(a.points[i]);
        const std::optional<std::array<Point, 2>> seen = draw_segment(vp, map, pa, pb, colour);
        const double yards = yards_between(a.points[i - 1], a.points[i]);
        total += yards;
        if (!seen) {
          continue;
        }
        box.add(px((*seen)[0].x), px((*seen)[0].y));
        box.add(px((*seen)[1].x), px((*seen)[1].y));
        const double len = std::hypot(pb.x - pa.x, pb.y - pa.y);
        if (!labelled || len == 0) {
          continue;
        }
        // The reading by the middle of the segment's visible part, on its
        // upper side, when the whole segment is longer than it.
        const std::string text = chart_range_text(yards) + " " + bearing_text(a.points[i - 1], a.points[i]);
        const int tw = font.text_width(text);
        if (len <= tw + 12) {
          continue;
        }
        double nx = -(pb.y - pa.y) / len;
        double ny = (pb.x - pa.x) / len;
        if (ny > 0) {
          nx = -nx;
          ny = -ny;
        }
        const int mx = px(((*seen)[0].x + (*seen)[1].x) / 2 + nx * 8);
        const int my = px(((*seen)[0].y + (*seen)[1].y) / 2 + ny * 8);
        const int left = nx < -0.3 ? mx - tw : nx > 0.3 ? mx : mx - tw / 2;
        draw_label(vp, left, my - font.height() / 2, font, text, colour, box);
      }
      if (labelled && a.points.size() > 2) {
        const Point last = map(a.points.back());
        if (last.x > -8 && last.x < map.width() && last.y > -8 && last.y < map.height()) {
          draw_label(vp, px(last.x) + 6, px(last.y) + 2, font, "TOTAL " + chart_range_text(total), colour, box);
        }
      }
      break;
    }
    case ChartAnnotation::Kind::kCircle: {
      const std::vector<Point> points = circle_points(map, a);
      for (std::size_t k = 1; k < points.size(); ++k) {
        if (const auto seen = draw_segment(vp, map, points[k - 1], points[k], colour)) {
          box.add(px((*seen)[0].x), px((*seen)[0].y));
          box.add(px((*seen)[1].x), px((*seen)[1].y));
        }
      }
      const Point c = map(a.points[0]);
      ui::draw_line(vp, px(c.x) - 2, px(c.y), px(c.x) + 2, px(c.y), colour);
      ui::draw_line(vp, px(c.x), px(c.y) - 2, px(c.x), px(c.y) + 2, colour);
      // The radius by the north-east point (an eighth of the points).
      if (!points.empty()) {
        const Point ne = points[(points.size() - 1) / 8];
        if (std::hypot(ne.x - c.x, ne.y - c.y) >= 8 && ne.x > -64 && ne.x < map.width() && ne.y > 0 &&
            ne.y < map.height() + 16) {
          draw_label(vp, px(ne.x) + 3, px(ne.y) - font.height() + 2, font,
                     "R " + chart_range_text(yards_between(a.points[0], a.points[1])), colour, box);
        }
      }
      break;
    }
    case ChartAnnotation::Kind::kMark: {
      const Point p = map(a.points[0]);
      if (p.x > -8 && p.x < map.width() + 8 && p.y > -8 && p.y < map.height() + 8) {
        draw_chart_symbol(vp, px(p.x), px(p.y), a.symbol, colour);
        box.add(px(p.x) - 4, px(p.y) - 4);
        box.add(px(p.x) + 5, px(p.y) + 5);
      }
      break;
    }
    case ChartAnnotation::Kind::kNote: {
      const Point p = map(a.points[0]);
      if (p.x > -400 && p.x < map.width() + 8 && p.y > -16 && p.y < map.height() + 16) {
        const int x = px(p.x);
        const int y = px(p.y);
        ui::fill_rect(vp, x - 1, y - 1, x + 1, y + 1, colour);
        box.add(x - 1, y - 1);
        draw_label(vp, x + 4, y - font.height() / 2, font, a.text, colour, box);
      }
      break;
    }
  }
  return box;
}

double segment_distance(Point p, Point a, Point b) {
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double l2 = dx * dx + dy * dy;
  const double t = l2 == 0 ? 0 : std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / l2, 0.0, 1.0);
  return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

}  // namespace

ChartPointer::Step ChartPointer::update(int x, int y, std::uint8_t buttons) {
  if (button_ == 0) {
    return Step::kNone;
  }
  if ((buttons & button_) == 0) {
    const bool dragged = dragging_;
    button_ = 0;
    return dragged ? Step::kDragEnd : Step::kClick;
  }
  if (!dragging_) {
    if (std::abs(x - start_x_) < kChartDragThreshold && std::abs(y - start_y_) < kChartDragThreshold) {
      return Step::kNone;
    }
    dragging_ = true;
    last_x_ = x;
    last_y_ = y;
    return Step::kDragBegin;
  }
  if (x == last_x_ && y == last_y_) {
    return Step::kNone;
  }
  last_x_ = x;
  last_y_ = y;
  return Step::kDragMove;
}

void ChartPointer::press(int x, int y, std::uint8_t button) {
  button_ = button;
  dragging_ = false;
  start_x_ = last_x_ = x;
  start_y_ = last_y_ = y;
}

std::string chart_range_text(double yards) {
  if (yards < 10 * kYardsPerMile) {
    return std::to_string(std::lround(yards / 10.0) * 10) + " YDS";
  }
  const long tenths = std::lround(yards / kYardsPerMile * 10.0);
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " NM";
}

ChartScale chart_nautical_scale(std::int32_t zoom_int) {
  constexpr std::array<double, 10> kRound{1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 8};
  constexpr double kBarYards = 176000;
  constexpr int kBarLength = 256;
  const double yards = kBarYards / std::max(zoom_int, 1);
  const bool in_yards = yards < 10 * kYardsPerMile;
  const double span = in_yards ? yards : yards / kYardsPerMile;
  const double power = std::pow(10.0, std::floor(std::log10(span)));
  double best = power;
  for (const double r : kRound) {
    if (r * power <= span) {
      best = r * power;
    }
  }
  const long rounded = std::lround(best);
  return {.length = static_cast<int>(std::lround(kBarLength * best / span)),
          .value = static_cast<int>(in_yards ? rounded / 10 : rounded),
          .yards = in_yards};
}

void draw_chart_scale_bar(const Viewport& vp, int x, int y, int length) {
  // The bar's colours (CONN.PCX).
  constexpr std::uint8_t kDark = 2;
  constexpr std::uint8_t kLight = 3;
  constexpr int kDivisions = 20;
  const auto tick = [length](int k) { return k * length / kDivisions; };
  for (int b = 0; b < 4; ++b) {
    const std::uint8_t top = b % 2 == 0 ? kLight : kDark;
    const std::uint8_t bottom = b % 2 == 0 ? kDark : kLight;
    ui::fill_rect(vp, x + tick(5 * b), y + 2, x + tick(5 * b + 5) - 1, y + 3, top);
    ui::fill_rect(vp, x + tick(5 * b), y + 4, x + tick(5 * b + 5) - 1, y + 5, bottom);
  }
  for (int k = 0; k <= kDivisions; ++k) {
    ui::draw_line(vp, x + tick(k), y + (k % 5 == 0 ? 0 : 1), x + tick(k), y + 5, kDark);
  }
}

std::string chart_ruler_text(geom::WorldXZ from, geom::WorldXZ to) {
  return chart_range_text(yards_between(from, to)) + "  " + bearing_text(from, to);
}

void draw_chart_annotations(const Viewport& vp, const ChartView& view, const ChartAnnotations& annotations,
                            const ChartInks& inks, const Font& font, int highlight) {
  const ScreenMap map(view);
  for (std::size_t i = 0; i < annotations.items().size(); ++i) {
    const ChartAnnotation& a = annotations.items()[i];
    const Box box = draw_one(vp, map, a, inks, font);
    if (static_cast<int>(i) != highlight || box.empty()) {
      continue;
    }
    // A dashed frame round the annotation the eraser would remove.
    const std::uint8_t frame = a.ink == ChartInk::kWhite ? inks[ChartInk::kBlack] : inks[ChartInk::kWhite];
    const int x0 = box.x0 - 3, y0 = box.y0 - 3, x1 = box.x1 + 3, y1 = box.y1 + 3;
    for (const std::array<int, 4>& e : {std::array{x0, y0, x1, y0}, std::array{x0, y1, x1, y1},
                                        std::array{x0, y0, x0, y1}, std::array{x1, y0, x1, y1}}) {
      int step = 0;
      ui::draw_line(vp, e[0], e[1], e[2], e[3], [&](int x, int y) {
        if ((step++ / 3) % 2 == 0) {
          ui::put_pixel(vp, x, y, frame);
        }
      });
    }
  }
}

void draw_chart_annotation(const Viewport& vp, const ChartView& view, const ChartAnnotation& a, const ChartInks& inks,
                           const Font& font) {
  draw_one(vp, ScreenMap(view), a, inks, font);
}

std::optional<std::size_t> pick_chart_annotation(const ChartView& view, const ChartAnnotations& annotations,
                                                 const Font& font, int x, int y, int radius) {
  const ScreenMap map(view);
  const Point p{static_cast<double>(x), static_cast<double>(y)};
  const auto near_polyline = [&](const std::vector<Point>& points) {
    for (std::size_t k = 1; k < points.size(); ++k) {
      if (segment_distance(p, points[k - 1], points[k]) <= radius) {
        return true;
      }
    }
    return false;
  };
  for (std::size_t i = annotations.items().size(); i-- > 0;) {
    const ChartAnnotation& a = annotations.items()[i];
    bool hit = false;
    switch (a.kind) {
      case ChartAnnotation::Kind::kStroke:
      case ChartAnnotation::Kind::kLine: {
        std::vector<Point> points;
        for (const geom::WorldXZ& w : a.points) {
          points.push_back(map(w));
        }
        hit = near_polyline(points);
        break;
      }
      case ChartAnnotation::Kind::kCircle:
        hit = near_polyline(circle_points(map, a));
        break;
      case ChartAnnotation::Kind::kMark: {
        const Point m = map(a.points[0]);
        hit = std::abs(m.x - p.x) <= 4 + radius && std::abs(m.y - p.y) <= 4 + radius;
        break;
      }
      case ChartAnnotation::Kind::kNote: {
        const Point n = map(a.points[0]);
        const double top = n.y - font.height() / 2;
        hit = std::hypot(n.x - p.x, n.y - p.y) <= radius ||
              (p.x >= n.x && p.x <= n.x + 4 + font.text_width(a.text) && p.y >= top && p.y <= top + font.height());
        break;
      }
    }
    if (hit) {
      return i;
    }
  }
  return std::nullopt;
}

void draw_chart_ruler(const Viewport& vp, const ChartView& view, geom::WorldXZ from, geom::WorldXZ to,
                      std::uint8_t colour, const Font& font) {
  const ScreenMap map(view);
  const Point pa = map(from);
  const Point pb = map(to);
  Point a = pa;
  Point b = pb;
  if (clip(map, a, b)) {
    int step = 0;
    ui::draw_line(vp, px(a.x), px(a.y), px(b.x), px(b.y), [&](int x, int y) {
      if ((step++ / 4) % 2 == 0) {
        ui::put_pixel(vp, x, y, colour);
      }
    });
  }
  draw_circle(vp, px(pa.x), px(pa.y), 4, colour);
  draw_circle(vp, px(pb.x), px(pb.y), 4, colour);
  const std::string text = chart_ruler_text(from, to);
  const int w = font.text_width(text) + 5;
  const int h = font.height() + 2;
  const int x = std::clamp(px((pa.x + pb.x) / 2) + 6, 0, std::max(0, map.width() - w));
  const int y = std::clamp(px((pa.y + pb.y) / 2) - h / 2, 0, std::max(0, map.height() - h));
  ui::fill_rect(vp, x, y, x + w - 1, y + h - 1, kBlack);
  ui::draw_line(vp, x, y, x + w - 1, y, colour);
  ui::draw_line(vp, x, y + h - 1, x + w - 1, y + h - 1, colour);
  ui::draw_line(vp, x, y, x, y + h - 1, colour);
  ui::draw_line(vp, x + w - 1, y, x + w - 1, y + h - 1, colour);
  const GlyphColourTable table = solid_table(colour);
  gui_draw_text(vp, x + 3, y + 2, font, text, &table);
}

void draw_chart_tool_button(const Viewport& vp, int x, int y, ChartTool tool, bool down) {
  draw_plate(vp, x, y, kChartToolButtonW, kChartToolButtonH, down);
  const int ox = x + (down ? 1 : 0);
  const int oy = y + (down ? 1 : 0);
  switch (tool) {
    case ChartTool::kPencil:
      engrave(vp, ox, oy,
              {{7, 13, 14, 6}, {8, 14, 15, 7}, {14, 6, 15, 7}, {12, 6, 15, 9}, {7, 13, 4, 16}, {8, 14, 4, 16}});
      break;
    case ChartTool::kLine:
      engrave(vp, ox, oy, {{5, 14, 15, 5}, {3, 13, 5, 13}, {3, 15, 5, 15}, {15, 4, 17, 4}, {15, 6, 17, 6}});
      break;
    case ChartTool::kCircle:
      draw_circle(vp, ox + 11, oy + 10, 6, kEngraveLight);
      draw_circle(vp, ox + 10, oy + 9, 6, kBevelDark);
      engrave(vp, ox, oy, {{10, 9, 15, 6}, {9, 9, 11, 9}, {10, 8, 10, 10}});
      break;
    case ChartTool::kMark:
      engrave_symbol(vp, ox + 7, oy + 9, ChartSymbol::kCross);
      engrave_symbol(vp, ox + 13, oy + 9, ChartSymbol::kCircle);
      break;
    case ChartTool::kNote:
      engrave(vp, ox, oy, {{4, 4, 14, 4}, {9, 4, 9, 15}, {7, 15, 11, 15}, {4, 4, 4, 6}, {14, 4, 14, 6}});
      break;
    case ChartTool::kRuler:
      // Dividers: the hinge and two legs.
      engrave(vp, ox, oy, {{9, 3, 11, 3}, {10, 2, 10, 5}, {9, 5, 5, 16}, {11, 5, 15, 16}, {7, 11, 13, 11}});
      break;
    case ChartTool::kEraser:
      engrave(vp, ox, oy, {{3, 12, 10, 5}, {10, 5, 16, 11}, {16, 11, 9, 18}, {9, 18, 3, 12}, {6, 9, 12, 15}});
      break;
  }
}

void draw_chart_undo_button(const Viewport& vp, int x, int y, bool down) {
  draw_plate(vp, x, y, kChartToolButtonW, kChartToolButtonH, down);
  const int ox = x + (down ? 1 : 0);
  const int oy = y + (down ? 1 : 0);
  // A hooked arrow turning back.
  engrave(vp, ox, oy,
          {{5, 9, 8, 6}, {8, 6, 12, 6}, {12, 6, 15, 9}, {15, 9, 15, 11}, {15, 11, 12, 14}, {12, 14, 8, 14},
           {5, 9, 5, 5}, {5, 9, 9, 9}});
}

void draw_chart_symbol_button(const Viewport& vp, int x, int y, ChartSymbol symbol, bool down) {
  draw_plate(vp, x, y, kChartSmallButton, kChartSmallButton, down);
  engrave_symbol(vp, x + 7 + (down ? 1 : 0), y + 7 + (down ? 1 : 0), symbol);
}

void draw_chart_ink_swatch(const Viewport& vp, int x, int y, std::uint8_t colour, bool selected) {
  draw_plate(vp, x, y, kChartSmallButton, kChartSmallButton, selected);
  ui::fill_rect(vp, x + 3, y + 3, x + 12, y + 12, colour);
  if (selected) {
    ui::draw_line(vp, x + 2, y + 2, x + 13, y + 2, kBlack);
    ui::draw_line(vp, x + 2, y + 13, x + 13, y + 13, kBlack);
    ui::draw_line(vp, x + 2, y + 2, x + 2, y + 13, kBlack);
    ui::draw_line(vp, x + 13, y + 2, x + 13, y + 13, kBlack);
  }
}

void draw_chart_tools_button(const Viewport& vp, int x, int y, bool down) {
  draw_plate(vp, x, y, kChartToolsButtonW, kChartToolButtonH, down);
  const int ox = x + (down ? 1 : 0);
  const int oy = y + (down ? 1 : 0);
  // The dividers over a pencil.
  engrave(vp, ox, oy,
          {{11, 3, 13, 3}, {12, 2, 12, 5}, {11, 5, 7, 16}, {13, 5, 17, 16}, {9, 11, 15, 11}, {22, 13, 33, 5},
           {23, 14, 34, 6}, {33, 5, 34, 6}, {22, 13, 20, 16}, {23, 14, 20, 16}});
}

}  // namespace sh
