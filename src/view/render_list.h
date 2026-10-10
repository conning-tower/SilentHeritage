// The 3D view's camera and render list: `world_set_camera` (0x94716),
// `render_list_add` (0x91b08) and `render_list_project_entry` (0x91bc8).
// Objects are projected with the original's integer perspective (focal 920
// px horizontally, 460 px vertically, horizon on row 130) into a far-to-near
// list that the terrain pass and the per-type draw routines walk.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/fixed.h"
#include "core/fixed_math.h"

namespace sh {

class Shp;

// The world view frame the original renders into (0x12a7b0, bitmap
// 0x10fd68): 640 x 260 pixels, sky on rows 0..129 and sea on rows
// 130..259. Stations show a 200-row window of it (`world_view_window`).
inline constexpr int kWorldViewWidth = 640;
inline constexpr int kWorldViewHeight = 260;
inline constexpr int kHorizonRow = 130;
// Perspective constants: screen x = x * 920 / z + width / 2, screen y =
// 130 - y * 460 / z; the near plane is z = 460.
inline constexpr std::int32_t kFocalX = 920;
inline constexpr std::int32_t kFocalY = 460;
inline constexpr std::int32_t kNearZ = 460;
// Render-list entries outside view z 0x1cd..0x4073ff or more than 70 deg off
// the axis are culled.
inline constexpr std::int32_t kMinEntryZ = 0x1cd;
inline constexpr std::int32_t kMaxEntryZ = 0x4073ff;
inline constexpr std::size_t kMaxRenderEntries = 400;

// The camera of one rendering (`world_set_camera`).
struct ViewCamera {
  std::int32_t x = 0;  // world units (y shifted by the map scale)
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::int32_t heading = 0;          // tenths of a degree
  std::int32_t zoom = fx::kOne;      // 16.16, at least 1.0 (0x12a79c)
  std::int32_t inv_zoom = fx::kOne;  // 1 / zoom (0x12a7a0)
  // World to view rotation with its z row divided by the zoom (0x16cfcc),
  // and the inverse of the unzoomed rotation (0x16cff0).
  fx::Mat3 view;
  fx::Mat3 inverse;
  // The world direction of the screen's +x axis, 16.16 (0x12a7a4/a8).
  std::int32_t right_x = 0;
  std::int32_t right_z = 0;
  // The rendered width and its half (`render_set_screen_width`).
  std::int32_t screen_width = kWorldViewWidth;
  std::int32_t half_width = kWorldViewWidth / 2;
  std::uint32_t scale_shift = 0;  // the map's scale (0x170f24)
};

// Returns the camera for a view along `heading` from (x, y, z) at
// magnification `zoom` (below 1.0 counts as 1.0), `screen_width` pixels
// wide (640 bridge, 201 scope and deck gun, 291 TBT).
ViewCamera make_view_camera(Angle10 heading, std::int32_t x, std::int32_t y, std::int32_t z, Fixed16 zoom,
                            int screen_width, std::uint32_t scale_shift = 0);

// The render types: which draw routine an entry uses (0x10ff0c).
enum class RenderType : std::int32_t {
  kStaticObject = 0,    // billboard with N views around 360 deg (.OBS objects, smoke)
  kOrientedObject = 1,  // .OBS object with ship-like aspect views (moored submarine)
  kShip = 3,
  kEnemySub = 4,
  kSplash = 5,
  kTorpedoWake = 6,  // trail quads lightening the sea (not added for electric torpedoes)
  kShipWake = 7,
  kDepthCharge = 8,
  kShell = 9,
  kExplosion = 10,
  kAircraft = 11,
  kBomb = 12,
  kRaft = 13,
  kAirGroup = 14,  // one pixel per plane of a distant bomber group
};

// The sinking and fire state a ship draw reads (the vehicle's fx block,
// ship +0x36c, enemy sub +0x9f4, set up by `veh_fx_state_init`).
struct VehicleFx {
  std::int32_t wave_phase = 0;   // +0x00: bow-wave animation step (0..2)
  bool on_fire = false;          // +0x08
  std::int32_t fire_frame = 0;   // +0x0c: FIRE-0.SHP frame
  bool sinking = false;          // +0x14: draw the sinking views
  std::int32_t blast_frame = -1;  // +0x20: frame of the sinking explosion, -1 = none
  std::int32_t blast_sprite = 0;  // +0x24: 0 EXPL-0, 1 EXPL-1, 2 DC, 3 GUNFIRE, 4 GUNEXPL
};

// What `ship_render_cb` / `enemy_sub_render_cb` gather for
// `veh_draw_ship_sprite`: the class sprites and dimensions (SHIPS.DAT) and
// the vehicle's range and state.
struct ShipLook {
  const Shp* sinking_sprite = nullptr;  // class +0xf0 (NAMES.SHP: three sinking views)
  std::uint32_t fire_order = 0;        // class +0x68: bit 0/1 fire behind the hull in bow/stern views
  std::int32_t range_yards = 0;        // +0x494: range from the viewpoint
  std::int32_t hull_down_start_yards = 0;  // +0x490: the hull starts sinking under the horizon
  std::int32_t hull_down_end_yards = 0;    // +0x48c: fully hull down (0 = never)
  std::uint8_t role = 0;               // class +0x1a (SHIPS.DAT role; 3 small craft: no blast)
  std::int32_t speed_tenths = 0;       // +0x2c0: speed in tenths of a knot (bow wave)
  std::uint32_t bow_wave_size = 0;     // class +0x5c: 16.16 render size of the bow wave
  std::int32_t mast_height_ft = 0;     // class +0xbc
  std::int32_t fire_height_ft = 0;     // class +0xc0: height of the fire and bow wave
  std::int32_t hull_down_height_ft = 0;  // class +0xbc - +0xb8
  VehicleFx fx;
};

// A past position of a wake's owner, world units.
struct WakePoint {
  std::int32_t x = 0;
  std::int32_t z = 0;
};

// One object handed to the renderer (`render_list_add` arguments plus the
// owner fields its draw routine reads).
struct RenderObject {
  RenderType type = RenderType::kStaticObject;
  const Shp* sprite = nullptr;  // the sprite set (entry +8)
  std::int32_t x = 0;           // world units, y up
  std::int32_t y = 0;
  std::int32_t z = 0;
  Angle10 heading;
  // Render size, 16.16 units: the sprite scale is size / distance, so a
  // sprite is drawn 1:1 at `size >> 16` world units.
  std::uint32_t size = 0;
  // The `a` argument: drawn after the far objects when no terrain cell of
  // the view holds it (false only for the .WLD objects).
  bool draw_unplaced = true;
  // Splash / explosion: frame of `sprite`. Depth charge and bomb: frame of
  // SPOUT.SHP while splashing, -1 for the weapon itself (owner +0x114/+0x118).
  std::int32_t frame = -1;
  Angle10 pitch;  // aircraft +0x1d4
  Angle10 roll;   // aircraft +0x1d8
  ShipLook ship;  // ships and enemy submarines
  // Wakes: the owner's trail (position block +0xc4, oldest first, up to 10;
  // the head is the object's own position) and the full wake width in world
  // units (ship beam x 16, torpedo WEAPONS.DAT +0x50 x 16).
  std::vector<WakePoint> trail;
  std::int32_t wake_width = 0;
};

// A render-list record (0x48 bytes at 0x170ff0).
struct RenderEntry {
  RenderType type = RenderType::kStaticObject;
  const RenderObject* owner = nullptr;
  const Shp* sprite = nullptr;
  std::int32_t x = 0;  // world position, camera-relative once projected
  std::int32_t y = 0;
  std::int32_t z = 0;
  std::int32_t heading = 0;
  std::uint32_t size = 0;      // +0x1c, the sprite scale (16.16) once projected
  std::uint32_t distance = 0;  // +0x20: sort key on entry (0 or -1), then view distance
  std::int32_t view_x = 0;     // +0x24
  std::int32_t view_z = 0;     // +0x28
  std::int32_t screen_x = 0;   // +0x2c
  std::int32_t screen_y = 0;   // +0x30
  std::int32_t bearing = 0;    // +0x34: view-space bearing (0 straight ahead)
  std::int32_t cell = 0;       // +0x38: .WLD grid cell index
  bool drawn_by_cell = false;  // +0x3c
  bool draw_unplaced = false;  // +0x40
};

// The render list of one frame.
class RenderList {
 public:
  // Appends an entry (`render_list_add`); entries past the 400th are
  // dropped and `overflowed()` turns true. A `sort_key` of 0 sorts by view
  // distance; 0xffffffff (the original's wakes) skips the culling and the
  // projection and sorts first. `grid_width` and `scale_shift` give the
  // entry's `.WLD` cell index.
  void add(const RenderObject& object, std::uint32_t sort_key, std::int32_t grid_width, std::uint32_t scale_shift);

