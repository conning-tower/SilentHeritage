// The render-list draw routines (0x10ff0c): `render_draw_static_object`
// (0x941c0), `render_draw_oriented_object` (0x942aa), `veh_draw_ship_sprite`
// (0x81b91) with its fire (0x8205b) and bow wave (0x81f21), and the effect
// draws `splash_draw`, `dc_draw`, `shell_draw`, `explosion_fx_draw`,
// `aircraft_render_cb`, `bomb_draw`, `pilot_draw` and `airgroup_draw`, and
// the wakes (`veh_draw_wake_trail`, 0x8b986).
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "assets/shp.h"
#include "core/fixed_math.h"
#include "view/draw_context.h"
#include "view/polygon.h"

namespace sh::view3d {
namespace {

constexpr std::size_t kDistanceRemaps = 0x3ea;  // `.ENV`: 6 x 256 (band -1 reads 0x2ea)
constexpr std::size_t kTerrainRemaps = 0xd53;   // `.ENV`: 4 x 256, by terrain band
constexpr std::int32_t kMaxWakeZ = 0x67200;      // wakes are drawn nearer than this view z

// The sinking view for each aspect frame (SH.EXE 0x10faa4): NAMES.SHP
// holds three.
const std::array<std::int32_t, 23>& sinking_frames() {
  static const std::array<std::int32_t, 23> table = [] {
    constexpr std::uint32_t kAddress = 0x10faa4;
    const auto t = exe_i32_array<23>(game_exe(), kAddress);
    check_exe_table(std::ranges::all_of(t, [](std::int32_t f) { return f >= 0 && f < 45; }), "sinking frame",
                    kAddress);
    return t;
  }();
  return table;
}

// The sprite banks the fx block's blast index selects (0x129d18...).
constexpr std::array<EffectSprite, 10> kBlastSprites = {
    EffectSprite::kExplosion0, EffectSprite::kExplosion1, EffectSprite::kDepthCharge, EffectSprite::kGunFire,
    EffectSprite::kGunExplosion, EffectSprite::kExplosion1, EffectSprite::kShell,      EffectSprite::kBomb,
    EffectSprite::kRaft,         EffectSprite::kSmoke};

ColourRemap header_remap(const Environment& env, std::size_t offset) {
  ColourRemap r{};
  for (std::size_t i = 0; i < r.size(); ++i) {
    r[i] = env.header[offset + i];
  }
  return r;
}

// The distance band of `distance` against the `.ENV` thresholds: the last
// one not above it, -1 below them all.
std::int32_t distance_band(const DrawContext& ctx, std::uint32_t distance) {
  std::int32_t band = 5;
  while (band >= 0 && (ctx.assets.env.distance_threshold(band) >> ctx.camera.scale_shift) > distance) {
    --band;
  }
  return band;
}

ColourRemap distance_remap(const DrawContext& ctx, std::uint32_t distance) {
  return header_remap(ctx.assets.env, static_cast<std::size_t>(static_cast<std::int32_t>(kDistanceRemaps) +
                                                               256 * distance_band(ctx, distance)));
}

Fixed16 fixed(std::int32_t raw) { return Fixed16::from_raw(raw); }

// sprite_draw_transformed with the original's remap flag.
void draw_sprite(DrawContext& ctx, const Viewport& vp, const Shp* set, std::int32_t frame, std::int32_t x,
                 std::int32_t y, std::int32_t angle, std::int32_t scale_x, std::int32_t scale_y,
                 const ColourRemap& remap) {
  if (set == nullptr || frame < 0 || static_cast<std::size_t>(frame) >= set->frame_count()) {
    return;
  }
  sprite_draw_transformed(vp, set->frame(static_cast<std::size_t>(frame)), x, y, ctx.scratch, Angle10(angle),
                          fixed(scale_x), fixed(scale_y), kSpriteRemap, &remap);
}

std::int32_t frame_count(const Shp* set) { return set == nullptr ? 0 : static_cast<std::int32_t>(set->frame_count()); }

Angle10 bearing_to_camera(const RenderEntry& e) { return fx::heading_from_delta(-e.x, -e.z); }

// The signed y / 16 of the original (`cdq; shl edx, 4; sbb; sar 4`).
std::int32_t feet(std::int32_t y) { return y / 16; }

void draw_static_object(DrawContext& ctx, const RenderEntry& e, std::optional<std::int32_t> cell_band) {
  const std::int32_t n = frame_count(e.sprite);
  if (n <= 0) {
    return;
  }
  // At least one degree per frame: the original divides by zero past 360
  // frames.
  const std::int32_t step = std::max(360 / n, 1);
  const Angle10 first = fx::angle_sub(Angle10(e.heading), Angle10(step * 10 / 2));
  const std::int32_t frame = fx::angle_sub(bearing_to_camera(e), first).tenths() / 10 / step;
  // Drawn outside the terrain, the original reads a band from address 0x154
  // (an undefined value); the nearest band is used here.
  const std::int32_t band = cell_band.value_or(0);
  const ColourRemap remap = header_remap(ctx.assets.env, kTerrainRemaps + 256 * static_cast<std::size_t>(band));
  const auto size = static_cast<std::int32_t>(e.size);
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, 0, size, size, remap);
}

// Mirrors an aspect frame past the sprite's views: returns the frame and
// negates `scale_x` for the far side.
std::int32_t mirror_frame(std::int32_t frame, std::int32_t count, std::int32_t& scale_x) {
  if (frame >= count) {
    frame = count - (frame - count) - 1;
    scale_x = -scale_x;
  }
  return frame;
}

void draw_oriented_object(DrawContext& ctx, const RenderEntry& e) {
  const std::int32_t n = frame_count(e.sprite);
  std::int32_t scale_x = static_cast<std::int32_t>(e.size);
  const std::int32_t frame = mirror_frame(render_view_aspect_frame(e), n, scale_x);
  if (frame >= n) {
    return;
  }
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, 0, scale_x, static_cast<std::int32_t>(e.size),
              distance_remap(ctx, e.distance));
}

