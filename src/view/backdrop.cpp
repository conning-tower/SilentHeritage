// The sky and the sea of the 3D view: `world_draw_sky` (0x91d1e) with the
// panorama (0x91d75, `render_draw_sky_band` 0x96825), the stars (0x922fb)
// and the sun and moon (0x91fd8, `sky_blit_above_horizon` 0x9220e), and
// `world_draw_sea_plane` (0x92513, `render_texture_span` 0x96328).
#include <algorithm>
#include <array>
#include <cstdint>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "core/fixed_math.h"
#include "view/draw_context.h"

namespace sh::view3d {
namespace {

// `.ENV` header offsets.
constexpr std::size_t kSeaShift = 0x304;     // texture scale shift of the sea
constexpr std::size_t kSeaShades = 0x308;    // 6 bands x 16 colours (sea texels 0..15)
constexpr std::size_t kSkyColour = 0xbe4;    // plain sky without the panorama
constexpr std::size_t kSkyThreshold = 0x1157;  // sky bodies drawn over indices below; 0x81 none
constexpr std::uint8_t kNoSkyBodies = 0x81;

constexpr int kPanoramaWidth = 1280;
constexpr int kPanoramaRows = 65;
constexpr std::uint8_t kStarColour = 0x80;

// The night stars (SH.EXE 0x1104f4): azimuth and elevation in tenths of a
// degree, 32 bits each.
const std::array<std::int32_t, 52>& stars() {
  static const std::array<std::int32_t, 52> table = [] {
    constexpr std::uint32_t kAddress = 0x1104f4;
    const auto t = exe_i32_array<52>(game_exe(), kAddress);
    for (std::size_t i = 0; i < t.size(); i += 2) {
      check_exe_table(t[i] >= 0 && t[i] < 3600 && t[i + 1] >= 0 && t[i + 1] <= 900, "star", kAddress);
    }
    return t;
  }();
  return table;
}

std::uint8_t* frame_pixel(const Viewport& frame, int x, int y) {
  return &frame.bitmap->row(frame.y0 + y)[static_cast<std::size_t>(frame.x0 + x)];
}

// The half field of view in tenths of a degree (atan(half width / 460)).
std::int32_t half_fov(const ViewCamera& cam) {
  return fx::heading_from_delta(cam.half_width << 16, kFocalY << 16).tenths();
}

// Returns the screen x of `azimuth` relative to the view's left edge.
std::int32_t sky_x(const ViewCamera& cam, std::int32_t azimuth, std::int32_t px_per_tenth) {
  std::int32_t d =
      fx::angle_sub(Angle10(azimuth), fx::angle_sub(Angle10(cam.heading), Angle10(half_fov(cam)))).tenths();
  if (d > 1800) {
    d -= 3600;
  }
  return fx::trunc(fx::mul(d << 16, px_per_tenth));
}

void draw_panorama(DrawContext& ctx) {
  const ViewCamera& cam = ctx.camera;
  const Image8& sky = ctx.assets.sky;
  const std::int32_t fov = half_fov(cam);
  const std::int32_t src_per_tenth = fx::div(kPanoramaWidth << 16, 3600 << 16);
  const std::int32_t screen_per_tenth = fx::div(cam.half_width << 16, fov << 16);
  const std::int32_t step = fx::div(src_per_tenth, screen_per_tenth);
  const std::int32_t left =
      fx::trunc(fx::mul(src_per_tenth, fx::angle_sub(Angle10(cam.heading), Angle10(fov)).tenths() << 16));
  std::int32_t before_wrap = fx::trunc(fx::div((kPanoramaWidth - left) << 16, step));
  if (before_wrap >= cam.screen_width) {
    before_wrap = cam.screen_width;
  }
  if (sky.width() != kPanoramaWidth || sky.height() < kPanoramaRows) {
    return;
  }
  for (int r = 0; r < kPanoramaRows; ++r) {
    const auto src = sky.row(r);
    std::uint8_t* top = frame_pixel(ctx.frame, 0, 2 * r);
    std::uint8_t* bottom = frame_pixel(ctx.frame, 0, 2 * r + 1);
    std::uint32_t acc = 0;
    int x = 0;
    for (; x < before_wrap; ++x, acc += static_cast<std::uint32_t>(step)) {
      top[x] = bottom[x] = src[static_cast<std::size_t>(left) + (acc >> 16)];
    }
    acc = 0;
    for (; x < cam.screen_width; ++x, acc += static_cast<std::uint32_t>(step)) {
      top[x] = bottom[x] = src[acc >> 16];
    }
  }
}

void draw_stars(DrawContext& ctx) {
  const Environment& env = ctx.assets.env;
  if (!ctx.lighting.night() || env.header[kSkyThreshold] == kNoSkyBodies) {
    return;
  }
  const ViewCamera& cam = ctx.camera;
  const std::int32_t vfov = fx::heading_from_delta(kHorizonRow << 16, kFocalY << 16).tenths();
  const std::int32_t px_x = fx::div(cam.half_width << 16, half_fov(cam) << 16);
  const std::int32_t px_y = fx::div(kHorizonRow << 16, vfov << 16);
  const std::array<std::int32_t, 52>& table = stars();
  for (std::size_t i = 0; i < table.size(); i += 2) {
    const std::int32_t x = sky_x(cam, table[i], px_x);
    const std::int32_t y = kHorizonRow - fx::trunc(fx::mul(table[i + 1] << 16, px_y));
    const int under = get_pixel(ctx.view, x, y);
    if (under >= 0 && under < env.header[kSkyThreshold]) {
      put_pixel(ctx.view, x, y, kStarColour);
    }
  }
}

// sky_blit_above_horizon: `bitmap` (w x h, 0 transparent) centred on
// (cx, cy), drawn over frame pixels below the sky threshold.
void blit_sky_body(DrawContext& ctx, std::int32_t cx, std::int32_t cy, const std::uint8_t* bitmap, int w, int h) {
  const int width = ctx.camera.screen_width;
  int left = cx - w / 2;
  const int right = left + w - 1;
  int top = cy - h / 2;
  const int bottom = top + h - 1;
  if (right < 0 || left >= width || bottom < 0 || top >= kHorizonRow) {
    return;
  }
  int skip_x = 0;
  int cols = w;
  if (left < 0) {
    skip_x = -left;
    cols = w - skip_x;
    left = 0;
  }
  int skip_y = 0;
  int rows = h;
  if (top < 0) {
    skip_y = -top;
    rows -= skip_y;
    top = 0;
  }
  if (right >= width) {
    cols -= right - width + 1;
  }
  if (bottom >= kWorldViewHeight) {
    rows -= bottom - (kWorldViewHeight - 1);
  }
  const std::uint8_t threshold = ctx.assets.env.header[kSkyThreshold];
  for (int y = 0; y < rows; ++y) {
    const std::uint8_t* src = bitmap + (skip_y + y) * w + skip_x;
    std::uint8_t* dst = frame_pixel(ctx.frame, left, top + y);
    for (int x = 0; x < cols; ++x) {
      if (dst[x] < threshold && src[x] != 0) {
        dst[x] = src[x];
      }
    }
  }
}

void draw_sky_body(DrawContext& ctx, bool sun) {
  const Environment& env = ctx.assets.env;
  if (env.header[kSkyThreshold] == kNoSkyBodies) {
    return;
  }
  const SkyBodies& sky = ctx.lighting.sky();
  const std::uint8_t* bitmap = nullptr;
  int size = 0;
  std::int32_t azimuth = 0;
  std::int32_t elevation = 0;
  if (sun) {
    bitmap = env.sun.data();
    size = 31;
    azimuth = sky.sun_azimuth.normalized().tenths();
    elevation = signed_elevation(sky.sun_elevation);
  } else {
    if (sky.moon_phase == 4 || sky.moon_phase < 0 ||
        static_cast<std::size_t>(sky.moon_phase) >= env.moon_phases.size()) {
      return;
    }
    bitmap = env.moon_phases[static_cast<std::size_t>(sky.moon_phase)].data();
    size = 33;
    azimuth = sky.moon_azimuth.normalized().tenths();
    elevation = signed_elevation(sky.moon_elevation);
  }
  const ViewCamera& cam = ctx.camera;
  // The vertical scale is a fifth of the horizon's: sun and moon sit low.
  const std::int32_t vfov5 = fx::heading_from_delta(kHorizonRow << 16, kFocalY << 16).tenths() * 5;
  const std::int32_t px_x = fx::div(cam.half_width << 16, half_fov(cam) << 16);
  const std::int32_t px_y = fx::div(kHorizonRow << 16, vfov5 << 16);
  const std::int32_t x = sky_x(cam, azimuth, px_x);
  const std::int32_t y = kHorizonRow - fx::trunc(fx::mul(elevation << 16, px_y));
  blit_sky_body(ctx, x, y, bitmap, size, size);
}

}  // namespace

void put_pixel(const Viewport& vp, int x, int y, std::uint8_t colour) {
  if (!vp.valid()) {
    return;
  }
  const int bx = vp.x0 + x;
  const int by = vp.y0 + y;
  if (vp.clip().contains(bx, by)) {
    vp.bitmap->set(bx, by, colour);
  }
}

int get_pixel(const Viewport& vp, int x, int y) {
  if (!vp.valid()) {
    return -1;
  }
  const int bx = vp.x0 + x;
  const int by = vp.y0 + y;
  return vp.clip().contains(bx, by) ? vp.bitmap->at(bx, by) : -1;
}

void draw_sky(DrawContext& ctx, const ViewDetail& detail) {
  if (detail.clouds) {
    draw_panorama(ctx);
    draw_stars(ctx);
  } else {
    const std::uint8_t colour = ctx.assets.env.header[kSkyColour];
    for (int y = 0; y < kHorizonRow; ++y) {
      std::uint8_t* row = frame_pixel(ctx.frame, 0, y);
      std::fill(row, row + kWorldViewWidth, colour);
    }
  }
  draw_sky_body(ctx, true);
  draw_sky_body(ctx, false);
}

void draw_sea(DrawContext& ctx, const ViewDetail& detail, std::int32_t drift) {
  const Environment& env = ctx.assets.env;
  if (!detail.waves) {
    const auto bands = env.flat_sea_row_bands();
    const auto colours = env.flat_sea_band_colors();
    auto fill = [&](int first, int last, std::uint8_t band) {
      last = std::min(last, kWorldViewHeight - 1);
      for (int y = first; y <= last; ++y) {
        std::uint8_t* row = frame_pixel(ctx.frame, 0, y);
        std::fill(row, row + kWorldViewWidth, colours[band]);
      }
    };
    std::uint8_t band = bands[0];
    int start = kHorizonRow;
    for (int i = 0; i < kHorizonRow; ++i) {
      if (bands[static_cast<std::size_t>(i)] != band) {
        fill(start, kHorizonRow + i, band);
        band = bands[static_cast<std::size_t>(i)];
        start = kHorizonRow + i;
      }
    }
    fill(start, kWorldViewHeight, band);
    return;
  }

  const ViewCamera& cam = ctx.camera;
  // Texture coordinates in 16.16 texels (one per foot), as the original's
  // 32-bit shifts compute them.
  auto shifted = [](std::int32_t v, int right) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << 16) >> right;
  };
  std::int32_t u0 = fx::wadd(shifted(cam.x, 4), drift);
  std::int32_t v0 = fx::wadd(shifted(cam.z, 4), drift);
  const std::int32_t height = fx::div(shifted(cam.y, 6), 0x80000) + fx::kOne;
  if (cam.zoom > fx::kOne) {
    const fx::Vec3 ahead = fx::transform(cam.inverse, {0, 0, kFocalY << 16});
    u0 = fx::wadd(fx::mul(u0, cam.inv_zoom), fx::wadd(fx::mul(ahead.x, cam.zoom), -ahead.x));
    v0 = fx::wadd(fx::mul(v0, cam.inv_zoom), fx::wadd(fx::mul(ahead.z, cam.zoom), -ahead.z));
  }
  const fx::Vec3 corner = fx::transform(cam.inverse, {-(cam.half_width << 16), 0, kFocalY << 16});
  const int shift = env.header[kSeaShift];
  const std::int32_t corner_x = corner.x << shift;
  const std::int32_t corner_z = corner.z << shift;
  const std::int32_t step_x = cam.right_x << shift;
  const std::int32_t step_z = cam.right_z << shift;
  const auto bands = env.flat_sea_row_bands();
  const auto texels = ctx.assets.sea_texture.pixels();
  if (texels.size() != 128 * 128) {
    return;
  }
  for (int r = 1; r <= kHorizonRow; ++r) {
    const std::int32_t depth = fx::div(r << 16, height);
    std::int32_t u = fx::wadd(u0, fx::div(corner_x, depth));
    std::int32_t v = fx::wadd(v0, fx::div(corner_z, depth));
    const std::int32_t du = fx::div(step_x, depth);
    const std::int32_t dv = fx::div(step_z, depth);
    const std::uint8_t* shades = env.header.data() + kSeaShades + 16 * bands[static_cast<std::size_t>(r - 1)];
    std::uint8_t* dst = frame_pixel(ctx.frame, 0, kHorizonRow + r - 1);
    for (int x = 0; x < cam.screen_width; ++x) {
      const auto at = static_cast<std::size_t>(((v >> 16) & 127) * 128 + ((u >> 16) & 127));
      dst[x] = shades[texels[at]];
      u = fx::wadd(u, du);
      v = fx::wadd(v, dv);
    }
  }
}

}  // namespace sh::view3d