  // Projects every entry and builds the far-to-near order
  // (`render_list_project_all`); culled entries are left out of the order.
  void project_all(const ViewCamera& camera);

  const std::vector<RenderEntry>& entries() const { return entries_; }
  std::vector<RenderEntry>& entries() { return entries_; }
  // Indices into `entries()`, farthest first.
  const std::vector<std::size_t>& order() const { return order_; }
  bool overflowed() const { return overflowed_; }

 private:
  void project(std::size_t index, const ViewCamera& camera);

  std::vector<RenderEntry> entries_;
  std::vector<std::size_t> order_;
  bool overflowed_ = false;
};

// Returns the sprite scale `size / (distance << scale_shift)` of a
// projected entry. Unlike the original's 32-bit shift, which can wrap the
// divisor to 0, the divisor is computed in 64 bits and is at least 1.
std::uint32_t projected_size(std::uint32_t size, std::uint32_t distance, std::uint32_t scale_shift);

// Returns the frame of the aspect-view table 0x10ff54 for the entry's
// relative bearing (`render_view_aspect_frame`, 0x94387): 0 bow-on, 22
// stern-on, up to 44 on the other side (draws mirror frames past the
// sprite's count).
std::int32_t render_view_aspect_frame(const RenderEntry& entry);

}  // namespace sh
