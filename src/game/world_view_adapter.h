// The adapter from the simulation to the 3D world view of the outside
// stations (bridge, periscope, TBT, deck gun): the eye of each station
// (`world_render_view`'s arguments in `bridge_render_view`,
// `scope_render_view`, `tbt_update` and `deckgun_update`), the scene of
// every vehicle as its vtable slot +0x14 adds it to the render list
// (`ship_add_render`, `enemy_sub_add_render`, `aircraft_add_render`,
// `airgroup_queue_draw`, `pilot_queue_draw`, `smoke_queue_draw`,
// `torpedo_add_wake_render`, `shell_queue_draw`...), the
// sky lighting, and the view state the stations share (the 640 x 260 frame,
// the sea drift and the shaded palette).
//
// Usage: build a `WorldViewAdapter` once per mission; each frame `advance`
// it, and when a station redraws call `render(eye)` and copy the station's
// window of `frame()` to the screen. After opening a station's PCX call
// `shade_palette` on its palette.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "assets/cat.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "core/fixed.h"
#include "core/image.h"
#include "core/random.h"
#include "sim/deck_gun.h"
#include "sim/player_sub.h"
#include "view/render_list.h"
#include "view/world_palette.h"
#include "view/world_view.h"

namespace sh {

class EnemySub;
class Install;
class Mission;
class Ship;
struct ShipClass;
struct ShipEffects;
struct MotionState;
struct World;

// The camera of one station view (`world_render_view(heading, x, y, z,
// zoom)` and `render_set_screen_width`).
struct ViewEye {
  Angle10 heading;     // true, own heading + the relative look bearing
  std::int32_t x = 0;  // world units
  std::int32_t y = 0;  // the eye's height (world y)
  std::int32_t z = 0;
  Fixed16 zoom = Fixed16::from_int(1);
  int screen_width = kWorldViewWidth;
};

// The rendered widths of the views.
inline constexpr int kBridgeViewWidth = 640;
inline constexpr int kScopeViewWidth = 201;
inline constexpr int kTbtViewWidth = 291;
inline constexpr int kDeckGunViewWidth = 201;
// The deck gun's eye height: 10 ft above the sea (y 0xa0), whatever the
// boat's trim.
inline constexpr std::int32_t kDeckGunEyeY = 0xa0;

// Returns the bridge's eye: the boat's heading plus `bearing`, from the
// bridge height (SUBS.DAT +176) above the boat, 1x, 640 wide.
ViewEye bridge_eye(const PlayerSub& sub, Angle10 bearing);
// Returns the periscope's eye: from the periscope height (SUBS.DAT +172),
// at `zoom`, 201 wide.
ViewEye scope_eye(const PlayerSub& sub, Angle10 bearing, Fixed16 zoom);
// Returns the TBT's eye: from the bridge height, at `zoom`, 291 wide.
ViewEye tbt_eye(const PlayerSub& sub, Angle10 bearing, Fixed16 zoom);
// Returns the deck gun's eye: along the boat's heading plus the mount's
// train, from the mount's position 10 ft above the sea, at `zoom`, 201 wide.
ViewEye deck_gun_eye(const PlayerSub& sub, const DeckGun::Mount& mount, Fixed16 zoom);

// The view options of the Alt-D detail dialog (GDETAIL.SAV).
struct WorldViewOptions {
  ViewDetail detail;          // 0x170b24
  bool torpedo_wakes = true;  // 0x129cfc
  bool ship_wakes = true;     // 0x129d00
};

// The 3D view state of a mission and its scene building.
class WorldViewAdapter {
 public:
  // Loads the view assets of `mission` (its `.ENV`, sea picture, map and
  // objects) and the ship and aircraft sprite archives. Throws `DataError`
  // on bad files.
  WorldViewAdapter(const Install& install, const Mission& mission);

  const WorldViewAssets& assets() const { return assets_; }
  WorldViewOptions& options() { return options_; }
  const WorldViewOptions& options() const { return options_; }
  WorldLighting& lighting() { return lighting_; }
  const WorldLighting& lighting() const { return lighting_; }

