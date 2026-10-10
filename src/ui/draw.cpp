#include "ui/draw.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace sh::ui {

namespace {

// Calls `pixel(x, y)` (viewport-relative) for each point of the line, in
// order, as `gfx_draw_line_clipped` steps it.
template <class Pixel>
void walk_line(int x0, int y0, int x1, int y1, Pixel pixel) {
  const int dx = x1 - x0;
  const int dy = y1 - y0;
  const int adx = std::abs(dx);
  const int ady = std::abs(dy);
  const int sx = dx < 0 ? -1 : 1;
  const int sy = dy < 0 ? -1 : 1;
  if (dx == 0 || dy == 0) {
    const int n = std::max(adx, ady);
    for (int i = 0; i <= n; ++i) {
      pixel(x0 + (dx == 0 ? 0 : sx * i), y0 + (dy == 0 ? 0 : sy * i));
    }
    return;
  }
  // The minor axis advances on each carry of a 32-bit accumulator that
  // starts at one half and adds minor / major as a 32.32 fraction.
  const std::uint64_t major = static_cast<std::uint64_t>(std::max(adx, ady));
  const std::uint64_t minor = static_cast<std::uint64_t>(std::min(adx, ady));
  const std::uint64_t slope = adx == ady ? 0xffffffffu : (minor << 32) / major;
  for (std::uint64_t i = 0; i <= major; ++i) {
    const int m = static_cast<int>((i * slope + 0x80000000u) >> 32);
    const int k = static_cast<int>(i);
    if (adx >= ady) {
      pixel(x0 + sx * k, y0 + sy * m);
    } else {
      pixel(x0 + sx * m, y0 + sy * k);
    }
  }
}

// Fills bitmap row `row` from l to r (viewport-relative), clipped.
void span(const Viewport& vp, const ClipRect& clip, int row, int l, int r, std::uint8_t colour) {
  row += vp.y0;
  l += vp.x0;
  r += vp.x0;
  if (row < clip.top || row > clip.bottom || r < clip.left || l > clip.right) {
    return;
  }
  l = std::max(l, clip.left);
  r = std::min(r, clip.right);
  for (int x = l; x <= r; ++x) {
    vp.bitmap->set(x, row, colour);
  }
}

}  // namespace

void put_pixel(const Viewport& vp, int x, int y, std::uint8_t colour) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  const int bx = vp.x0 + x;
  const int by = vp.y0 + y;
  if (clip.contains(bx, by)) {
    vp.bitmap->set(bx, by, colour);
  }
}

void draw_line(const Viewport& vp, int x0, int y0, int x1, int y1, std::uint8_t colour) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  walk_line(x0, y0, x1, y1, [&](int x, int y) {
    if (clip.contains(vp.x0 + x, vp.y0 + y)) {
      vp.bitmap->set(vp.x0 + x, vp.y0 + y, colour);
    }
  });
}

void draw_line(const Viewport& vp, int x0, int y0, int x1, int y1, const std::function<void(int, int)>& plot) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  walk_line(x0, y0, x1, y1, [&](int x, int y) {
    if (clip.contains(vp.x0 + x, vp.y0 + y)) {
      plot(x, y);
    }
  });
}

void fill_rect(const Viewport& vp, int x0, int y0, int x1, int y1, std::uint8_t colour) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) {
    span(vp, clip, y, std::min(x0, x1), std::max(x0, x1), colour);
  }
}

void fill_triangle(const Viewport& vp, Point a, Point b, Point c, std::uint8_t colour) {
  if (!vp.valid()) {
    return;
  }
  const ClipRect clip = vp.clip();
  std::array<Point, 3> p{a, b, c};
  std::sort(p.begin(), p.end(), [](const Point& l, const Point& r) { return l.y < r.y; });
  // The x of edge (u, v) at row y, in 16.16 stepped from u.
  const auto edge_x = [](const Point& u, const Point& v, int y) {
    if (v.y == u.y) {
      return u.x;
    }
    const std::int64_t step = (static_cast<std::int64_t>(v.x - u.x) << 16) / (v.y - u.y);
    return static_cast<int>(((static_cast<std::int64_t>(u.x) << 16) + step * (y - u.y) + 0x8000) >> 16);
  };
  for (int y = p[0].y; y <= p[2].y; ++y) {
    const int xl = edge_x(p[0], p[2], y);
    const int xr = y < p[1].y ? edge_x(p[0], p[1], y) : edge_x(p[1], p[2], y);
    span(vp, clip, y, std::min(xl, xr), std::max(xl, xr), colour);
  }
}

