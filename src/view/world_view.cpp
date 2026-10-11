#include "view/world_view.h"

#include <algorithm>
#include <format>
#include <string>

#include "assets/pcx.h"
#include "core/error.h"
#include "core/install.h"
#include "gfx/text.h"
#include "view/draw_context.h"

namespace sh {
namespace {

// The effect sprite files in `EffectSprite` order (veh_load_effect_sprites).
constexpr std::array<std::string_view, kEffectSpriteCount> kEffectFiles = {
    "ART/SPOUT.SHP",  "ART/FIRE-0.SHP",  "ART/WAKE.SHP",    "ART/WAKE_F.SHP", "ART/EXPL-0.SHP",
    "ART/EXPL-1.SHP", "ART/DC.SHP",      "ART/GUNFIRE.SHP", "ART/GUNEXPL.SHP", "ART/SHELL.SHP",
    "ART/BOMB.SHP",   "ART/RAFT.SHP",    "ART/SMOKE.SHP"};

constexpr int kSeaTextureSize = 128;
constexpr std::uint8_t kSeaTexelBase = 0x50;  // the first sea colour (range 0 of 0x10febc)
constexpr std::uint8_t kTerrainTexelBase = 0x82;

}  // namespace

WorldViewAssets WorldViewAssets::load(const Install& install, std::string_view env_path, SeaState state,
                                      std::string_view world_path, std::string_view objects_path) {
  WorldViewAssets a;
  a.sea_state = state;
  a.env = parse_environment(install.read(env_path), env_path);
  a.sky = decode_pcx(a.env.sky_pcx, std::format("{} sky", env_path)).image;

  // world_load_sea_texture: 128 x 128, stored minus the first sea colour.
  a.sea_texture = load_pcx(install, "ART/SEA.PCC").image;
  if (a.sea_texture.width() != kSeaTextureSize || a.sea_texture.height() != kSeaTextureSize) {
    throw DataError("ART/SEA.PCC: the sea texture must be 128 x 128");
  }
  for (std::uint8_t& p : a.sea_texture.pixels()) {
    p = static_cast<std::uint8_t>(p - kSeaTexelBase);
  }
  // world_load_terrain_texture: any square picture.
  a.terrain_texture = load_pcx(install, "ART/TTEXT.PCC").image;
  if (a.terrain_texture.width() != a.terrain_texture.height()) {
    throw DataError("ART/TTEXT.PCC: the terrain texture must be square");
  }

  // world_build_env_color_table: texels 130..134 through ramp entries.
  for (int band = 0; band < 4; ++band) {
    const auto ramp = a.env.sea_color_ramp(state, band);
    for (std::size_t level = 0; level < 12; ++level) {
      auto& table = a.terrain_shades[static_cast<std::size_t>(band)][level];
      for (std::size_t k = 0; k < 5; ++k) {
        table[kTerrainTexelBase + k] = ramp[level + k];
      }
    }
  }

  if (!world_path.empty()) {
    a.world = parse_world(install.read(world_path), world_path);
  }
  if (!objects_path.empty()) {
    a.objects = parse_objects(install.read(objects_path), objects_path);
    for (std::size_t i = 0; i < a.objects.size(); ++i) {
      a.object_sprites.push_back(Shp::parse(a.objects[i].shp, std::format("{} object {}", objects_path, i)));
    }
  }
  for (std::size_t i = 0; i < kEffectSpriteCount; ++i) {
    a.effects[i] = Shp::load(install, kEffectFiles[i]);
  }
  return a;
}

std::int32_t sea_drift_step(std::int32_t sea_state, std::int32_t ticks) {
  return (sea_state == 0 ? 1 : 5) * (sea_state + 1) * 0xdd3 * ticks / 100;
}

RenderList render_world_view(const Viewport& vp, const Scene& scene, const WorldViewAssets& assets,
                             const WorldLighting& lighting, WatcomRandom& random) {
  const std::uint32_t shift = assets.world ? assets.world->scale_shift : 0;
  const ViewCamera camera =
      make_view_camera(scene.heading, scene.x, scene.y, scene.z, scene.zoom, scene.screen_width, shift);
  const std::int32_t grid_width = assets.world ? assets.world->grid.width : 0;

  RenderList list;
  for (const RenderObject& object : scene.objects) {
    // The wakes are added unprojected (b = -1), which also sorts them first.
    const bool wake = object.type == RenderType::kTorpedoWake || object.type == RenderType::kShipWake;
    list.add(object, wake ? 0xffffffffu : 0, grid_width, shift);
  }
  const std::size_t scene_entries = list.entries().size();
  // render_list_add_world_objects: the map's landmarks, after the vehicles.
  std::vector<RenderObject> landmarks;
  if (assets.world) {
    landmarks.reserve(assets.world->locations.size());
    for (const Location& loc : assets.world->locations) {
      const auto type = static_cast<std::size_t>(loc.type);
      if (type >= assets.objects.size() || type >= assets.object_sprites.size()) {
        continue;
      }
      RenderObject& o = landmarks.emplace_back();
      o.type = assets.objects[type].render_type == ObjectRenderType::kBillboard ? RenderType::kStaticObject
                                                                                : RenderType::kOrientedObject;
      o.sprite = &assets.object_sprites[type];
      o.x = loc.x;
      o.y = loc.z;
      o.z = loc.y;
      o.heading = Angle10(static_cast<std::int16_t>(loc.heading.tenths()));
      o.size = static_cast<std::uint32_t>(assets.objects[type].size) << 16;
      o.draw_unplaced = false;
      list.add(o, 0, grid_width, shift);
    }
  }
  list.project_all(camera);

  view3d::DrawContext ctx{
      {vp.bitmap, vp.x0, vp.y0, vp.x0 + std::max(kWorldViewWidth, scene.screen_width) - 1,
       vp.y0 + kWorldViewHeight - 1},
      {vp.bitmap, vp.x0, vp.y0, vp.x0 + scene.screen_width - 1, vp.y0 + kWorldViewHeight - 1},
      camera,
      assets,
      lighting,
      random,
      Image8(),
      gui_colour_table(),
  };
  view3d::draw_sky(ctx, scene.detail);
  view3d::draw_sea(ctx, scene.detail, scene.sea_drift);
  view3d::draw_terrain_and_objects(ctx, list, scene.detail);

  // The landmarks die with this call: their entries keep no owner.
  for (std::size_t i = scene_entries; i < list.entries().size(); ++i) {
    list.entries()[i].owner = nullptr;
  }
  return list;
}

}  // namespace sh