// veh_draw_fire_sprite (0x8205b): the fire above the deck, half size.
void draw_fire(DrawContext& ctx, const RenderEntry& e, const ShipLook& ship) {
  if (!ship.fx.on_fire) {
    return;
  }
  const std::int32_t lift = fx::imul(ship.fire_height_ft, kFocalY * kUnitsPerFoot) / e.view_z;
  const std::int32_t half = static_cast<std::int32_t>(e.size) / 2;
  draw_sprite(ctx, ctx.view, &ctx.assets.effect(EffectSprite::kFire), ship.fx.fire_frame, e.screen_x,
              e.screen_y - lift, 0, half, half, ctx.gui);
}

// 0x81f21: the bow wave of a moving ship, three animation steps per view.
void draw_bow_wave(DrawContext& ctx, const RenderEntry& e, const RenderObject& obj, std::int32_t waterline,
                   std::int32_t aspect) {
  const ShipLook& ship = obj.ship;
  if (ship.fx.sinking || ship.speed_tenths <= 1) {
    return;
  }
  const Shp& wave = ctx.assets.effect(ship.speed_tenths > 150 ? EffectSprite::kWakeFast : EffectSprite::kWake);
  if (feet(obj.y) + ship.fire_height_ft < 0) {
    return;
  }
  const auto scale = static_cast<std::int32_t>(projected_size(ship.bow_wave_size, e.distance, ctx.camera.scale_shift));
  const std::int32_t views = frame_count(&wave) / 3;
  std::int32_t scale_x = scale;
  const std::int32_t frame = mirror_frame(aspect, views, scale_x);
  if (frame >= views) {
    return;
  }
  draw_sprite(ctx, ctx.view, &wave, ship.fx.wave_phase + 3 * frame, e.screen_x, waterline + 2, 0, scale_x, scale,
              ctx.gui);
}

