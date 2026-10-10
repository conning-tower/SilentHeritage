#include "view/world_palette.h"

#include <algorithm>
#include <cstdlib>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "core/fixed_math.h"

namespace sh {
namespace {

// Header offsets of the daylight parameters (0x16e167..0x16e16b).
constexpr std::size_t kAmbientMinimum = 0x1153;
constexpr std::size_t kTintRed = 0x1154;
constexpr std::size_t kSkyBodies = 0x1157;  // 0x81: no sun, moon or stars
constexpr std::uint8_t kNoSkyBodies = 0x81;

// The 6-bit colours read from SH.EXE: the three night-sky colours colour
// 128 cycles through (0x1105cc), then the ship-light colour of 44-47
// (0x1105d8).
struct NightColours {
  std::array<std::array<std::uint8_t, 3>, 3> sky{};
  std::array<std::uint8_t, 3> ship_light{};
};

const NightColours& night_colours() {
  static const NightColours colours = [] {
    constexpr std::uint32_t kSky = 0x1105cc;
    constexpr std::uint32_t kShipLight = 0x1105d8;
    const ExeImage& exe = game_exe();
    NightColours n;
    for (std::uint32_t i = 0; i < 9; ++i) {
      n.sky[i / 3][i % 3] = exe.u8(kSky + i);
      check_exe_table(n.sky[i / 3][i % 3] < 64, "night sky colour", kSky);
    }
    for (std::uint32_t c = 0; c < 3; ++c) {
      n.ship_light[c] = exe.u8(kShipLight + c);
      check_exe_table(n.ship_light[c] < 64, "ship light colour", kShipLight);
    }
    return n;
  }();
  return colours;
}

constexpr std::int32_t clamp_int(std::int32_t v, std::int32_t lo, std::int32_t hi) {
  if (v < lo) {
    v = lo;
  }
  if (v > hi) {
    v = hi;
  }
  return v;
}

std::uint8_t& channel(VgaPalette& p, int index, int c) {
  return p[static_cast<std::size_t>(index) * 3 + static_cast<std::size_t>(c)];
}

void copy_range(const VgaPalette& from, VgaPalette& to, const PaletteRange& r) {
  for (int i = r.first * 3; i < (r.last + 1) * 3; ++i) {
    to[static_cast<std::size_t>(i)] = from[static_cast<std::size_t>(i)];
  }
}

std::uint8_t darken(std::uint8_t c, std::int32_t percent) {
  return static_cast<std::uint8_t>(c - static_cast<std::uint8_t>(static_cast<std::int32_t>(c) * percent / 100));
}

}  // namespace

const std::array<PaletteRange, 6>& world_palette_ranges() {
  static const std::array<PaletteRange, 6> ranges = [] {
    constexpr std::uint32_t kAddress = 0x10febc;
    const auto v = exe_i32_array<12>(game_exe(), kAddress);
    std::array<PaletteRange, 6> r;
    for (std::size_t i = 0; i < r.size(); ++i) {
      r[i] = {v[2 * i], v[2 * i + 1]};
      check_exe_table(r[i].first >= 0 && r[i].first <= r[i].last && r[i].last < 256, "palette range", kAddress);
    }
    return r;
  }();
  return ranges;
}

VgaPalette world_base_palette(const Environment& env, SeaState state) {
  VgaPalette base{};
  env.apply_world_palette(std::span<std::uint8_t, 0x300>(base), state);
  return base;
}

std::uint8_t WorldLighting::fogged(std::uint8_t c, std::int32_t grey, std::int32_t limit) const {
  return static_cast<std::uint8_t>(clamp_int((grey - c) * fog_ / 100 + c, 0, limit));
}

Darkening WorldLighting::update_light(const Environment& env) {
  const std::int32_t sun = clamp_int(signed_elevation(sky_.sun_elevation) - 50, -200, 200);
  const std::int32_t moon = clamp_int(signed_elevation(sky_.moon_elevation) + 50, -100, 100);
  // The dawn/dusk tint fades out away from a sun elevation of 10 degrees.
  const std::int32_t tint_fade = std::abs((sun - 50) / 2);
  const std::int32_t dark = fx::angle_sub(Angle10(400), fx::angle_add(Angle10(sun), Angle10(200))).tenths() * 100 / 400;
  const std::int32_t moon_low = fx::angle_sub(Angle10(200), fx::angle_add(Angle10(moon), Angle10(100))).tenths();
  std::int32_t ambient = env.header[kAmbientMinimum];
  if (env.header[kSkyBodies] != kNoSkyBodies) {
    ambient += (100 - moon_low * 100 / 200) * (4 - sky_.moon_phase) * 6 / 100;
  }
  const std::int32_t max_dark = 100 - ambient;
  auto channel_dark = [&](std::size_t offset) {
    const std::int32_t tint = env.header[offset];
    return clamp_int(dark - (tint - tint * tint_fade / 100), 0, max_dark);
  };
  const Darkening d{channel_dark(kTintRed), channel_dark(kTintRed + 1), channel_dark(kTintRed + 2)};
  light_ = 100 - (d.r + d.g + d.b) / 3;
  return d;
}

Darkening WorldLighting::apply_daylight_palette(const Environment& env, VgaPalette& palette) {
  const std::int32_t sun = clamp_int(signed_elevation(sky_.sun_elevation) - 50, -200, 200);
  if (!night_ && sun < 7) {
    night_ = true;
    night_ticks_ = 0;
    for (int c = 0; c < 3; ++c) {
      channel(palette, kNightSkyIndex, c) =
          night_colours().sky[static_cast<std::size_t>(night_cycle_)][static_cast<std::size_t>(c)];
    }
  } else if (night_ && sun > 7) {
    night_ = false;
  }
  const Darkening d = update_light(env);

  for (std::size_t r = 0; r < world_palette_ranges().size(); ++r) {
    if (r == 5) {
      continue;  // the fire colours stay bright
    }
    const PaletteRange range = world_palette_ranges()[r];
    for (int i = range.first; i <= range.last; ++i) {
      if (i == kNightSkyIndex || (r == 2 && i >= kSelfLitFirst && i <= kSelfLitLast)) {
        continue;
      }
      channel(palette, i, 0) = darken(channel(palette, i, 0), d.r);
      channel(palette, i, 1) = darken(channel(palette, i, 1), d.g);
      channel(palette, i, 2) = darken(channel(palette, i, 2), d.b);
    }
  }

  if (ship_lights_) {
    for (int k = 0; k < 4; ++k) {
      for (int c = 0; c < 3; ++c) {
        std::uint8_t v = night_colours().ship_light[static_cast<std::size_t>(c)];
        if (k == 1) {
          v = static_cast<std::uint8_t>(v - flicker_);
        }
        channel(palette, kShipLightFirst + k, c) = v;
      }
    }
    flicker_ticks_ = 0;
  }

  if (fog_ != 0) {
    fog_ = std::min(fog_, 100);
    const std::int32_t grey = light_ * 63 / 100;
    for (int i = 0x10; i <= 0x9f; ++i) {
      const bool bright = i == kNightSkyIndex || (i >= kSelfLitFirst && i <= kSelfLitLast);
      const std::int32_t limit = bright ? grey * 5 : grey;
      for (int c = 0; c < 3; ++c) {
        channel(palette, i, c) = fogged(channel(palette, i, c), grey, limit);
      }
    }
  }
  skip_cycle_ = true;
  return d;
}

Darkening WorldLighting::shade_screen_palette(const Environment& env, const VgaPalette& base, VgaPalette& screen) {
  VgaPalette work{};
  const std::array<PaletteRange, 6>& ranges = world_palette_ranges();
  for (std::size_t r = 0; r < ranges.size(); ++r) {
    copy_range(r < 4 ? base : screen, work, ranges[r]);
  }
  const Darkening d = apply_daylight_palette(env, work);
  for (const PaletteRange& range : ranges) {
    copy_range(work, screen, range);
  }
  return d;
}

bool WorldLighting::cycle_lights(VgaPalette& palette) {
  bool changed = false;
  if (!skip_cycle_) {
    const std::int32_t grey = light_ * 63 / 100;
    if (night_ && night_ticks_ > 39) {
      night_ticks_ = 0;
      night_cycle_ = night_cycle_ >= 2 ? 0 : night_cycle_ + 1;
      for (int c = 0; c < 3; ++c) {
        std::uint8_t v = night_colours().sky[static_cast<std::size_t>(night_cycle_)][static_cast<std::size_t>(c)];
        if (fog_ != 0) {
          v = fogged(v, grey, grey * 5);
        }
        channel(palette, kNightSkyIndex, c) = v;
      }
      changed = true;
    }
    if (ship_lights_ && flicker_ticks_ > 19) {
      flicker_ += 3;
      flicker_ticks_ = 0;
      if (flicker_ > 50) {
        flicker_ = 1;
      }
      for (int c = 0; c < 3; ++c) {
        std::uint8_t v = static_cast<std::uint8_t>(night_colours().ship_light[static_cast<std::size_t>(c)] - flicker_);
        if (fog_ != 0) {
          v = fogged(v, grey, grey * 5);
        }
        channel(palette, kFlickerLightIndex, c) = v;
      }
      changed = true;
    }
  }
  skip_cycle_ = false;
  return changed;
}

void dim_station_overlay(VgaPalette& screen, const VgaPalette& original, const Darkening& dark) {
  const std::array<std::int32_t, 3> half = {dark.r >> 1, dark.g >> 1, dark.b >> 1};
  for (int i = 0xa0; i < 0x100; ++i) {
    if (i >= 0xe0 && i <= 0xe5) {
      continue;
    }
    for (int c = 0; c < 3; ++c) {
      const auto at = static_cast<std::size_t>(i) * 3 + static_cast<std::size_t>(c);
      screen[at] = darken(original[at], half[static_cast<std::size_t>(c)]);
    }
  }
}

}  // namespace sh
