#include "game/stations/chart_view.h"

#include <algorithm>

#include "core/error.h"
#include "core/fixed_math.h"
#include "gfx/sprite.h"

namespace sh {

namespace {

using fx::div;
using fx::mul;

}  // namespace

ChartView::ChartView(const World& world, int x0, int y0, int x1, int y1)
    : world_(world),
      x0_(x0),
      y0_(y0),
      x1_(x1),
      y1_(y1),
      // world_load_awld_chart's declared width (0x16e178): one less than
      // the rows', whose last column is never shown.
      map_w_(world.chart.width - 1),
      map_h_(world.chart.height) {
  if (map_w_ <= 0 || map_h_ <= 0 || world.grid.width < 2 || world.grid.height < 2) {
    throw DataError("the map has no chart");
  }
  if (!world.coast_tiles_shp.empty()) {
    const Shp tiles = Shp::parse(world.coast_tiles_shp, "WLD coast tiles");
    if (tiles.frame_count() >= 16) {
      tiles_.assign(tiles.frames().begin(), tiles.frames().begin() + 16);
    }
  }
  set_zoom(0x10000);
  set_center(0, 0);
}

void ChartView::set_extent(int x1, int y1) {
  centre_narrow_ = true;
  x1_ = x1;
  y1_ = y1;
  set_zoom(zoom_);
}

std::int32_t ChartView::zoom_int() const { return fx::trunc(zoom_); }

void ChartView::set_zoom(std::int32_t zoom) {
  if (zoom <= 0xffff) {
    return;
  }
  zoom_ = zoom;
  half_w_ = div(mul((x1_ - x0_ + 1) * 0x10000, scale_), zoom) >> 1;
  half_h_ = div(mul((y1_ - y0_ + 1) * 0x10000, scale_), zoom) >> 1;
  step_ = div(scale_, zoom);
  set_center(cx_, cy_);
}

void ChartView::set_center(std::int32_t x, std::int32_t y) {
  if (x - half_w_ < 0) {
    x = half_w_;
  }
  if (y - half_h_ < 0) {
    y = half_h_;
  }
  if (map_w_ * 0x10000 < half_w_ + x) {
    x = map_w_ * 0x10000 - half_w_;
  }
  if (centre_narrow_ && map_w_ * 0x10000 < 2 * half_w_) {
    x = map_w_ * 0x10000 / 2;
  }
  if (map_h_ * 0x10000 < half_h_ + y) {
    y = map_h_ * 0x10000 - half_h_;
  }
  cx_ = x;
  cy_ = y;
}

ChartPoint ChartView::world_to_screen(std::int32_t x, std::int32_t z) const {
  const std::int32_t kx = div(map_w_ * 0x10000, (world_.grid.width - 1) * 0x10000);
  const std::int32_t ky = div(map_h_ * 0x10000, (world_.grid.height - 1) * 0x10000);
  const std::int32_t cell = world_.cell_size();
  const std::int32_t px = div(mul(x, kx), cell);
  const std::int32_t py = div(mul(z, ky), cell);
  ChartPoint p;
  p.x16 = div(px - (cx_ - half_w_), step_);
  p.y16 = div((map_h_ * 0x10000 - py) - (cy_ - half_h_), step_);
  p.x = fx::trunc(p.x16);
  p.y = fx::trunc(p.y16);
  return p;
}

std::array<std::int32_t, 2> ChartView::world_to_chart(std::int32_t x, std::int32_t z) const {
  const ChartPoint p = world_to_screen(x, z);
  return {(cx_ - half_w_) + mul(p.x16 - x0_ * 0x10000, step_), (cy_ - half_h_) + mul(p.y16 - y0_ * 0x10000, step_)};
}

std::array<std::int32_t, 2> ChartView::screen_to_chart(std::int32_t x, std::int32_t y) const {
  return {(cx_ - half_w_) + mul((x - x0_) * 0x10000, step_), (cy_ - half_h_) + mul((y - y0_) * 0x10000, step_)};
}

std::array<std::int32_t, 2> ChartView::screen_to_world(std::int32_t x, std::int32_t y) const {
  const std::int32_t kx = div((world_.grid.width - 1) * 0x10000, map_w_ * 0x10000);
  const std::int32_t ky = div((world_.grid.height - 1) * 0x10000, map_h_ * 0x10000);
  const std::int32_t cell = world_.cell_size();
  const std::int32_t mx = mul(x * 0x10000, step_) + (cx_ - half_w_);
  const std::int32_t my = map_h_ * 0x10000 - (mul(y * 0x10000, step_) + (cy_ - half_h_));
  return {mul(mul(mx, cell), kx), mul(mul(my, cell), ky)};
}

bool ChartView::point_on_screen(std::int32_t x, std::int32_t z, std::int32_t& sx, std::int32_t& sy) const {
  const ChartPoint p = world_to_screen(x, z);
  sx = p.x;
  sy = p.y;
  return fx::abs(sx) <= 32000 && fx::abs(sy) <= 32000;
}

void ChartView::draw(Image8& screen) {
  if (tiled()) {
    draw_tiles(screen);
  } else {
    draw_scaled(screen);
  }
}

void ChartView::draw_scaled(Image8& screen) const {
  // The scaled copy (0x9775b): nearest chart pixel, stepping in 16.16.
  const auto start_x = static_cast<std::uint32_t>(cx_ - half_w_);
  auto ys = static_cast<std::uint32_t>(cy_ - half_h_);
  const auto step = static_cast<std::uint32_t>(step_);
  for (int y = y0_; y <= y1_; ++y, ys += step) {
    if (y < 0 || y >= screen.height()) {
      continue;
    }
    const auto row = std::min<std::int32_t>(static_cast<std::int32_t>(ys >> 16), map_h_ - 1);
    std::uint32_t xs = start_x;
    for (int x = x0_; x <= x1_; ++x, xs += step) {
      if (x < 0 || x >= screen.width()) {
        continue;
      }
      const auto column = std::min<std::int32_t>(static_cast<std::int32_t>(xs >> 16), map_w_ - 1);
      screen.set(x, y, world_.chart.at(column, row));
    }
  }
}

void ChartView::draw_tiles(Image8& screen) {
  // chart_view_draw_zoomed (0x9555e): per grid cell, the coast tile whose
  // frame is the land mask of its four corners, scaled to the cell.
  const std::int32_t gw = world_.grid.width;
  const std::int32_t gh = world_.grid.height;
  const std::int32_t cell = world_.cell_size();
  const std::int32_t kx = div(map_w_ * 0x10000, (gw - 1) * 0x10000);
  const std::int32_t ky = div(map_h_ * 0x10000, (gh - 1) * 0x10000);
  const std::int32_t cell_px_x = div(mul(cell, kx), step_);
  const std::int32_t cell_px_y = div(mul(cell, ky), step_);
  const std::array<std::int32_t, 2> origin = screen_to_world(0, 0);
  const std::int32_t wx = origin[0];
  const std::int32_t wy = (gh + 1) * cell - origin[1];
  const std::int32_t column0 = wx / cell;
  std::int32_t row = wy / cell;
  const std::int32_t off_x = div(mul(wx & (cell - 1), kx), step_);
  const std::int32_t off_y = div(mul(wy & (cell - 1), ky), step_);
  const std::int32_t frame_w = tiles_[0].width();
  const std::int32_t frame_h = tiles_[0].height();
  std::int32_t sx = div(cell_px_x, frame_w * 0x10000);
  std::int32_t sy = div(cell_px_y, frame_h * 0x10000);
  sx += sx / 128;
  sy += sy / 128;
  const std::int32_t vw = x1_ - x0_ + 1;
  const std::int32_t vh = y1_ - y0_ + 1;
  const Viewport vp{&screen, x0_, y0_, x1_, y1_};
  std::array<bool, 16> ready{};
  // The corners sampled per cell (0x10fd48 / 0x10fd58).
  constexpr std::array<std::int32_t, 4> kDx{0, 0, 1, 1};
  constexpr std::array<std::int32_t, 4> kDy{0, -1, -1, 0};
  const std::int64_t grid_size = static_cast<std::int64_t>(world_.grid.pixels.size());
  for (std::int32_t y16 = -off_y; fx::trunc(y16) < vh; y16 += cell_px_y, ++row) {
    std::int32_t column = column0;
    for (std::int32_t x16 = -off_x; fx::trunc(x16) < vw; x16 += cell_px_x, ++column) {
      std::uint32_t mask = 0;
      for (std::size_t k = 0; k < 4; ++k) {
        const std::int64_t index = static_cast<std::int64_t>(gw) * (row + kDy[k]) + column + kDx[k];
        const std::uint8_t value =
            index >= 0 && index < grid_size ? world_.grid.pixels[static_cast<std::size_t>(index)] : 0;
        if (value >= kCoastCell) {
          mask |= 1u << k;
        }
      }
      sprite_draw_transformed(vp, tiles_[mask], fx::trunc(x16), fx::trunc(y16), scratch_[mask], Angle10(0),
                              Fixed16::from_raw(sx), Fixed16::from_raw(sy),
                              ready[mask] ? kSpriteScratchReady : 0u);
      ready[mask] = true;
    }
  }
}

}  // namespace sh