void draw_ship(DrawContext& ctx, const RenderEntry& e) {
  const RenderObject& obj = *e.owner;
  const ShipLook& ship = obj.ship;
  const bool sinking_views = ship.fx.sinking && ship.sinking_sprite != nullptr;
  const Shp* set = sinking_views ? ship.sinking_sprite : e.sprite;
  const auto size = static_cast<std::int32_t>(e.size);
  std::int32_t scale_x = size;
  const std::int32_t n = frame_count(set);
  const std::int32_t aspect = render_view_aspect_frame(e);
  bool fire_first = false;
  if ((aspect < 2 || aspect > 42) && (ship.fire_order & 1) != 0) {
    fire_first = true;
  }
  if (aspect > 20 && aspect < 24 && (ship.fire_order & 2) != 0) {
    fire_first = true;
  }
  std::int32_t frame = mirror_frame(aspect, n, scale_x);
  if (frame >= n) {
    return;
  }
  if (fire_first) {
    draw_fire(ctx, e, ship);
  }
  if (sinking_views && frame >= 0 && frame < static_cast<std::int32_t>(sinking_frames().size())) {
    frame = sinking_frames()[static_cast<std::size_t>(frame)];
  }
  // Far ships sink under the horizon between the hull-down ranges.
  std::int32_t hull_y = e.screen_y;
  // (Equal hull-down ranges would divide by zero in the original.)
  if (ship.hull_down_end_yards > 0 && ship.range_yards > ship.hull_down_start_yards &&
      ship.hull_down_end_yards != ship.hull_down_start_yards) {
    const std::int32_t r = std::min(ship.range_yards, ship.hull_down_end_yards);
    const std::int32_t bottom = fx::imul(ship.hull_down_height_ft, kFocalY * kUnitsPerFoot) / e.view_z + kHorizonRow;
    hull_y += fx::imul(bottom - e.screen_y, r - ship.hull_down_start_yards) /
              (ship.hull_down_end_yards - ship.hull_down_start_yards);
  }
  // The hull is cut at the waterline (the sea surface row when submerged).
  std::int32_t waterline = e.screen_y;
  if (obj.y < 0) {
    waterline = fx::imul(ctx.camera.y, kFocalY) / e.view_z + kHorizonRow;
  }
  const Viewport above_water = with_bottom(ctx.view, waterline);
  if (feet(obj.y) + ship.mast_height_ft >= 0) {
    draw_sprite(ctx, above_water, set, frame, e.screen_x, hull_y, 0, scale_x, size, distance_remap(ctx, e.distance));
  }
  if (ship.fx.sinking) {
    draw_sprite(ctx, above_water, &ctx.assets.effect(EffectSprite::kFire), ship.fx.fire_frame, e.screen_x,
                waterline + 2, 0, size, size, ctx.gui);
    if (ship.role != 3 && ship.fx.blast_frame >= 0 && ship.fx.blast_sprite >= 0 &&
        static_cast<std::size_t>(ship.fx.blast_sprite) < kBlastSprites.size()) {
      const Shp& blast = ctx.assets.effect(kBlastSprites[static_cast<std::size_t>(ship.fx.blast_sprite)]);
      draw_sprite(ctx, above_water, &blast, ship.fx.blast_frame, e.screen_x, waterline + 2, 0, size, size, ctx.gui);
    }
  }
  if (ship.range_yards <= ship.hull_down_start_yards) {
    draw_bow_wave(ctx, e, obj, waterline, aspect);
  }
  if (!fire_first) {
    draw_fire(ctx, e, ship);
  }
}

// splash_draw (0x88244) / explosion_fx_draw (0x8861b): the owner's frame.
void draw_effect_frame(DrawContext& ctx, const RenderEntry& e) {
  const auto size = static_cast<std::int32_t>(e.size);
  draw_sprite(ctx, ctx.view, e.sprite, e.owner->frame, e.screen_x, e.screen_y, 0, size, size, ctx.gui);
}

