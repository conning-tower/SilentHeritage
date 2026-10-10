// The terrain pass of the 3D view (`world_draw_terrain`, 0x92a8c): the
// `.WLD` height grid around the camera as two triangles per cell, clipped
// against the near plane and the 90 deg frustum (`clip_*`, 0x93ea2..0x940af),
// sun-shaded from a slope normal table (`world_build_normal_table`,
// 0x929b8), drawn far to near with the render-list entries of each cell.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/fixed_math.h"
#include "view/draw_context.h"
#include "view/polygon.h"

namespace sh::view3d {
namespace {

constexpr std::int32_t kNearPlane = kNearZ;
constexpr std::int32_t kLevelHeight = 0x15e0;  // 350 ft per land level
constexpr std::int32_t kCoast = 0x20;
constexpr std::size_t kTerrainRamps = 0xa04;  // `.ENV` 4 ramps x 30 colours per sea state
constexpr std::uint8_t kNoVertex = 0xff;
// The cell struct holds 19 vertex attributes (0x15c bytes).
constexpr std::size_t kMaxCellVertices = 19;

// The 64 x 64 unit normals of slopes -4..+4 in 1/8 steps:
// normalize(cross((1, dx, 0), (0, dz, -1))) at [dz][dx].
const std::array<fx::Vec3, 64 * 64>& normal_table() {
  static const std::array<fx::Vec3, 64 * 64> table = [] {
    std::array<fx::Vec3, 64 * 64> t{};
    std::size_t i = 0;
    for (std::int32_t dz = -0x40000; dz != 0x40000; dz += 0x2000) {
      for (std::int32_t dx = -0x40000; dx != 0x40000; dx += 0x2000) {
        t[i++] = fx::normalize(fx::cross({fx::kOne, dx, 0}, {0, dz, -fx::kOne}));
      }
    }
    return t;
  }();
  return table;
}

// Returns the normal for level differences a (rows) and b (columns); the
// original indexes the table without bounds, here the index is clamped.
const fx::Vec3& normal_at(std::int32_t a, std::int32_t b) {
  const std::int32_t i = std::clamp((32 + a) * 64 + 32 + b, 0, 64 * 64 - 1);
  return normal_table()[static_cast<std::size_t>(i)];
}

struct GridVertex {
  fx::Vec3 p;  // view space
  std::int32_t level = 0;
  std::int32_t sx = 0;
  std::int32_t sy = 0;
};

struct CellAttr {
  std::int32_t sx = 0;
  std::int32_t sy = 0;
  std::int32_t u = 0;
  std::int32_t v = 0;
};

using TriList = std::array<std::uint8_t, 7>;

struct Cell {
  std::size_t count = 4;
  std::array<CellAttr, kMaxCellVertices> attr{};
  std::array<fx::Vec3, kMaxCellVertices> pos{};  // the clip vertex buffer 0x1870d8
  std::array<std::int32_t, 4> level{};       // 0x1870c8
  std::array<TriList, 2> tri{};
  std::array<std::int32_t, 2> shade{};  // +0x144 / +0x148
  std::int32_t z = 0;                   // +0x14c: sort key
  std::int32_t id = 0;                  // +0x150: grid cell index
  std::int32_t band = 0;                // +0x154: distance band 0..3
};

struct Projector {
  std::int32_t half_width;
  void project(const fx::Vec3& p, CellAttr& a) const {
    if (p.z > kNearPlane - 1) {
      a.sx = half_width + fx::imul(p.x, kFocalX) / p.z;
      a.sy = fx::imul(p.y, -kFocalY) / p.z + kHorizonRow;
    }
  }
};

bool outside_near(const fx::Vec3& p) { return p.z < kNearPlane; }
bool outside_left(const fx::Vec3& p) { return -p.z != p.x && p.z <= -p.x; }
bool outside_right(const fx::Vec3& p) { return p.z < p.x; }

// The point of edge j -> i on each plane (`clip_intersect_*`).
fx::Vec3 intersect_near(const fx::Vec3& i, const fx::Vec3& j) {
  const std::int64_t t = kNearPlane - j.z;
  const std::int32_t d = i.z - j.z;
  return {fx::int64_div(t * (i.x - j.x), d) + j.x, j.y + fx::int64_div(t * (i.y - j.y), d), kNearPlane};
}
fx::Vec3 intersect_left(const fx::Vec3& i, const fx::Vec3& j) {
  const std::int32_t d = (i.x - j.x) + (i.z - j.z);
  const std::int64_t t = -j.z - j.x;
  const std::int32_t dx = fx::int64_div(std::int64_t{i.x - j.x} * t, d);
  const std::int32_t dy = fx::int64_div(std::int64_t{i.y - j.y} * t, d);
  return {dx + j.x, j.y + dy, -(j.x + dx)};
}
fx::Vec3 intersect_right(const fx::Vec3& i, const fx::Vec3& j) {
  const std::int32_t d = (i.z - j.z) - (i.x - j.x);
  const std::int64_t t = j.x - j.z;
  const std::int32_t dx = fx::int64_div(std::int64_t{i.x - j.x} * t, d);
  const std::int32_t dy = fx::int64_div(std::int64_t{i.y - j.y} * t, d);
  return {dx + j.x, dy + j.y, dx + j.x};
}

struct TexInfo {
  bool enabled = false;
  std::int32_t u_max = 0;  // texture width - 1 (0x16cfac)
  std::int32_t v_max = 0;
};

// clip_interpolate_texcoords (0x95d4c): the texel of the new vertex from
// the world-space ratio along the edge a -> b.
void interpolate_texcoords(Cell& cell, const ViewCamera& cam, const TexInfo& tex, std::size_t a, std::size_t b,
                           std::size_t n) {
  if (!tex.enabled) {
    return;
  }
  const fx::Vec3 along = fx::transform(cam.inverse, cell.pos[b] - cell.pos[a]);
  const fx::Vec3 to_new = fx::transform(cam.inverse, cell.pos[n] - cell.pos[a]);
  auto lerp = [](std::int32_t from, std::int32_t to, std::int32_t num, std::int32_t den, std::int32_t limit) {
    std::int32_t d = to - from;
    d = fx::trunc(d);
    d = den == 0 ? 0 : fx::int64_div(std::int64_t{d} * num, den);
    d = std::min(d, limit);
    d = std::max(d, -limit);
    return from + d * 0x10000;
  };
  cell.attr[n].u = lerp(cell.attr[a].u, cell.attr[b].u, to_new.x, along.x, tex.u_max);
  cell.attr[n].v = lerp(cell.attr[a].v, cell.attr[b].v, to_new.z, along.z, tex.v_max);
}

// Clips triangle list `list` of `cell` against one plane, appending the new
// vertices (Sutherland-Hodgman from the first inside vertex).
template <typename Outside, typename Intersect>
void clip_list(Cell& cell, TriList& list, Outside outside, Intersect intersect, const ViewCamera& cam,
               const TexInfo& tex, const Projector& proj) {
  TriList out;
  out.fill(kNoVertex);
  std::size_t start = 0;
  while (list[start] != kNoVertex && outside(cell.pos[list[start]])) {
    ++start;
  }
  out[0] = list[start];
  if (list[start] != kNoVertex) {
    std::size_t n = 1;
    std::size_t i = start;
    do {
      const std::size_t next = list[i + 1] == kNoVertex ? 0 : i + 1;
      const std::uint8_t cur_v = list[i];
      const std::uint8_t next_v = list[next];
      const bool a = outside(cell.pos[cur_v]);
      const bool b = outside(cell.pos[next_v]);
      if (!a || !b) {
        if (a != b && cell.count < kMaxCellVertices && n + 1 < out.size()) {
          const std::size_t k = cell.count;
          cell.pos[k] = intersect(cell.pos[cur_v], cell.pos[next_v]);
          interpolate_texcoords(cell, cam, tex, cur_v, next_v, k);
          out[n++] = static_cast<std::uint8_t>(k);
          proj.project(cell.pos[k], cell.attr[k]);
          ++cell.count;
        }
        if (!b && next != start && n + 1 < out.size()) {
          out[n++] = next_v;
        }
      }
      i = next;
    } while (i != start);
  }
  list = out;
}

}  // namespace

void draw_terrain_and_objects(DrawContext& ctx, RenderList& list, const ViewDetail& detail) {
  const WorldViewAssets& assets = ctx.assets;
  const ViewCamera& cam = ctx.camera;
  std::vector<std::size_t> cell_order;
  std::vector<Cell> cells;

  if (assets.world && assets.world->grid.width >= 2 && assets.world->grid.height >= 2) {
    const World& world = *assets.world;
    const IndexedRaster& grid = world.grid;
    const std::uint32_t shift = cam.scale_shift;
    const std::int32_t gw = grid.width;
    const std::int32_t gh = grid.height;
    const std::int32_t range = detail.visibility;

    // The window of grid cells holding the camera and the far corners of
    // the view at `range` cells.
    const fx::Mat3 to_world = fx::mat3_from_euler(Angle10(-cam.heading), Angle10(0), Angle10(0));
    const fx::Vec3 cam_world{cam.x, cam.y, cam.z};
    const fx::Vec3 cam_cell{static_cast<std::int32_t>(static_cast<std::uint32_t>(cam.x) << shift) >> 16,
                            static_cast<std::int32_t>(static_cast<std::uint32_t>(cam.y) << shift) >> 16,
                            static_cast<std::int32_t>(static_cast<std::uint32_t>(cam.z) << shift) >> 16};
    const auto half = static_cast<std::int32_t>((static_cast<std::uint32_t>(cam.half_width * range) + 0xe6) / 0x398);
    const fx::Vec3 c1 = fx::transform(to_world, {-half, 0, range}) + cam_cell;
    const fx::Vec3 c2 = fx::transform(to_world, {half, 0, range}) + cam_cell;
    const std::int32_t x_max = std::clamp(std::max({c1.x, c2.x, cam_cell.x}) + 1, 0, gw - 2);
    const std::int32_t z_max = std::clamp(std::max({c1.z, c2.z, cam_cell.z}) + 1, 0, gh - 2);
    const std::int32_t x_min = std::clamp(std::min({c1.x, c2.x, cam_cell.x}) - 1, 0, gw - 2);
    const std::int32_t z_min = std::clamp(std::min({c1.z, c2.z, cam_cell.z}) - 1, 0, gh - 2);
    const std::int32_t nx = x_max - x_min + 1;
    const std::int32_t nz = z_max - z_min + 1;

    // Distance band limits (0x187200): 1/2, 3/4, 7/8 and 15/16 of the range.
    std::array<std::int32_t, 4> band_limit{};
    for (int i = 0; i < 4; ++i) {
      const auto r = static_cast<std::uint32_t>(range);
      const std::uint32_t cells_in = r - (r >> (4 - i));
      band_limit[static_cast<std::size_t>(3 - i)] =
          fx::mul(cam.inv_zoom, static_cast<std::int32_t>(cells_in * 0x10000));
    }

    TexInfo tex;
    tex.enabled = detail.land_texture && assets.terrain_texture.width() > 0;
    tex.u_max = assets.terrain_texture.width() - 1;
    tex.v_max = assets.terrain_texture.height() - 1;
    const std::array<std::int32_t, 4> corner_u = {0, 0, tex.u_max << 16, tex.u_max << 16};
    const std::array<std::int32_t, 4> corner_v = {tex.v_max << 16, 0, 0, tex.v_max << 16};

    // The view-space vertex grid, south-west corner first.
    const auto step = [&](std::int32_t v) { return (v >> shift) >> shift; };
    const fx::Vec3 east = fx::transform(cam.view, {step(0x10000), 0, 0});
    const fx::Vec3 up = fx::transform(cam.view, {0, step(kLevelHeight), 0});
    const fx::Vec3 north = fx::transform(cam.view, {0, 0, step(0x10000)});
    const fx::Vec3 origin{static_cast<std::int32_t>(static_cast<std::uint32_t>(x_min) << 16) >> shift, 0,
                          static_cast<std::int32_t>(static_cast<std::uint32_t>(z_min) << 16) >> shift};
    fx::Vec3 row_start = fx::transform(cam.view, origin - cam_world);
    const auto stride = static_cast<std::size_t>(nx + 1);
    std::vector<GridVertex> verts(stride * static_cast<std::size_t>(nz + 1));
    for (std::int32_t r = 0; r <= nz; ++r) {
      // Grid rows count from the north edge; the south-most vertex row of a
      // window at the map's edge lies one row past the grid (water here).
      const std::int32_t grid_row = gh - z_min - r;
      fx::Vec3 cur = row_start;
      for (std::int32_t c = 0; c <= nx; ++c) {
        GridVertex& g = verts[static_cast<std::size_t>(r) * stride + static_cast<std::size_t>(c)];
        const std::int32_t col = x_min + c;
        g.level = grid_row >= 0 && grid_row < gh && col < gw ? grid.at(col, grid_row) - kCoast : -1;
        const fx::Vec3 lift = g.level > 0 ? fx::scale(up, g.level << 16) : fx::Vec3{};
        g.p = lift + cur;
        if (g.p.z > kNearPlane - 1) {
          g.sx = fx::imul(g.p.x, kFocalX) / g.p.z + cam.half_width;
          g.sy = fx::imul(g.p.y, -kFocalY) / g.p.z + kHorizonRow;
        }
        cur = cur + east;
      }
      row_start = row_start + north;
    }

    // Sun direction for the shading (world_set_sun's matrix times +z).
    const SkyBodies& sky = ctx.lighting.sky();
    const fx::Mat3 sun = fx::mat3_from_euler(Angle10(-sky.sun_azimuth.tenths()), sky.sun_elevation, Angle10(0));
    const fx::Vec3 away_from_sun = -fx::transform(sun, {0, 0, step(0x10000)});
    const std::int32_t max_shade = tex.enabled ? 11 : 15;
    const Projector proj{cam.half_width};

    cells.reserve(static_cast<std::size_t>(nx) * static_cast<std::size_t>(nz));
    for (std::int32_t r = 0; r < nz; ++r) {
      for (std::int32_t c = 0; c < nx; ++c) {
        const std::size_t base = static_cast<std::size_t>(r) * stride + static_cast<std::size_t>(c);
        const std::array<std::size_t, 4> corner = {base, base + stride, base + stride + 1, base + 1};
        Cell cell;
        cell.z = verts[base].p.z;
        cell.id = (z_min + r) * gw + x_min + c;
        cell.tri[0] = {0, 1, 2, kNoVertex, kNoVertex, kNoVertex, kNoVertex};
        cell.tri[1] = {0, 2, 3, kNoVertex, kNoVertex, kNoVertex, kNoVertex};
        int near_count = 0;
        int wide_count = 0;
        for (std::size_t k = 0; k < 4; ++k) {
          const GridVertex& g = verts[corner[k]];
          cell.level[k] = g.level;
          if (std::abs(g.sx) > 0x4000) {
            ++wide_count;
          }
          cell.attr[k] = {g.sx, g.sy, corner_u[k], corner_v[k]};
          cell.pos[k] = g.p;
          if (g.p.z < kNearPlane) {
            ++near_count;
          }
        }
        // Only all-land triangles (no vertex under water) are drawn.
        for (TriList& t : cell.tri) {
          for (std::size_t i = 0; t[i] != kNoVertex; ++i) {
            if (cell.level[t[i]] < 0) {
              t[0] = kNoVertex;
              break;
            }
          }
        }
        if (near_count == 4) {
          cell.tri[0][0] = kNoVertex;
          cell.tri[1][0] = kNoVertex;
          near_count = 0;
        }
        if (wide_count != 0 && near_count == 0) {
          near_count = 1;
        }
        if (near_count != 0) {
          for (TriList& t : cell.tri) {
            clip_list(cell, t, outside_near, intersect_near, cam, tex, proj);
            if (t[0] != kNoVertex) {
              clip_list(cell, t, outside_left, intersect_left, cam, tex, proj);
              if (t[0] != kNoVertex) {
                clip_list(cell, t, outside_right, intersect_right, cam, tex, proj);
              }
            }
          }
        }
        if (cell.tri[0][0] != kNoVertex || cell.tri[1][0] != kNoVertex) {
          while (cell.band <= 2 && cell.z > band_limit[static_cast<std::size_t>(cell.band)]) {
            ++cell.band;
          }
          const auto& h = cell.level;
          if (cell.tri[0][0] != kNoVertex) {
            const std::int32_t d = fx::dot(normal_at(h[0] - h[1], h[2] - h[1]), away_from_sun);
            cell.shade[0] = std::clamp((d + fx::kOne) >> 12, 0, max_shade);
          }
          if (cell.tri[1][0] != kNoVertex) {
            const std::int32_t d = fx::dot(normal_at(h[3] - h[2], h[2] - h[1]), away_from_sun);
            cell.shade[1] = std::clamp((d + fx::kOne) >> 12, 0, max_shade);
          }
        }
        // Far to near: before the first cell nearer than this one.
        const std::size_t index = cells.size();
        cells.push_back(cell);
        auto it = cell_order.begin();
        while (it != cell_order.end() && cells[*it].z >= cell.z) {
          ++it;
        }
        cell_order.insert(it, index);
        for (std::size_t e : list.order()) {
          if (list.entries()[e].cell == cell.id) {
            list.entries()[e].drawn_by_cell = true;
          }
        }
      }
    }
  }

  // render_list_draw_unplaced (0x9415b): the entries beyond the terrain.
  for (std::size_t e : list.order()) {
    const RenderEntry& entry = list.entries()[e];
    if (entry.draw_unplaced && !entry.drawn_by_cell) {
      draw_entry(ctx, entry, std::nullopt);
    }
  }

  const SeaState state = assets.sea_state;
  for (std::size_t index : cell_order) {
    const Cell& cell = cells[index];
    for (std::size_t t = 0; t < 2; ++t) {
      std::array<PolyVertex, 7> poly{};
      std::size_t n = 0;
      for (std::size_t i = 0; cell.tri[t][i] != kNoVertex && n < poly.size(); ++i) {
        const CellAttr& a = cell.attr[cell.tri[t][i]];
        poly[n++] = {a.sx, a.sy, 0, a.u, a.v};
      }
      if (n < 3) {
        continue;
      }
      const auto band = static_cast<std::size_t>(cell.band);
      const auto shade = static_cast<std::size_t>(cell.shade[t]);
      if (detail.land_texture && assets.terrain_texture.width() > 0) {
        texture_polygon_shaded(ctx.view, std::span(poly.data(), n), assets.terrain_texture,
                               assets.terrain_shades[band][shade]);
      } else {
        const std::size_t at = kTerrainRamps + 120 * static_cast<std::size_t>(state) + 30 * band + shade;
        poly[0].colour = static_cast<std::int32_t>(assets.env.header[at]) << 16;
        fill_polygon(ctx.view, std::span(poly.data(), n));
      }
    }
    // render_list_draw_cell (0x9418c)
    for (std::size_t e : list.order()) {
      const RenderEntry& entry = list.entries()[e];
      if (entry.cell == cell.id) {
        draw_entry(ctx, entry, cell.band);
      }
    }
  }
}

}  // namespace sh::view3d