void fill_ellipse(const Viewport& vp, int cx, int cy, int rx, int ry, std::uint8_t colour) {
  // gfx_fill_ellipse (0xad2e9)
  if (!vp.valid()) {
    return;
  }
  if (rx == 0 || ry == 0) {
    draw_line(vp, cx - rx, cy - ry, cx + rx, cy + ry, colour);
    return;
  }
  const ClipRect clip = vp.clip();
  const std::int64_t a2 = static_cast<std::int64_t>(rx) * rx;
  const std::int64_t b2 = static_cast<std::int64_t>(ry) * ry;
  std::int64_t x = 0;
  std::int64_t y = ry;
  std::int64_t dx = 0;
  std::int64_t dy = 2 * a2 * ry;
  std::int64_t d = (a2 >> 2) + b2 - a2 * ry;
  const auto spans = [&]() {
    span(vp, clip, static_cast<int>(cy + y), static_cast<int>(cx - x), static_cast<int>(cx + x), colour);
    span(vp, clip, static_cast<int>(cy - y), static_cast<int>(cx - x), static_cast<int>(cx + x), colour);
  };
  while (dx - dy < 0) {
    spans();
    if (d >= 0) {
      --y;
      dy -= 2 * a2;
      d -= dy;
    }
    ++x;
    dx += 2 * b2;
    d += dx + b2;
  }
  d += (((a2 - b2) >> 1) + (a2 - b2) - dx - dy) >> 1;
  do {
    spans();
    if (d < 0) {
      ++x;
      dx += 2 * b2;
      d += dx;
    }
    --y;
    dy -= 2 * a2;
    d -= dy - a2;
  } while (y >= 0);
}

void gui_fill_rect(const Viewport& vp, int x, int y, int w, int h, std::uint8_t colour) {
  fill_rect(vp, x, y, x + w - 1, y + h - 1, colour);
}

void gui_raised_box(const Viewport& vp, int x, int y, int w, int h, std::uint8_t fill, std::uint8_t left,
                    std::uint8_t right) {
  const int x1 = x + w - 1;
  const int y1 = y + h - 1;
  fill_rect(vp, x, y, x1, y1, fill);
  draw_line(vp, x, y, x, y1, left);
  draw_line(vp, x + 1, y + 1, x + 1, y1 - 1, left);
  draw_line(vp, x1, y, x1, y1, right);
  draw_line(vp, x1 - 1, y + 1, x1 - 1, y1 - 1, right);
  draw_line(vp, x, y, x1, y, left);
  draw_line(vp, x + 1, y + 1, x1 - 1, y + 1, left);
  draw_line(vp, x, y1, x1, y1, right);
  draw_line(vp, x + 1, y1 - 1, x1 - 1, y1 - 1, right);
}

void gui_focus_rect(const Viewport& vp, int x, int y, int w, int h, std::uint8_t colour, bool dotted) {
  const int x1 = x + w - 1;
  const int y1 = y + h - 1;
  if (!dotted) {
    draw_line(vp, x, y, x1, y, colour);
    draw_line(vp, x1, y, x1, y1, colour);
    draw_line(vp, x1, y1, x, y1, colour);
    draw_line(vp, x, y1, x, y, colour);
    return;
  }
  // gui_dotted_plot (0x42d59): one toggle across the four edges.
  bool skip = false;
  const std::function<void(int, int)> plot = [&](int px, int py) {
    if (!skip) {
      put_pixel(vp, px, py, colour);
    }
    skip = !skip;
  };
  draw_line(vp, x, y, x1, y, plot);
  draw_line(vp, x1, y, x1, y1, plot);
  draw_line(vp, x1, y1, x, y1, plot);
  draw_line(vp, x, y1, x, y, plot);
}

}  // namespace sh::ui