// The splash of a depth charge or bomb entering the water: SPOUT.SHP at
// 1/8 (charges) or 1/6 (bombs) of the weapon's size.
void draw_weapon_splash(DrawContext& ctx, const RenderEntry& e, std::int32_t divisor) {
  const std::int32_t size = static_cast<std::int32_t>(e.size) / divisor;
  draw_sprite(ctx, ctx.view, &ctx.assets.effect(EffectSprite::kSpout), e.owner->frame, e.screen_x, e.screen_y, 0,
              size, size, ctx.gui);
}

void draw_depth_charge(DrawContext& ctx, const RenderEntry& e) {
  if (e.owner->frame >= 0) {
    draw_weapon_splash(ctx, e, 8);
    return;
  }
  const std::int32_t n = frame_count(e.sprite);
  std::int32_t frame = (fx::angle_sub(bearing_to_camera(e), Angle10(e.heading)).tenths() / 10 + 10) / 20;
  if (frame >= 18) {
    frame = 0;
  }
  std::int32_t scale_x = static_cast<std::int32_t>(e.size);
  frame = mirror_frame(frame, n, scale_x);
  if (frame >= n) {
    return;
  }
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, 0, scale_x, static_cast<std::int32_t>(e.size),
              distance_remap(ctx, e.distance));
}

void draw_shell(DrawContext& ctx, const RenderEntry& e) {
  const std::int32_t n = frame_count(e.sprite);
  std::int32_t frame =
      (fx::angle_sub(bearing_to_camera(e), fx::angle_sub(Angle10(e.heading), Angle10(2700))).tenths() / 10 + 10) / 20;
  if (frame > 18) {
    frame = 0;
  }
  if (frame >= n) {
    return;
  }
  // The tracer dot under the shell.
  put_pixel(ctx.view, e.screen_x, e.screen_y, ctx.random.random_int(3) == 0 ? 0x41 : 0x16);
  const auto size = static_cast<std::int32_t>(e.size);
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, 0, size, size, distance_remap(ctx, e.distance));
}

void draw_bomb(DrawContext& ctx, const RenderEntry& e) {
  if (e.owner->frame >= 0) {
    draw_weapon_splash(ctx, e, 6);
    return;
  }
  const std::int32_t n = frame_count(e.sprite);
  const std::int32_t frame =
      (fx::angle_sub(bearing_to_camera(e), fx::angle_sub(Angle10(e.heading), Angle10(2700))).tenths() + 0x70) / 0xe1;
  if (frame >= n) {
    return;
  }
  put_pixel(ctx.view, e.screen_x, e.screen_y, 0x16);
  const auto size = static_cast<std::int32_t>(e.size);
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, 0, size, size, distance_remap(ctx, e.distance));
}

void draw_raft(DrawContext& ctx, const RenderEntry& e) {
  const std::int32_t n = frame_count(e.sprite);
  std::int32_t scale_x = static_cast<std::int32_t>(e.size);
  const std::int32_t frame = mirror_frame(render_view_aspect_frame(e), n, scale_x);
  if (frame >= n) {
    return;
  }
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, 0, scale_x, static_cast<std::int32_t>(e.size),
              distance_remap(ctx, e.distance));
}