  // Returns the scene of `mission` seen from `eye`: every live vehicle the
  // original adds to the render list, in the vehicle list order. Ships and
  // submarines hidden by land, or not sighted with Limited Visibility, are
  // left out; aircraft, bomber groups and pilots only show once sighted.
  // A sighted bomber group's formation draws from `random` (the game's
  // generator, which it then reseeds as the original does).
  Scene build_scene(const Mission& mission, const ViewEye& eye, WatcomRandom& random);

  // Advances the sea drift by a frame of `timer_ticks` and the light timers
  // (`world_tick_light_cycle` runs at 100 Hz).
  void advance(std::int32_t timer_ticks, std::int32_t sea_state);

  // Sets the sun, moon, ship lights and fog of the mission's sky (`Sky`,
  // the original's `world_set_sun` / `world_set_moon` and fog 0x170f88);
  // returns true when the station palettes must be shaded again: the sky
  // changed since the last call (`0x129c5c`, which this takes).
  bool update_sky(Mission& mission);

  // Renders `eye`'s view of the mission into `frame()`
  // (`world_render_view`), with the game's generator for the tracers.
  // Returns the render list.
  RenderList render(const Mission& mission, const ViewEye& eye, WatcomRandom& random);
  // The 640 x 260 frame (bitmap 0x10fd68) and the camera of the last render.
  Image8& frame() { return frame_; }
  const Image8& frame() const { return frame_; }
  const ViewCamera& camera() const { return camera_; }
  const Scene& scene() const { return scene_; }

  // Shades the world ranges of a station palette loaded from its PCX
  // (`world_shade_screen_palette`); returns the darkening for the station's
  // overlay dimming (`dim_station_overlay`).
  Darkening shade_palette(VgaPalette& palette);

  // Returns the screen column in the view of `eye` of world point (`x`,
  // `z`) (`tdc_draw_target_marker`), or nothing when it is nearer than
  // the near plane (view z < 460).
  std::optional<int> project_x(const ViewEye& eye, std::int32_t x, std::int32_t z) const;
  // Returns the point of the world frame (column, row) in the view of
  // `eye` of world point `p` as `player_draw_recon_target_marker` projects
  // it: the column of `project_x`, the row 130 + y * 460 / z (the view's y
  // taken unnegated, unlike the renderer's), or nothing when it is nearer
  // than the near plane.
  std::optional<std::array<int, 2>> project_point(const ViewEye& eye, const fx::Vec3& p) const;

 private:
  // What `ship_render_cb` / `enemy_sub_render_cb` read of a hull.
  struct Hull {
    const ShipClass* cls = nullptr;
    const MotionState* motion = nullptr;
    const ShipEffects* effects = nullptr;
    std::int32_t distance_yd = -1;     // the player's sighting distance (-1 unseen)
    std::int32_t player_range_yd = 0;  // the player's lookout range to it
    std::int32_t hull_down_height_ft = 0;
    RenderType type = RenderType::kShip;
  };

  const Shp* ship_sprite(const std::string& name);
  const Shp* aircraft_sprite(const std::string& name);
  void add_hull(Scene& scene, const Mission& mission, const Hull& hull, const ViewEye& eye);
  void add_ship(Scene& scene, const Mission& mission, const Ship& ship, const ViewEye& eye);
  void add_enemy_sub(Scene& scene, const Mission& mission, const EnemySub& sub, const ViewEye& eye);

  WorldViewAssets assets_;
  CatArchive ships_cat_;
  std::map<std::string, Shp> ship_sprites_;
  CatArchive air_cat_;
  std::map<std::string, Shp> air_sprites_;
  WorldViewOptions options_;
  WorldLighting lighting_;
  VgaPalette base_palette_{};
  Image8 frame_ = make_world_view_frame();
  ViewCamera camera_;
  Scene scene_;
  std::int32_t sea_drift_ = 0;
};

}  // namespace sh
