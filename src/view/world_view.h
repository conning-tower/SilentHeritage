// The 3D world view of the bridge, periscope, TBT and deck gun
// (`world_render_view`, 0x91a69): the camera, the render list of the scene's
// objects and the map's landmarks, the sky panorama with the sun, moon and
// stars, the sea plane, the terrain of the `.WLD` grid with the objects of
// each cell, and the per-type sprite draws (ships, aircraft, effects).
//
// Usage: load `WorldViewAssets` once per mission; each frame fill a `Scene`
// from the simulation and call `render_world_view` on a 640 x 260 frame
// (`make_world_view_frame`), then show the station's 200-row window of it.
// The palette is built separately with `WorldLighting` (view/world_palette.h).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "assets/environment.h"
#include "assets/objects.h"
#include "assets/shp.h"
#include "assets/world.h"
#include "core/fixed.h"
#include "core/image.h"
#include "core/random.h"
#include "gfx/viewport.h"
#include "view/render_list.h"
#include "view/world_palette.h"

namespace sh {

class Install;

// The effect sprites `veh_load_effect_sprites` loads (0x129d08..0x129d3c),
// in that order.
enum class EffectSprite : std::size_t {
  kSpout,      // SPOUT.SHP: splashes
  kFire,       // FIRE-0.SHP
  kWake,       // WAKE.SHP: bow wave
  kWakeFast,   // WAKE_F.SHP: bow wave above 15 kn
  kExplosion0, // EXPL-0.SHP
  kExplosion1, // EXPL-1.SHP
  kDepthCharge,
  kGunFire,
  kGunExplosion,
  kShell,
  kBomb,
  kRaft,
  kSmoke,
};
inline constexpr std::size_t kEffectSpriteCount = 13;

// The detail settings of the Alt-D dialog (0x170b24, default 0x7f).
struct ViewDetail {
  bool clouds = true;        // bit 0: sky panorama, else a plain sky colour
  bool waves = true;         // bit 1: textured sea, else flat distance bands
  bool land_texture = true;  // bit 2: textured terrain, else flat shades
  std::int32_t visibility = 15;  // bits 3-7: terrain range in grid cells (10..30)
};

// Everything the view reads from files, loaded once per mission.
struct WorldViewAssets {
  Environment env;
  SeaState sea_state = SeaState::kCalm;
  Image8 sky;              // the 1280 x 65 panorama of the `.ENV`
  Image8 sea_texture;      // SEA.PCC, 128 x 128, texels minus 80 (0..15)
  Image8 terrain_texture;  // TTEXT.PCC (square), texels 130..134
  std::optional<World> world;  // the patrol map: terrain grid and landmarks
  std::vector<MapObject> objects;      // the `.OBS` entries, by location type
  std::vector<Shp> object_sprites;     // their sprites
  std::array<Shp, kEffectSpriteCount> effects;
  // The terrain shade tables 0x1780c8[band 0..3][level 0..11]: texels
  // 130..134 map to ramp `band` entries level..level+4, the rest to 0.
  std::array<std::array<std::array<std::uint8_t, 256>, 12>, 4> terrain_shades{};

  // Loads `env_path` (e.g. "DATA/ENV-01.ENV") for `state`, ART/SEA.PCC,
  // ART/TTEXT.PCC and the effect sprites, plus the map `world_path` and its
  // objects `objects_path` when not empty. Throws `DataError` on bad files.
  static WorldViewAssets load(const Install& install, std::string_view env_path, SeaState state,
                              std::string_view world_path = {}, std::string_view objects_path = {});

  const Shp& effect(EffectSprite s) const { return effects[static_cast<std::size_t>(s)]; }
};

// One frame's view: the camera, the visible objects and the settings.
struct Scene {
  Angle10 heading;     // view direction
  std::int32_t x = 0;  // eye position in world units (y = eye height)
  std::int32_t y = 0;
  std::int32_t z = 0;
  Fixed16 zoom = Fixed16::from_int(1);  // periscope / TBT / gun magnification
  int screen_width = kWorldViewWidth;   // 640 bridge, 201 scope and gun, 291 TBT
  ViewDetail detail;
  // The sea texture's drift in 16.16 texels along x and z (0x170f6c/70),
  // advanced by `sea_drift_step` each frame.
  std::int32_t sea_drift = 0;
  // The vehicles and effects that passed their own visibility tests
  // (vtable +0x14), in the order the simulation adds them.
  std::vector<RenderObject> objects;
};

// Returns the sea texture drift of one frame of `ticks` timer ticks at the
// `.SCN` sea state `sea_state` (0x125f64), as the original's per-frame update
// (0x7b394) adds it: (1 calm, else 5) x (state + 1) x 0xdd3 x ticks / 100.
std::int32_t sea_drift_step(std::int32_t sea_state, std::int32_t ticks);

// Returns a blank 640 x 260 frame for `render_world_view`.
inline Image8 make_world_view_frame() { return Image8(kWorldViewWidth, kWorldViewHeight); }

// Returns the 200-row window of the frame a station shows: rows `bob` to
// `bob + 199`, where `bob` is the swell offset (30 on a calm sea).
inline Viewport world_view_window(Image8& frame, int bob) { return {&frame, 0, bob, kWorldViewWidth - 1, bob + 199}; }

// Renders `scene` into the frame of `vp` (whose origin is the frame's
// top-left; at least 640 x 260): sky, sun and moon, stars at night, sea,
// terrain and objects, far to near. `lighting` gives the sky bodies and the
// night flag; `random` is the game's generator (shell tracers draw from it).
// Returns the projected render list, whose entry owners point into
// `scene.objects` (none for the map's landmarks).
RenderList render_world_view(const Viewport& vp, const Scene& scene, const WorldViewAssets& assets,
                             const WorldLighting& lighting, WatcomRandom& random);

}  // namespace sh
