#include "game/stations/panel_ends.h"

#include <cstdint>

namespace sh {

namespace {

// Returns `v` modulo `n` (n > 0) in 0..n-1.
int wrap(int v, int n) {
  const int m = v % n;
  return m < 0 ? m + n : m;
}

}  // namespace

PanelEnds make_panel_ends(const Image8& panel, const Image8& plate, int left_width, int right_width,
                          const PanelEndsLayout& layout) {
  const int rows = panel.height();
  // Whether panel pixel (`x`, `y`) is on the mount of the edge gauge
  // centred at `cx2 / 2`, in squared half pixels.
  const auto on_mount = [&](int x, int y, int cx2) {
    const auto within = [&](int dx2, int dy, int radius) {
      return dx2 * dx2 + 4 * dy * dy <= 4 * radius * radius;
    };
    const int dx2 = 2 * x - cx2;
    const int bolt_dx2 = layout.bolt_dx2 * (dx2 < 0 ? -1 : 1);
    return within(dx2, y - layout.centre_y, layout.mount_radius) ||
           within(dx2 - bolt_dx2, y - layout.bolt_top, layout.lug_radius) ||
           within(dx2 - bolt_dx2, y - layout.bolt_bottom, layout.lug_radius);
  };
  // The pixel at panel column `x` (outside 0..639) and row `y`.
  const auto pixel = [&](int x, int y) -> std::uint8_t {
    const bool left = x < 0;
    if (y < layout.band_rows) {
      return panel.at(left ? wrap(x, 2) : kScreenWidth - 2 + wrap(x, 2), y);
    }
    const int cx2 = layout.centre_x2 + (left ? 0 : 2 * layout.gauge_shift);
    if (on_mount(x, y, cx2)) {
      return panel.at(left ? x + layout.gauge_shift : x - layout.gauge_shift, y);
    }
    // The plate section's grid is aligned on the panel's, so that its
    // dither keeps the panel's phase.
    const ui::Rect& r = left ? layout.left_plate : layout.right_plate;
    return plate.at(r.x0 + wrap(x - r.x0, r.x1 - r.x0 + 1), r.y0 + wrap(y - r.y0, r.y1 - r.y0 + 1));
  };
  PanelEnds ends{Image8(left_width, rows), Image8(right_width, rows)};
  for (int y = 0; y < rows; ++y) {
    for (int i = 0; i < left_width; ++i) {
      ends.left.set(left_width - 1 - i, y, pixel(-1 - i, y));
    }
    for (int i = 0; i < right_width; ++i) {
      ends.right.set(i, y, pixel(kScreenWidth + i, y));
    }
  }
  return ends;
}

}  // namespace sh
