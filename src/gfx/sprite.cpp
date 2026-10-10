#include "gfx/sprite.h"

#include <stdexcept>

#include "core/fixed_math.h"

namespace sh {

namespace {

// Cohen-Sutherland outcode of (x, y) against `c`, bits as the original's.
unsigned outcode(std::int64_t x, std::int64_t y, const ClipRect& c) {
  return (x < c.left ? 8u : 0u) | (x > c.right ? 4u : 0u) | (y < c.top ? 2u : 0u) | (y > c.bottom ? 1u : 0u);
}

// The RLE blit shared by draw_sprite_clipped (0xaaa9c) and
// sprite_draw_remapped (0xaaf86): `map(c)` gives the drawn colour.
template <typename Map>
DrawResult blit(const Viewport& vp, const SpriteFrame& frame, int x, int y, Map map) {
  if (!vp.valid()) {
    return DrawResult::kBadBitmap;
  }
  const ClipRect clip = vp.clip();
  if (clip.empty()) {
    return DrawResult::kEmptyClip;
  }
  if (frame.empty()) {
    return DrawResult::kEmptyFrame;
  }
  const std::int64_t bx0 = std::int64_t{frame.x0} + x + vp.x0;
  const std::int64_t by0 = std::int64_t{frame.y0} + y + vp.y0;
  const std::int64_t bx1 = std::int64_t{frame.x1} + x + vp.x0;
  const std::int64_t by1 = std::int64_t{frame.y1} + y + vp.y0;
  if ((outcode(bx0, by0, clip) & outcode(bx1, by1, clip)) != 0) {
    return DrawResult::kOutside;
  }
  Image8& dst = *vp.bitmap;
  const std::vector<std::uint8_t>& rle = frame.rle;
  std::size_t p = 0;
  for (std::int64_t py = by0; py <= by1 && py <= clip.bottom; ++py) {
    const bool row_visible = py >= clip.top;
    std::int64_t px = bx0;
    for (;;) {
      const std::uint8_t b = rle[p++];
      const int n = b >> 1;
      if (n == 0) {
        if ((b & 1) == 0) {
          break;
        }
        px += rle[p++];
        continue;
      }
      const bool literal = (b & 1) != 0;
      if (row_visible) {
        for (int i = 0; i < n; ++i) {
          const std::int64_t xx = px + i;
          if (xx >= clip.left && xx <= clip.right) {
            dst.set(static_cast<int>(xx), static_cast<int>(py),
                    map(rle[literal ? p + static_cast<std::size_t>(i) : p]));
          }
        }
      }
      p += literal ? static_cast<std::size_t>(n) : 1;
      px += n;
    }
  }
  return DrawResult::kOk;
}

// A corner of the textured quad: screen position and texel.
struct Vertex {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t u = 0;
  std::int32_t v = 0;
};

// (a << 16) / dy with a 16-bit `a`, as the original's edge slopes.
std::int32_t edge_slope(std::int32_t a, std::int32_t dy) {
  return static_cast<std::int32_t>((std::int64_t{static_cast<std::int16_t>(a)} << 16) / dy);
}

std::int32_t start16(std::int32_t v) { return static_cast<std::int32_t>((static_cast<std::uint32_t>(v) << 16) + 0x8000u); }

// One polygon edge being walked, from vertex `from` towards `to`.
struct Edge {
  int to = 0;
  std::int32_t remaining = 0;  // rows left before switching edges
  std::int32_t x = 0, u = 0, v = 0;     // 16.16
  std::int32_t dx = 0, du = 0, dv = 0;  // per row

