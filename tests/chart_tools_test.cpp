// Tests of the chart's annotation tools (chart_tools.h; the extended
// variant's `Features::chart_tools`) on a synthetic map and font: the
// readings, the undo step, the "CHRT" section, the click / drag split, and
// where the annotations land at several zooms.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "assets/world.h"
#include "core/error.h"
#include "game/stations/chart_tools.h"
#include "game/stations/chart_view.h"
#include "ui/event.h"

namespace sh {
namespace {

using Kind = ChartAnnotation::Kind;

// The map's ink indices, away from the sea colour 0x80 and the font's.
constexpr ChartInks kInks{{1, 2, 3, 4}};
constexpr std::uint8_t kSea = 0x80;
// The view: the original's map rectangle.
constexpr int kViewW = 0x280;
constexpr int kViewH = 0x1cb;
// One yard.
constexpr std::int32_t kYard = 48;

// A sea of 64 x 48 grid cells under a 1201 x 801 chart.
World sea_world() {
  World w;
  w.grid = {64, 48, std::vector<std::uint8_t>(64 * 48, 0)};
  w.chart = {1201, 801, std::vector<std::uint8_t>(1201 * 801, kSea)};
  return w;
}

// A font of 6-pixel-high glyphs, 4 wide, every pixel drawn.
Font block_font() {
  std::vector<std::uint8_t> data{'1', '.', 0, 0};
  const auto u32 = [&data](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
      data.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
  };
  constexpr std::uint32_t kGlyphs = 128;
  constexpr std::uint32_t kW = 4;
  constexpr std::uint32_t kH = 6;
  u32(kGlyphs);
  u32(kH);
  u32(0xff);
  const std::uint32_t first = 16 + kGlyphs * 4;
  for (std::uint32_t i = 0; i < kGlyphs; ++i) {
    u32(first + i * (4 + kW * kH));
  }
  for (std::uint32_t i = 0; i < kGlyphs; ++i) {
    u32(kW);
    data.insert(data.end(), kW * kH, 1);
  }
  return Font::parse(data, "block font");
}

struct Rig {
  World world = sea_world();
  ChartView view{world, 0, 0, kViewW - 1, kViewH - 1};
  Image8 screen{kViewW, kViewH, 0};
  Font font = block_font();

