#include "view/polygon.h"

#include <algorithm>
#include <cstddef>

#include "core/fixed_math.h"

namespace sh {
namespace {

// The clip rectangle of a viewport as the rasterizers compute it: the
// origin clamped to 0 (a negative x0/y0 counts as 0) and the extent.
struct Clip {
  std::int32_t x0 = 0;
  std::int32_t y0 = 0;
  std::int32_t max_x = -1;  // last column, relative to x0
  std::int32_t max_y = -1;  // last row, relative to y0
};

bool make_clip(const Viewport& vp, Clip& clip) {
  if (!vp.valid()) {
    return false;
  }
  clip.x0 = std::max(vp.x0, 0);
  clip.y0 = std::max(vp.y0, 0);
  clip.max_x = std::min(vp.x1, vp.bitmap->width() - 1) - clip.x0;
  clip.max_y = std::min(vp.y1, vp.bitmap->height() - 1) - clip.y0;
  return clip.max_x >= 0 && clip.max_y >= 0;
}

// (a << 16) / dy with a 16-bit `a`, as the original's x slopes.
std::int32_t x_slope(std::int32_t a, std::int32_t dy) {
  return static_cast<std::int32_t>((std::int64_t{static_cast<std::int16_t>(a)} << 16) / dy);
}

// (a << 16) / (dy << 16) on 64 bits, as the u/v slopes.
std::int32_t uv_slope(std::int32_t a, std::int32_t dy) {
  return static_cast<std::int32_t>((std::int64_t{a} << 16) / (std::int64_t{dy} << 16));
}

// One polygon edge being walked: from vertex `cur` toward `next`.
struct Edge {
  std::size_t cur = 0;
  std::size_t next = 0;
  std::int32_t rem = 0;  // rows left on this edge
  std::int32_t x = 0;    // 16.16
  std::int32_t u = 0;
  std::int32_t v = 0;
  std::int32_t dx = 0;
  std::int32_t du = 0;
  std::int32_t dv = 0;
};

class Walker {
 public:
  Walker(std::span<const PolyVertex> v, bool left) : v_(v), left_(left) {}

  std::size_t step(std::size_t i) const {
    if (left_) {
      return i == 0 ? v_.size() - 1 : i - 1;
    }
    return i + 1 >= v_.size() ? 0 : i + 1;
  }

  // Sets up the first edge from the top vertex, skipping edges wholly above
  // row 0 and horizontal ones.
  void first(Edge& e, std::size_t top) const {
    std::size_t cur = top;
    std::size_t next = step(cur);
    while ((v_[cur].y < 0 && v_[next].y <= 0) || v_[next].y == v_[cur].y) {
      cur = next;
      next = step(cur);
    }
    load(e, cur, next, fx::wsub(v_[next].y, v_[cur].y));
  }

  // Moves to the edge after the current one (a horizontal edge takes a row).
  void advance(Edge& e) const {
    const std::size_t cur = e.next;
    const std::size_t next = step(cur);
    const std::int32_t dy = fx::wsub(v_[next].y, v_[cur].y);
    load(e, cur, next, dy == 0 ? 1 : dy);
  }

 private:
  void load(Edge& e, std::size_t cur, std::size_t next, std::int32_t dy) const {
    const PolyVertex& a = v_[cur];
    const PolyVertex& b = v_[next];
    e.cur = cur;
    e.next = next;
    e.rem = dy;
    e.dx = x_slope(fx::wsub(b.x, a.x), dy);
    e.du = uv_slope(fx::wsub(b.u, a.u), dy);
    e.dv = uv_slope(fx::wsub(b.v, a.v), dy);
    e.x = fx::wadd(fx::imul(a.x, 0x10000), 0x8000);
    e.u = fx::wadd(a.u, 0x8000);
    e.v = fx::wadd(a.v, 0x8000);
  }