// aircraft_render_cb (0x848c1): 12 yaw sectors (mirrored past 6) x 7
// elevation sectors, shifted by the aircraft's pitch and roll.
void draw_aircraft(DrawContext& ctx, const RenderEntry& e) {
  const RenderObject& obj = *e.owner;
  const std::int32_t pitch = obj.pitch.tenths();
  const std::int32_t roll = obj.roll.tenths();
  std::int32_t yaw =
      fx::angle_sub(fx::angle_add(bearing_to_camera(e), Angle10(0x96)), Angle10(e.heading)).tenths() / 300;
  const std::int32_t level = std::abs(e.z) > std::abs(e.x) ? e.z : e.x;
  std::int32_t elevation =
      fx::angle_add(fx::angle_sub(Angle10(900), fx::heading_from_delta(e.y, std::abs(level))), Angle10(0x96)).tenths() /
      300;
  std::int32_t scale_x = static_cast<std::int32_t>(e.size);
  bool mirrored = false;
  if (yaw > 6) {
    yaw = 12 - yaw;
    scale_x = -scale_x;
    mirrored = true;
  }
  elevation = std::min(elevation, 6);
  std::int32_t frame = yaw * 7 + elevation;
  std::int32_t angle = 0;
  if (yaw != 3) {
    const std::int32_t d = fx::angle_add(Angle10(pitch), Angle10(0x96)).tenths() / 300;
    frame += yaw == 6 ? -d : d;
  }
  if (yaw != 0 && yaw != 6) {
    angle = pitch;
  }
  if (yaw == 0) {
    angle = roll / 3;
  }
  if (yaw == 6) {
    angle = -roll / 3;
  }
  if (yaw != 0 && yaw != 6) {
    const std::int32_t d = fx::angle_add(Angle10(roll), Angle10(300)).tenths() / 600;
    frame += mirrored ? d : -d;
  }
  if (frame >= frame_count(e.sprite) || frame < 0) {
    return;
  }
  draw_sprite(ctx, ctx.view, e.sprite, frame, e.screen_x, e.screen_y, angle, scale_x,
              static_cast<std::int32_t>(e.size), distance_remap(ctx, e.distance));
  if (e.distance > 24000) {
    put_pixel(ctx.view, e.screen_x, e.screen_y, 0x10);
  }
}

// airgroup_draw (0x8ab7e): one pixel, colour 16 through the distance remap
// of a quarter of the distance.
void draw_air_group(DrawContext& ctx, const RenderEntry& e) {
  std::int32_t band = 5;
  while (band >= 0 && (e.distance >> 2) < (ctx.assets.env.distance_threshold(band) >> ctx.camera.scale_shift)) {
    --band;
  }
  const auto at = static_cast<std::size_t>(static_cast<std::int32_t>(kDistanceRemaps) + 256 * band + 0x10);
  put_pixel(ctx.view, e.screen_x, e.screen_y, ctx.assets.env.header[at]);
}

// render_fill_clipped_polygon (0x8bc58): clips the view-space polygon at
// the near plane (new points keep the outside point's y), projects it and
// remaps it if every point lies between the near plane and z 0x67200.
void remap_clipped_polygon(DrawContext& ctx, std::span<const fx::Vec3> pts, const ColourRemap& table) {
  constexpr std::int32_t kNear = kNearZ;
  std::array<fx::Vec3, 8> out{};
  std::size_t m = 0;
  for (std::size_t i = 0; i < pts.size(); ++i) {
    const std::size_t j = i + 1 >= pts.size() ? 0 : i + 1;
    if (pts[i].z < kNear && pts[j].z < kNear) {
      continue;
    }
    std::size_t outside = pts.size();
    std::size_t inside = 0;
    if (pts[i].z >= kNear) {
      out[m++] = pts[i];
    } else {
      inside = j;
      outside = i;
    }
    if (pts[j].z < kNear) {
      inside = i;
      outside = j;
    }
    if (outside != pts.size()) {
      const fx::Vec3& o = pts[outside];
      const fx::Vec3& in = pts[inside];
      const std::int32_t x = o.x + fx::int64_div(std::int64_t{kNear - o.z} * (in.x - o.x), in.z - o.z);
      out[m++] = {x, o.y, kNear};
    }
  }
  if (m < 3) {
    return;
  }
  std::array<PolyVertex, 8> poly{};
  for (std::size_t k = 0; k < m; ++k) {
    if (out[k].z >= kMaxWakeZ || out[k].z < kNear) {
      return;
    }
    poly[k].x = fx::imul(out[k].x, kFocalX) / out[k].z + ctx.camera.half_width;
    poly[k].y = fx::imul(-out[k].y, kFocalY) / out[k].z + kHorizonRow;
  }
  remap_polygon(ctx.view, std::span(poly.data(), m), table);
}