  // Centres the view on world point `at` at `zoom` (whole), placed at 1x
  // (`world_to_chart` goes through the 16.16 screen position).
  void look(geom::WorldXZ at, std::int32_t zoom) {
    view.set_zoom(0x10000);
    const std::array<std::int32_t, 2> c = view.world_to_chart(at.x, at.z);
    view.set_zoom(zoom << 16);
    view.set_center(c[0], c[1]);
  }
  Viewport vp() { return Viewport::full(screen); }
  void draw(const ChartAnnotations& a, int highlight = -1) {
    screen.fill(0);
    draw_chart_annotations(vp(), view, a, kInks, font, highlight);
  }
  std::array<int, 2> at(geom::WorldXZ p) const {
    const ChartPoint c = view.world_to_screen(p.x, p.z);
    return {c.x, c.y};
  }
  std::uint8_t pixel(std::array<int, 2> p) const { return screen.at(p[0], p[1]); }
  // Returns how many pixels of `colour` lie in the rectangle.
  int count(std::uint8_t colour, int x0, int y0, int x1, int y1) const {
    int n = 0;
    for (int y = std::max(y0, 0); y <= std::min(y1, kViewH - 1); ++y) {
      for (int x = std::max(x0, 0); x <= std::min(x1, kViewW - 1); ++x) {
        n += screen.at(x, y) == colour ? 1 : 0;
      }
    }
    return n;
  }
};

// The middle of the synthetic map, in world units.
geom::WorldXZ middle(const World& w) { return {w.cell_size() * 32, w.cell_size() * 24}; }

ChartAnnotation mark(geom::WorldXZ p, ChartInk ink = ChartInk::kRed) {
  return {.kind = Kind::kMark, .ink = ink, .symbol = ChartSymbol::kCross, .points = {p}, .text = {}};
}

ChartAnnotation line(std::vector<geom::WorldXZ> points, ChartInk ink = ChartInk::kBlack) {
  return {.kind = Kind::kLine, .ink = ink, .symbol = ChartSymbol::kCross, .points = std::move(points), .text = {}};
}

}  // namespace

TEST_CASE("Chart tool ranges are in yards below 10 nautical miles", "[charttools]") {
  CHECK(chart_range_text(0) == "0 YDS");
  CHECK(chart_range_text(4214) == "4210 YDS");
  CHECK(chart_range_text(4215) == "4220 YDS");
  CHECK(chart_range_text(20253) == "20250 YDS");
  CHECK(chart_range_text(20254) == "10.0 NM");
  CHECK(chart_range_text(2025.4 * 12.46) == "12.5 NM");
  CHECK(chart_range_text(2025.4 * 136.6) == "136.6 NM");
}

TEST_CASE("The ruler reads the true bearing", "[charttools]") {
  const geom::WorldXZ o{1000000, 1000000};
  CHECK(chart_ruler_text(o, {o.x, o.z + 100 * kYard}) == "100 YDS  000\x1a");
  CHECK(chart_ruler_text(o, {o.x + 100 * kYard, o.z}) == "100 YDS  090\x1a");
  CHECK(chart_ruler_text(o, {o.x, o.z - 100 * kYard}) == "100 YDS  180\x1a");
  CHECK(chart_ruler_text(o, {o.x - 100 * kYard, o.z}) == "100 YDS  270\x1a");
  CHECK(chart_ruler_text(o, {o.x + 3000 * kYard, o.z + 3000 * kYard}) == "4240 YDS  045\x1a");
}

TEST_CASE("Chart annotations undo their last change only", "[charttools]") {
  ChartAnnotations a;
  CHECK_FALSE(a.can_undo());
  CHECK_FALSE(a.undo());
  a.add(mark({1, 2}));
  a.add(mark({3, 4}));
  REQUIRE(a.undo());
  REQUIRE(a.items().size() == 1);
  CHECK(a.items()[0].points[0].x == 1);
  CHECK_FALSE(a.undo());

  a.add(mark({5, 6}));
  a.erase(0);
  REQUIRE(a.items().size() == 1);
  CHECK(a.items()[0].points[0].x == 5);
  REQUIRE(a.undo());
  CHECK(a.items().size() == 2);

  a.clear();
  CHECK(a.empty());
  REQUIRE(a.undo());
  CHECK(a.items().size() == 2);

  // Nothing to erase or clear leaves the undo step alone.
  a.erase(7);
  CHECK_FALSE(a.can_undo());
  a.assign({});
  a.clear();
  CHECK_FALSE(a.can_undo());

  a.add(mark({1, 1}));
  a.assign({mark({9, 9})});
  CHECK_FALSE(a.can_undo());
}

TEST_CASE("Chart annotations round-trip through their save section", "[charttools]") {
  ChartAnnotations a;
  a.add({.kind = Kind::kStroke, .ink = ChartInk::kYellow, .symbol = ChartSymbol::kCross,
         .points = {{-5, 7}, {100, -200}, {0x7fffffff, -0x7fffffff}}, .text = {}});
  a.add(line({{1, 2}, {3, 4}}, ChartInk::kWhite));
  a.add({.kind = Kind::kCircle, .ink = ChartInk::kRed, .symbol = ChartSymbol::kCross, .points = {{10, 10}, {20, 10}},
         .text = {}});
  a.add({.kind = Kind::kMark, .ink = ChartInk::kBlack, .symbol = ChartSymbol::kSquare, .points = {{8, 9}}, .text = {}});
  a.add({.kind = Kind::kNote, .ink = ChartInk::kRed, .symbol = ChartSymbol::kCross, .points = {{8, 9}},
         .text = "Convoy at 0412"});
  const std::vector<std::uint8_t> data = a.encode();
  CHECK(ChartAnnotations::decode(data) == a.items());

  // The layout: version, count, then the first item's kind, ink, symbol.
  REQUIRE(data.size() > 11);
  CHECK(data[0] == 1);
  CHECK(data[4] == 5);
  CHECK(data[8] == static_cast<std::uint8_t>(Kind::kStroke));
  CHECK(data[9] == static_cast<std::uint8_t>(ChartInk::kYellow));

  // Notes are kept to their longest.
  ChartAnnotations long_note;
  long_note.add({.kind = Kind::kNote, .ink = ChartInk::kBlack, .symbol = ChartSymbol::kCross, .points = {{0, 0}},
                 .text = std::string(40, 'A')});
  CHECK(ChartAnnotations::decode(long_note.encode())[0].text == std::string(kChartNoteMaxLength, 'A'));

  CHECK(ChartAnnotations::decode(ChartAnnotations{}.encode()).empty());
}

TEST_CASE("A damaged chart section is refused", "[charttools]") {
  ChartAnnotations a;
  a.add(line({{1, 2}, {3, 4}}));
  const std::vector<std::uint8_t> good = a.encode();
  for (std::size_t n = 0; n < good.size(); ++n) {
    INFO("cut at " << n);
    CHECK_THROWS_AS(ChartAnnotations::decode(std::span(good).first(n)), DataError);
  }
  std::vector<std::uint8_t> bad = good;
  bad[0] = 2;  // version
  CHECK_THROWS_AS(ChartAnnotations::decode(bad), DataError);
  bad = good;
  bad[8] = 9;  // kind
  CHECK_THROWS_AS(ChartAnnotations::decode(bad), DataError);
  bad = good;
  bad[9] = static_cast<std::uint8_t>(kChartInkCount);
  CHECK_THROWS_AS(ChartAnnotations::decode(bad), DataError);
  bad = good;
  bad[11] = 1;  // a line of one point
  CHECK_THROWS_AS(ChartAnnotations::decode(bad), DataError);
  bad = good;
  bad.push_back(0);
  CHECK_THROWS_AS(ChartAnnotations::decode(bad), DataError);
}

TEST_CASE("The chart pointer tells a click from a drag", "[charttools]") {
  using Step = ChartPointer::Step;
  ChartPointer p;
  CHECK(p.update(10, 10, ui::kButtonLeft) == Step::kNone);
  p.press(100, 100, ui::kButtonLeft);
  CHECK(p.update(103, 97, ui::kButtonLeft) == Step::kNone);
  CHECK(p.update(103, 97, 0) == Step::kClick);
  CHECK_FALSE(p.active());

  p.press(100, 100, ui::kButtonLeft);
  CHECK(p.update(100, 104, ui::kButtonLeft) == Step::kDragBegin);
  CHECK(p.dragging());
  CHECK(p.update(100, 104, ui::kButtonLeft) == Step::kNone);
  // Back within the threshold, it stays a drag.
  CHECK(p.update(101, 101, ui::kButtonLeft) == Step::kDragMove);
  CHECK(p.update(101, 101, 0) == Step::kDragEnd);
  CHECK(p.start_x() == 100);

  // Only the pressed button counts.
  p.press(50, 50, ui::kButtonMiddle);
  CHECK(p.update(60, 50, ui::kButtonMiddle | ui::kButtonLeft) == Step::kDragBegin);
  CHECK(p.update(70, 50, ui::kButtonLeft) == Step::kDragEnd);

  p.press(50, 50, ui::kButtonLeft);
  p.cancel();
  CHECK(p.update(50, 50, 0) == Step::kNone);
}

TEST_CASE("The inks are the palette's nearest colours", "[charttools]") {
  VgaPalette pal{};
  const auto set = [&pal](std::size_t i, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    pal[i * 3] = r;
    pal[i * 3 + 1] = g;
    pal[i * 3 + 2] = b;
  };
  for (std::size_t i = 0; i < 256; ++i) {
    set(i, 30, 30, 30);
  }
  set(17, 0, 0, 0);
  set(18, 50, 2, 1);
  set(19, 62, 55, 2);
  set(20, 63, 63, 63);
  const ChartInks inks = ChartInks::nearest(pal);
  CHECK(inks[ChartInk::kBlack] == 17);
  CHECK(inks[ChartInk::kRed] == 18);
  CHECK(inks[ChartInk::kYellow] == 19);
  CHECK(inks[ChartInk::kWhite] == 20);
}

TEST_CASE("Annotations stay on their world points at every zoom", "[charttools]") {
  Rig rig;
  const geom::WorldXZ m = middle(rig.world);
  ChartAnnotations a;
  a.add(mark(m));
  for (std::int32_t zoom : {1, 8, 256}) {
    INFO("zoom " << zoom);
    rig.look(m, zoom);
    rig.draw(a);
    const std::array<int, 2> p = rig.at(m);
    CHECK(rig.pixel(p) == kInks[ChartInk::kRed]);
    // The symbol keeps its size: a cross of 7 pixels.
    CHECK(rig.count(kInks[ChartInk::kRed], p[0] - 5, p[1] - 5, p[0] + 5, p[1] + 5) > 12);
    CHECK(rig.count(kInks[ChartInk::kRed], 0, 0, kViewW - 1, kViewH - 1) ==
          rig.count(kInks[ChartInk::kRed], p[0] - 5, p[1] - 5, p[0] + 5, p[1] + 5));
  }
}

TEST_CASE("Lines are labelled when long enough", "[charttools]") {
  Rig rig;
  const geom::WorldXZ m = middle(rig.world);
  rig.look(m, 8);
  const std::int32_t px = rig.view.screen_to_world(1, 0)[0] - rig.view.screen_to_world(0, 0)[0];
  const std::uint8_t ink = kInks[ChartInk::kBlack];
  // A long east-west segment: one pixel wide, its reading above.
  ChartAnnotations a;
  a.add(line({{m.x - 200 * px, m.z}, {m.x + 200 * px, m.z}}));
  rig.draw(a);
  const std::array<int, 2> c = rig.at(m);
  CHECK(rig.pixel(c) == ink);
  CHECK(rig.pixel({c[0], c[1] + 1}) != ink);
  CHECK(rig.count(ink, c[0] - 100, c[1] - 16, c[0] + 100, c[1] - 3) > 50);
  CHECK(rig.count(ink, c[0] - 100, c[1] + 3, c[0] + 100, c[1] + 16) == 0);

  // A short one has no reading.
  ChartAnnotations s;
  s.add(line({{m.x - 10 * px, m.z}, {m.x + 10 * px, m.z}}));
  rig.draw(s);
  CHECK(rig.count(ink, c[0] - 100, c[1] - 16, c[0] + 100, c[1] - 3) == 0);

  // A polyline gets its total by its last point; a pencil stroke nothing.
  ChartAnnotations p;
  p.add(line({{m.x - 200 * px, m.z}, {m.x, m.z}, {m.x, m.z - 150 * px}}));
  rig.draw(p);
  const std::array<int, 2> end = rig.at({m.x, m.z - 150 * px});
  CHECK(rig.count(ink, end[0] + 5, end[1] + 1, end[0] + 120, end[1] + 10) > 50);
  ChartAnnotations stroke;
  stroke.add({.kind = Kind::kStroke, .ink = ChartInk::kBlack, .symbol = ChartSymbol::kCross,
              .points = {{m.x - 200 * px, m.z}, {m.x, m.z}, {m.x, m.z - 150 * px}}, .text = {}});
  rig.draw(stroke);
  CHECK(rig.count(ink, end[0] + 5, end[1] + 1, end[0] + 120, end[1] + 10) == 0);
  CHECK(rig.count(ink, c[0] - 100, c[1] - 16, c[0] - 20, c[1] - 3) == 0);
}

TEST_CASE("Circles go through the world points at their radius", "[charttools]") {
  Rig rig;
  const geom::WorldXZ m = middle(rig.world);
  const std::int32_t r = rig.world.cell_size();
  ChartAnnotations a;
  a.add({.kind = Kind::kCircle, .ink = ChartInk::kWhite, .symbol = ChartSymbol::kCross,
         .points = {m, {m.x + r, m.z}}, .text = {}});
  const std::uint8_t ink = kInks[ChartInk::kWhite];
  for (std::int32_t zoom : {1, 2, 8}) {
    INFO("zoom " << zoom);
    rig.look(m, zoom);
    rig.draw(a);
    for (const geom::WorldXZ p : {geom::WorldXZ{m.x + r, m.z}, geom::WorldXZ{m.x, m.z + r},
                                  geom::WorldXZ{m.x - r, m.z}, geom::WorldXZ{m.x, m.z - r}}) {
      const std::array<int, 2> s = rig.at(p);
      CHECK(rig.count(ink, s[0] - 1, s[1] - 1, s[0] + 1, s[1] + 1) > 0);
    }
    // Hollow, with a small cross at the centre.
    const std::array<int, 2> c = rig.at(m);
    CHECK(rig.pixel(c) == ink);
    const std::array<int, 2> e = rig.at({m.x + r, m.z});
    const int half = (e[0] - c[0]) / 2;
    CHECK(rig.count(ink, c[0] + 4, c[1] - 4, c[0] + half, c[1] + 4) == 0);
  }
}

TEST_CASE("Lines far off the view are clipped to it", "[charttools]") {
  Rig rig;
  const geom::WorldXZ m = middle(rig.world);
  rig.look(m, 256);
  ChartAnnotations a;
  // From the view's middle to the map's far side, beyond the view's own
  // transform (32000 pixels).
  a.add({.kind = Kind::kStroke, .ink = ChartInk::kRed, .symbol = ChartSymbol::kCross,
         .points = {m, {m.x + 30 * rig.world.cell_size(), m.z}}, .text = {}});
  rig.draw(a);
  const std::array<int, 2> c = rig.at(m);
  const std::uint8_t ink = kInks[ChartInk::kRed];
  CHECK(rig.pixel(c) == ink);
  CHECK(rig.count(ink, kViewW - 3, c[1] - 2, kViewW - 1, c[1] + 2) > 0);
  CHECK(rig.count(ink, 0, 0, c[0] - 2, kViewH - 1) == 0);
}

TEST_CASE("The eraser picks the newest annotation under the pointer", "[charttools]") {
  Rig rig;
  const geom::WorldXZ m = middle(rig.world);
  rig.look(m, 8);
  const std::int32_t px = rig.view.screen_to_world(1, 0)[0] - rig.view.screen_to_world(0, 0)[0];
  ChartAnnotations a;
  a.add(line({{m.x - 100 * px, m.z}, {m.x + 100 * px, m.z}}));                     // 0
  a.add(mark({m.x + 50 * px, m.z}));                                                // 1
  a.add({.kind = Kind::kCircle, .ink = ChartInk::kWhite, .symbol = ChartSymbol::kCross,
         .points = {{m.x, m.z + 100 * px}, {m.x + 40 * px, m.z + 100 * px}}, .text = {}});  // 2
  a.add({.kind = Kind::kNote, .ink = ChartInk::kBlack, .symbol = ChartSymbol::kCross,
         .points = {{m.x - 150 * px, m.z - 60 * px}}, .text = "NOTE"});              // 3
  const auto pick = [&](geom::WorldXZ p, int dx = 0, int dy = 0) {
    const std::array<int, 2> s = rig.at(p);
    return pick_chart_annotation(rig.view, a, rig.font, s[0] + dx, s[1] + dy);
  };
  CHECK(pick(m) == std::optional<std::size_t>(0));
  CHECK(pick(m, 0, 4) == std::optional<std::size_t>(0));
  CHECK(pick(m, 0, 6) == std::nullopt);
  // The mark lies on the line: the newer wins.
  CHECK(pick({m.x + 50 * px, m.z}) == std::optional<std::size_t>(1));
  // A circle by its outline, not its middle.
  CHECK(pick({m.x + 40 * px, m.z + 100 * px}) == std::optional<std::size_t>(2));
  CHECK(pick({m.x, m.z + 100 * px}) == std::nullopt);
  // A note by its point or its text.
  CHECK(pick({m.x - 150 * px, m.z - 60 * px}) == std::optional<std::size_t>(3));
  CHECK(pick({m.x - 150 * px, m.z - 60 * px}, 12, 0) == std::optional<std::size_t>(3));
  CHECK(pick({m.x - 150 * px, m.z - 60 * px}, 40, 0) == std::nullopt);
}

TEST_CASE("The eraser's annotation is framed", "[charttools]") {
  Rig rig;
  const geom::WorldXZ m = middle(rig.world);
  rig.look(m, 8);
  ChartAnnotations a;
  a.add(mark(m));
  const std::array<int, 2> c = rig.at(m);
  const std::uint8_t frame = kInks[ChartInk::kWhite];
  rig.draw(a);
  CHECK(rig.count(frame, 0, 0, kViewW - 1, kViewH - 1) == 0);
  rig.draw(a, 0);
  // A dashed box a few pixels round the mark, in a contrasting ink.
  CHECK(rig.count(frame, c[0] - 8, c[1] - 8, c[0] + 9, c[1] - 6) > 4);
  CHECK(rig.count(frame, c[0] - 4, c[1] - 4, c[0] + 4, c[1] + 4) == 0);
  ChartAnnotations white;
  white.add(mark(m, ChartInk::kWhite));
  rig.draw(white, 0);
  CHECK(rig.count(kInks[ChartInk::kBlack], c[0] - 8, c[1] - 8, c[0] + 9, c[1] - 6) > 4);
}

}  // namespace sh

