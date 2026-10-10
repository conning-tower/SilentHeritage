// The 3D view's palette effects: the world palette ranges, the day/night
// darkening from the sun and moon elevations (`world_apply_daylight_palette`,
// 0x9140e), the station palette shading (`world_shade_screen_palette`,
// 0x91349), the ship lights and their timer-driven blinking (`world_set_camera`,
// `world_tick_light_cycle`), and the station overlay dimming hooks.
#pragma once

#include <array>
#include <cstdint>

#include "assets/environment.h"
#include "assets/pal.h"
#include "core/fixed.h"

namespace sh {

// The six world palette ranges (0x10febc), first..last inclusive. Ranges
// 0-3 come from the world base palette, 4-5 from the station's own; range
// 5 (64-74, fires) is never darkened.
struct PaletteRange {
  int first = 0;
  int last = 0;
};
// Returns the ranges, read from SH.EXE (`game_exe`) on first use.
const std::array<PaletteRange, 6>& world_palette_ranges();

// Palette indices with a fixed role at night: the star / night-sky colour
// (128, cycling through three blues), the self-lit colours 39-43, and the
// four ship-light colours 44-47 (45 flickers like a fire).
inline constexpr int kNightSkyIndex = 128;
inline constexpr int kSelfLitFirst = 39;
inline constexpr int kSelfLitLast = 43;
inline constexpr int kShipLightFirst = 44;
inline constexpr int kFlickerLightIndex = 45;

// The sun and moon as `world_set_sun` / `world_set_moon` receive them:
// azimuth (clockwise from north) and elevation in tenths of a degree (an
// elevation of 3500 is -10 deg), and the moon phase 0..4 (4 = no moon).
struct SkyBodies {
  Angle10 sun_azimuth;
  Angle10 sun_elevation;
  Angle10 moon_azimuth;
  Angle10 moon_elevation;
  std::int32_t moon_phase = 4;
};

// Returns elevation `e` in -1800..1800, as the original reads it back from
// the body's rotation matrix with `fixed_asin`.
constexpr std::int32_t signed_elevation(Angle10 e) {
  const std::int32_t t = e.normalized().tenths();
  return t > 1800 ? t - 3600 : t;
}

// Per-channel darkening in percent applied to the shaded ranges.
struct Darkening {
  std::int32_t r = 0;
  std::int32_t g = 0;
  std::int32_t b = 0;
};

// Returns the world base palette (0x170b28): entries 16-63 and 80-129 from
// the `.ENV` header and 130-159 from its `state` sea colours, the rest 0.
VgaPalette world_base_palette(const Environment& env, SeaState state);

// The lighting state the original keeps in globals: the night flag, the
// light-cycle timers and the derived light and fog percentages.
class WorldLighting {
 public:
  // Sets the sun and moon (the elevations are used as given; the original
  // round-trips them through a rotation matrix and `fixed_asin`).
  void set_sky(const SkyBodies& sky) { sky_ = sky; }
  const SkyBodies& sky() const { return sky_; }
  // Turns the four ship-light colours on or off (0x170f2c..: on while the
  // sun is below the horizon, `sky_update_sun_or_moon`).
  void set_ship_lights(bool on) { ship_lights_ = on; }
  bool ship_lights() const { return ship_lights_; }
  // Sets the fog blend in percent (0x170f88); values above 100 count as 100.
  void set_fog(std::int32_t percent) { fog_ = percent; }
  std::int32_t fog() const { return fog_; }

  // Returns the ambient light in percent (0x170f84) after the last update.
  std::int32_t light_percent() const { return light_; }
  // Returns true at night (0x16cf98): stars are drawn and colour 128 cycles.
  bool night() const { return night_; }

  // Computes the light percentage only (`world_apply_daylight_palette(0)`,
  // as `world_set_sun` / `world_set_moon` do); returns the darkening.
  Darkening update_light(const Environment& env);

  // Darkens the world ranges of `palette` for the current sky
  // (`world_apply_daylight_palette`): every range but 64-74, sparing 128 and
  // 39-43; sets the ship lights and blends in the fog. Also updates the
  // night flag. Returns the darkening for the station hooks.
  Darkening apply_daylight_palette(const Environment& env, VgaPalette& palette);

  // Shades the six world ranges of the station palette `screen`
  // (`world_shade_screen_palette`): ranges 0-3 from `base`
  // (`world_base_palette`), 4-5 from `screen` itself, darkened by
  // `apply_daylight_palette`; other entries are left as they are.
  Darkening shade_screen_palette(const Environment& env, const VgaPalette& base, VgaPalette& screen);

  // Advances the 100 Hz light timers by one tick (`world_tick_light_cycle`).
  void tick() {
    ++night_ticks_;
    ++flicker_ticks_;
  }

  // Steps the blinking colours when their timers are due, as
  // `world_set_camera` does each frame: at night colour 128 moves to the
  // next of its three blues every 40 ticks, and with the lights on colour 45
  // flickers every 20 ticks. Writes the changed entries into `palette` and
  // returns true if any changed.
  bool cycle_lights(VgaPalette& palette);

 private:
  // Returns `c` blended toward the fog grey and limited, as the fog pass.
  std::uint8_t fogged(std::uint8_t c, std::int32_t grey, std::int32_t limit) const;

  SkyBodies sky_;
  bool ship_lights_ = false;
  std::int32_t fog_ = 0;
  std::int32_t light_ = 100;
  bool night_ = false;
  std::int32_t night_ticks_ = 0;     // 0x16cfa0
  std::int32_t flicker_ticks_ = 0;   // 0x16cfa4
  std::int32_t night_cycle_ = 0;     // 0x17807c (0..2)
  std::int32_t flicker_ = 0;         // 0x178080
  bool skip_cycle_ = false;          // 0x170f3c: the palette was just rebuilt
};

// Dims the station overlay colours 160-255 (except 224-229) of `screen`
// from their station-PCX values `original` by half the darkening, as the
// bridge, deck gun and TBT palette hooks do (0x17e31, 0x376a3, 0x78498).
void dim_station_overlay(VgaPalette& screen, const VgaPalette& original, const Darkening& dark);

}  // namespace sh