  void start(const std::array<Vertex, 4>& q, int from, int next, std::int32_t dy) {
    to = next;
    remaining = dy;
    dx = edge_slope(q[next].x - q[from].x, dy);
    du = edge_slope(q[next].u - q[from].u, dy);
    dv = edge_slope(q[next].v - q[from].v, dy);
    x = start16(q[from].x);
    u = start16(q[from].u);
    v = start16(q[from].v);
  }
  void step() {
    x += dx;
    u += du;
    v += dv;
  }
};

// The textured-quad rasterizer of sprite_draw_transformed (0xab591): maps
// `tex` (0xff transparent) onto the quad `q` (v0..v3 in order), clipped.
void draw_textured_quad(Image8& dst, const ClipRect& clip, const std::array<Vertex, 4>& q, const Image8& tex) {
  std::int32_t ymin = 0x7fff;
  std::int32_t ymax = -0x8000;
  int top = 0;
  unsigned outside = 0xf;
  for (int i = 0; i < 4; ++i) {
    if (q[i].y <= ymin) {
      ymin = q[i].y;
      top = i;
    }
    if (ymax <= q[i].y) {
      ymax = q[i].y;
    }
    outside &= outcode(q[i].x, q[i].y, clip);
  }
  if (outside != 0 || ymax == ymin) {
    return;
  }
  // Finds the first edge from `top` in direction `dir` that is not flat and
  // not wholly above the clip rectangle; returns its start vertex.
  auto first_edge = [&](Edge& e, int dir) {
    int from = top;
    for (int guard = 0; guard < 4; ++guard) {
      const int next = (from + dir + 4) % 4;
      const std::int32_t dy = q[next].y - q[from].y;
      if (!((q[from].y < clip.top && q[next].y <= clip.top) || dy == 0)) {
        e.start(q, from, next, dy);
        return from;
      }
      from = next;
    }
    return -1;
  };
  Edge a;  // walks v[i-1] (the original's first chain)
  Edge b;  // walks v[i+1]
  const int a_from = first_edge(a, -1);
  const int b_from = first_edge(b, +1);
  if (a_from < 0 || b_from < 0) {
    return;
  }
  std::int32_t rows = clip.bottom - ymin;
  if (ymax <= clip.bottom) {
    rows += ymax - clip.bottom;
  }
  if (ymin < clip.top) {
    rows -= clip.top - ymin;
    auto advance = [&](Edge& e, int from) {
      const std::int32_t k = clip.top - q[from].y;
      e.remaining -= k;
      e.x += fx::mul_floor(e.dx, k << 16);
      e.u += fx::mul_floor(e.du, k << 16);
      e.v += fx::mul_floor(e.dv, k << 16);
    };
    advance(a, a_from);
    advance(b, b_from);
    ymin = clip.top;
  }
  const std::int64_t tex_w = tex.width();
  const std::int64_t tex_h = tex.height();
  for (std::int32_t y = ymin;; ++y) {
    const Edge& l = b.x <= a.x ? b : a;
    const Edge& r = b.x <= a.x ? a : b;
    std::int32_t xs = l.x >> 16;
    std::int32_t xe = r.x >> 16;
    if (xs <= clip.right && xe >= clip.left) {
      std::int32_t us = l.u;
      std::int32_t vs = l.v;
      std::int32_t du = 0;
      std::int32_t dv = 0;
      if (xe != xs) {
        const std::int64_t n = std::int64_t{static_cast<std::int32_t>(static_cast<std::uint32_t>(xe - xs) << 16)};
        du = static_cast<std::int32_t>((std::int64_t{r.u - l.u} << 16) / n);
        dv = static_cast<std::int32_t>((std::int64_t{r.v - l.v} << 16) / n);
        if (xs < clip.left) {
          const std::int32_t k = clip.left - xs;
          xs = clip.left;
          us += fx::mul_floor(du, k << 16);
          vs += fx::mul_floor(dv, k << 16);
        }
        if (xe > clip.right) {
          xe = clip.right;
        }
      }
      // The original steps a texel pointer by the integer parts plus the
      // carries of the fractions: the same as flooring u and v here.
      std::int64_t u = static_cast<std::uint32_t>(us);
      std::int64_t v = static_cast<std::uint32_t>(vs);
      auto row = dst.row(y);
      for (std::int32_t x = xs; x <= xe; ++x, u += du, v += dv) {
        const std::int64_t tu = u >> 16;
        const std::int64_t tv = v >> 16;
        // Texels outside the scratch read other memory in the original.
        if (tu < 0 || tv < 0 || tu >= tex_w || tv >= tex_h) {
          continue;
        }
        const std::uint8_t c = tex.at(static_cast<int>(tu), static_cast<int>(tv));
        if (c != 0xff) {
          row[static_cast<std::size_t>(x)] = c;
        }
      }
    }
    if (--rows < 0) {
      return;
    }
    if (rows == 0) {
      a.step();
      b.step();
      continue;
    }
    auto walk = [&](Edge& e, int dir) {
      if (--e.remaining == 0) {
        const int from = e.to;
        const int next = (from + dir + 4) % 4;
        const std::int32_t dy = q[next].y - q[from].y;
        e.start(q, from, next, dy == 0 ? 1 : dy);
      } else {
        e.step();
      }
    };
    walk(a, -1);
    walk(b, +1);
  }
}

}  // namespace

DrawResult draw_sprite_clipped(const Viewport& vp, const SpriteFrame& frame, int x, int y) {
  return blit(vp, frame, x, y, [](std::uint8_t c) { return c; });
}

DrawResult draw_sprite_remapped(const Viewport& vp, const SpriteFrame& frame, int x, int y,
                                const ColourRemap& remap) {
  return blit(vp, frame, x, y, [&remap](std::uint8_t c) { return remap[c]; });
}

DrawResult sprite_draw_transformed(const Viewport& vp, const SpriteFrame& frame, int x, int y, Image8& scratch,
                                   Angle10 angle, Fixed16 scale_x, Fixed16 scale_y, unsigned flags,
                                   const ColourRemap* remap) {
  if ((flags & kSpriteRemap) != 0 && remap == nullptr) {
    throw std::invalid_argument("sprite_draw_transformed: kSpriteRemap without a remap table");
  }
  const Fixed16 one = Fixed16::from_int(1);
  if (scale_x == one && scale_y == one && angle.tenths() == 0) {
    return (flags & kSpriteRemap) != 0 ? draw_sprite_remapped(vp, frame, x, y, *remap)
                                       : draw_sprite_clipped(vp, frame, x, y);
  }
  if (frame.empty()) {
    return DrawResult::kEmptyFrame;
  }
  const int w = frame.width();
  const int h = frame.height();
  if ((flags & kSpriteScratchReady) == 0) {
    scratch = Image8(w, h, 0xff);
    const Viewport svp = Viewport::full(scratch);
    if ((flags & kSpriteRemap) != 0) {
      draw_sprite_remapped(svp, frame, -frame.x0, -frame.y0, *remap);
    } else {
      draw_sprite_clipped(svp, frame, -frame.x0, -frame.y0);
    }
  } else if (scratch.width() != w || scratch.height() != h) {
    throw std::invalid_argument("sprite_draw_transformed: scratch does not hold this frame");
  }
  if (!vp.valid()) {
    return DrawResult::kBadBitmap;
  }
  const ClipRect clip = vp.clip();
  if (clip.empty()) {
    return DrawResult::kEmptyClip;
  }
  // The corners of the frame box rotate about the hotspot, which lies at
  // (-x0, -y0) in the scratch.
  const fx::Point2 centre{-frame.x0, -frame.y0};
  const std::int32_t ox = x + frame.x0 + vp.x0;
  const std::int32_t oy = y + frame.y0 + vp.y0;
  std::array<Vertex, 4> quad;
  const std::array<fx::Point2, 4> tex{{{0, 0}, {w - 1, 0}, {w - 1, h - 1}, {0, h - 1}}};
  for (std::size_t i = 0; i < 4; ++i) {
    const fx::Point2 p = fx::point_rotate_scale(tex[i], centre, angle, scale_x.raw(), scale_y.raw());
    quad[i] = {p.x + ox, p.y + oy, tex[i].x, tex[i].y};
  }
  draw_textured_quad(*vp.bitmap, clip, quad, scratch);
  return DrawResult::kOk;
}

void sprite_apply_palette(const SpriteFrame& frame, VgaPalette& palette) {
  for (const ShpPaletteEntry& e : frame.palette) {
    const std::size_t i = std::size_t{e.index} * 3;
    palette[i] = e.r;
    palette[i + 1] = e.g;
    palette[i + 2] = e.b;
  }
}

}  // namespace sh