  std::span<const PolyVertex> v_;
  bool left_;
};

void step_edge(Edge& e) {
  e.x = fx::wadd(e.x, e.dx);
  e.u = fx::wadd(e.u, e.du);
  e.v = fx::wadd(e.v, e.dv);
}

// Scan-converts `v` and calls `span(row, a, b)` for each visible row with
// the edges ordered left (a) and right (b) as the original orders them.
template <typename SpanFn>
void scan_polygon(const Clip& clip, std::span<const PolyVertex> v, SpanFn span) {
  if (v.size() < 3) {
    return;
  }
  std::int32_t ymin = 0x7fff;
  std::int32_t ymax = -0x8000;
  std::size_t top = 0;
  unsigned and_code = 0xf;
  for (std::size_t i = 0; i < v.size(); ++i) {
    const PolyVertex& p = v[i];
    if (p.y <= ymin) {
      ymin = p.y;
      top = i;
    }
    if (p.y >= ymax) {
      ymax = p.y;
    }
    const unsigned code = (p.x < 0 ? 8u : 0u) | (clip.max_x - p.x < 0 ? 4u : 0u) | (p.y < 0 ? 2u : 0u) |
                          (clip.max_y - p.y < 0 ? 1u : 0u);
    and_code &= code;
  }
  if (and_code != 0 || ymin == ymax) {
    return;
  }
  const Walker left_walker(v, true);
  const Walker right_walker(v, false);
  Edge left;
  Edge right;
  left_walker.first(left, top);
  right_walker.first(right, top);

  std::int32_t count = clip.max_y - ymin;
  if (ymax <= clip.max_y) {
    count += ymax - clip.max_y;
  }
  std::int32_t row = ymin;
  if (ymin < 0) {
    count += ymin;
    row = 0;
    for (Edge* e : {&left, &right}) {
      const std::int32_t y = v[e->cur].y;
      e->rem = fx::wadd(e->rem, y);
      const std::int32_t skip = fx::imul(-y, 0x10000);
      e->x = fx::wadd(e->x, fx::mul_floor(e->dx, skip));
      e->u = fx::wadd(e->u, fx::mul_floor(e->du, skip));
      e->v = fx::wadd(e->v, fx::mul_floor(e->dv, skip));
    }
  }
  for (;;) {
    if (right.x <= left.x) {
      span(row, right, left);
    } else {
      span(row, left, right);
    }
    ++row;
    if (--count < 0) {
      return;
    }
    if (count == 0) {
      step_edge(left);
      step_edge(right);
      continue;
    }
    if (--left.rem == 0) {
      left_walker.advance(left);
    } else {
      step_edge(left);
    }
    if (--right.rem == 0) {
      right_walker.advance(right);
    } else {
      step_edge(right);
    }
  }
}

}  // namespace

void fill_polygon(const Viewport& vp, std::span<const PolyVertex> v) {
  Clip clip;
  if (v.empty() || !make_clip(vp, clip)) {
    return;
  }
  const auto colour = static_cast<std::uint8_t>((v[0].colour + 0x8000) >> 16);
  Image8& bmp = *vp.bitmap;
  scan_polygon(clip, v, [&](std::int32_t row, const Edge& a, const Edge& b) {
    std::int32_t xa = a.x >> 16;
    std::int32_t xb = b.x >> 16;
    if (xa > clip.max_x || xb < 0) {
      return;
    }
    xa = std::max(xa, 0);
    xb = std::min(xb, clip.max_x);
    auto line = bmp.row(clip.y0 + row);
    std::fill(line.begin() + clip.x0 + xa, line.begin() + clip.x0 + xb + 1, colour);
  });
}

void remap_polygon(const Viewport& vp, std::span<const PolyVertex> v, const std::array<std::uint8_t, 256>& table) {
  Clip clip;
  if (v.empty() || !make_clip(vp, clip)) {
    return;
  }
  Image8& bmp = *vp.bitmap;
  scan_polygon(clip, v, [&](std::int32_t row, const Edge& a, const Edge& b) {
    std::int32_t xa = a.x >> 16;
    std::int32_t xb = b.x >> 16;
    if (xa > clip.max_x || xb < 0) {
      return;
    }
    xa = std::max(xa, 0);
    xb = std::min(xb, clip.max_x);
    auto line = bmp.row(clip.y0 + row);
    for (std::int32_t x = xa; x <= xb; ++x) {
      auto& p = line[static_cast<std::size_t>(clip.x0 + x)];
      p = table[p];
    }
  });
}

void texture_polygon_shaded(const Viewport& vp, std::span<const PolyVertex> v, const Image8& texture,
                            const std::array<std::uint8_t, 256>& shade) {
  Clip clip;
  if (v.empty() || !make_clip(vp, clip) || texture.width() <= 0 || texture.height() <= 0) {
    return;
  }
  Image8& bmp = *vp.bitmap;
  const auto texels = texture.pixels();
  const auto size = static_cast<std::int64_t>(texels.size());
  const std::int64_t pitch = texture.width();
  scan_polygon(clip, v, [&](std::int32_t row, const Edge& a, const Edge& b) {
    std::int32_t xa = a.x >> 16;
    const std::int32_t xb = b.x >> 16;
    if (xa > clip.max_x || xb < 0) {
      return;
    }
    std::int32_t u = a.u;
    std::int32_t w = a.v;
    std::int32_t du = 0;
    std::int32_t dv = 0;
    std::int32_t end = xb;
    const std::int32_t n = xb - xa;
    if (n != 0) {
      du = uv_slope(fx::wsub(b.u, a.u), n);
      dv = uv_slope(fx::wsub(b.v, a.v), n);
      if (xa < 0) {
        u = fx::wadd(u, fx::mul_floor(du, fx::imul(-xa, 0x10000)));
        w = fx::wadd(w, fx::mul_floor(dv, fx::imul(-xa, 0x10000)));
        xa = 0;
      }
      end = std::min(end, clip.max_x);
    }
    auto line = bmp.row(clip.y0 + row);
    for (std::int32_t x = xa; x <= end; ++x) {
      std::int64_t at = std::int64_t{w >> 16} * pitch + (u >> 16);
      at %= size;
      if (at < 0) {
        at += size;
      }
      line[static_cast<std::size_t>(clip.x0 + x)] = shade[texels[static_cast<std::size_t>(at)]];
      u = fx::wadd(u, du);
      w = fx::wadd(w, dv);
    }
  });
}

}  // namespace sh