// veh_draw_wake_trail (0x8b986): quads of the wake width along the trail,
// from the owner back to its oldest position, lightening the sea colours
// 80-95 less and less, ending in a triangle; at most 10 segments.
void draw_wake(DrawContext& ctx, const RenderEntry& e) {
  const RenderObject& obj = *e.owner;
  const ViewCamera& cam = ctx.camera;
  const auto count = static_cast<std::int32_t>(std::min<std::size_t>(obj.trail.size(), 10));
  if (count == 0) {
    return;
  }
  ColourRemap table{};
  for (std::size_t c = 0; c < table.size(); ++c) {
    table[c] = static_cast<std::uint8_t>(c);  // the original leaves 255 unset
  }
  const std::int32_t half = obj.wake_width >> 1;
  std::int32_t segments = 10;
  std::int32_t fade = 0;
  fx::Vec3 prev;
  for (std::int32_t i = count; i >= 0; --i) {
    std::int32_t rx = e.x;
    std::int32_t rz = e.z;
    if (i != count) {
      rx = obj.trail[static_cast<std::size_t>(i)].x - cam.x;
      rz = obj.trail[static_cast<std::size_t>(i)].z - cam.z;
    }
    const fx::Vec3 p = fx::transform(cam.view, {rx, -cam.y, rz});
    if (i != count) {
      const std::int32_t steps = fx::trunc(fade);
      for (std::int32_t c = 0x50; c < 0x60; ++c) {
        std::int32_t t = c + 1 - (5 - steps);
        t = std::max(t, 0x52);
        t = std::min(t, c);
        table[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(t);
      }
      fade += 0x8000;
      if (p.z < kMaxWakeZ && (p.z >= kNearZ || prev.z >= kNearZ)) {
        const fx::Mat3 along =
            fx::mat3_from_euler(fx::heading_from_delta(-(p.x - prev.x), p.z - prev.z), Angle10(0), Angle10(0));
        const fx::Vec3 o = fx::transform(along, {half, 0, 0});
        std::array<fx::Vec3, 4> quad = {{{p.x + o.x, p.y, p.z + o.z},
                                         {p.x - o.x, p.y, p.z - o.z},
                                         {prev.x - o.x, prev.y, prev.z - o.z},
                                         {prev.x + o.x, prev.y, prev.z + o.z}}};
        remap_clipped_polygon(ctx, quad, table);
        if (--segments < 1) {
          i = 0;
        }
        if (i == 0) {
          quad[2] = {p.x + fx::imul(o.z, -8), p.y, p.z + fx::imul(o.x, 8)};
          remap_clipped_polygon(ctx, std::span(quad.data(), 3), table);
        }
      }
    }
    prev = p;
  }
}

}  // namespace

void draw_entry(DrawContext& ctx, const RenderEntry& entry, std::optional<std::int32_t> cell_band) {
  switch (entry.type) {
    case RenderType::kStaticObject:
      draw_static_object(ctx, entry, cell_band);
      break;
    case RenderType::kOrientedObject:
      draw_oriented_object(ctx, entry);
      break;
    case RenderType::kShip:
    case RenderType::kEnemySub:
      draw_ship(ctx, entry);
      break;
    case RenderType::kSplash:
    case RenderType::kExplosion:
      draw_effect_frame(ctx, entry);
      break;
    case RenderType::kTorpedoWake:
    case RenderType::kShipWake:
      draw_wake(ctx, entry);
      break;
    case RenderType::kDepthCharge:
      draw_depth_charge(ctx, entry);
      break;
    case RenderType::kShell:
      draw_shell(ctx, entry);
      break;
    case RenderType::kAircraft:
      draw_aircraft(ctx, entry);
      break;
    case RenderType::kBomb:
      draw_bomb(ctx, entry);
      break;
    case RenderType::kRaft:
      draw_raft(ctx, entry);
      break;
    case RenderType::kAirGroup:
      draw_air_group(ctx, entry);
      break;
  }
}

}  // namespace sh::view3d