namespace sh {

TEST_CASE("The nautical scale shows round ranges", "[charttools]") {
  CHECK(chart_nautical_scale(1) == ChartScale{.length = 236, .value = 80, .yards = false});
  CHECK(chart_nautical_scale(2) == ChartScale{.length = 236, .value = 40, .yards = false});
  CHECK(chart_nautical_scale(8) == ChartScale{.length = 236, .value = 10, .yards = false});
  // Below 10 nautical miles, in tens of yards.
  CHECK(chart_nautical_scale(16) == ChartScale{.length = 233, .value = 1000, .yards = true});
  CHECK(chart_nautical_scale(128) == ChartScale{.length = 223, .value = 120, .yards = true});
  CHECK(chart_nautical_scale(kChartMaxZoom) == ChartScale{.length = 238, .value = 8, .yards = true});
}

TEST_CASE("The scale bar has four blocks of five divisions", "[charttools]") {
  Image8 image(300, 8, 6);
  draw_chart_scale_bar(Viewport::full(image), 0, 0, 256);
  // As CHART.SHP's bar: ticks every 12.8 pixels, rounded down.
  std::set<int> ticks;
  for (int k = 0; k <= 20; ++k) {
    ticks.insert(k * 256 / 20);
  }
  for (int x = 0; x <= 256; ++x) {
    INFO("x " << x);
    const bool minor = ticks.contains(x);
    const bool major = x % 64 == 0;
    CHECK((image.at(x, 0) == 2) == major);
    CHECK((image.at(x, 1) == 2) == (minor || major));
    if (!minor && !major) {
      const bool even = x / 64 % 2 == 0;
      CHECK(image.at(x, 2) == (even ? 3 : 2));
      CHECK(image.at(x, 5) == (even ? 2 : 3));
    }
  }
  CHECK(image.at(257, 3) == 6);
}

}  // namespace sh
