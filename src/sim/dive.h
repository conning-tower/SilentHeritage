// Submarine depth keeping (`sub_dive_state_init` 0x8c4cf, `sub_dive_update`
// 0x8c559): ballast moved toward per-state targets, buoyancy as a vertical
// drift, dive planes pitching the hull, the surfaced/submerged switch of the
// drag coefficients and the compressed-air reserve. Depths are in feet below
// the surface, measured at the keel; ballast is in tons x 100.
#pragma once

#include <array>
#include <cstdint>

#include "core/fixed.h"
#include "sim/motion.h"

namespace sh {

// The SUBS.DAT class fields used by the submarine motion and the own-boat
// systems (offsets within the 1704-byte record).
struct SubClassParams {
  std::int32_t length_ft = 0;              // +148
  std::int32_t beam_ft = 0;                // +152
  std::int32_t draft_ft = 0;               // +156, the keel depth surfaced.
  std::int32_t test_depth_ft = 0;          // +160
  std::int32_t periscope_depth_ft = 0;     // +164
  std::int32_t radar_depth_ft = 0;         // +168
  std::int32_t periscope_height_ft = 0;    // +172
  std::int32_t bridge_height_ft = 0;       // +176, also the depth where waves start to fade.
  std::int32_t awash_depth_ft = 0;         // +180
  std::int32_t noise_factor = 0;           // +184
  std::int32_t displacement_surfaced = 0;  // +208 (tons), the surfaced ballast.
  std::int32_t ballast_neutral = 0;        // +212 (tons), the submerged displacement: neutral buoyancy.
  std::int32_t ballast_max = 0;            // +216 (tons), +212 + 10: the flooding cap.
  std::int32_t diesel_hp = 0;              // +220, total of the diesels.
  std::int32_t motor_hp = 0;               // +224, total of the electric motors.
  Fixed16 drag_cubic_surfaced;             // +228
  Fixed16 drag_cubic_submerged;            // +232
  Fixed16 drag_linear_surfaced;            // +236
  Fixed16 drag_linear_submerged;           // +240
  Fixed16 lateral_drag;                    // +244
  Fixed16 astern_drag_factor;              // +248
  Fixed16 turn_factor;                     // +252
  Fixed16 pitch_factor;                    // +256, the planes' counterpart of turn_factor.
  std::int32_t diesel_count = 0;           // +260 (below 2, propelling diesels also charge).
  std::int32_t motor_count = 0;            // +264
  std::int32_t fuel_capacity = 0;          // +268
  Fixed16 fuel_use;                        // +272, fuel per second at full diesel power.
  std::array<std::int32_t, 11> power_surfaced{};   // +280, total engine % per telegraph step.
  std::array<std::int32_t, 11> power_submerged{};  // +324

  friend bool operator==(const SubClassParams&, const SubClassParams&) = default;
};

// The dive block (player 0x11ab08; enemy sub +0x328).
struct DiveState {
  enum class Mode : std::int32_t {
    kIdle = 0,       // Ballast left alone.
    kTrim = 1,       // Toward neutral buoyancy (minus the flood weight).
    kFlood = 2,      // Toward the maximum: diving.
    kPump = 3,       // Toward neutral - span: rising.
    kBlow = 4,       // Toward neutral - 6 span with compressed air.
    kSurface = 5,    // Toward the surfaced displacement.
  };

  std::int32_t flood = 0;                  // +0x00 flood weight (x 100).
  std::int32_t depth_ft = 0;               // +0x04
  std::int32_t ordered_depth_ft = -1;      // +0x08, -1 for none.
  std::int32_t ballast = 0;                // +0x0c tons x 100.
  Mode mode = Mode::kIdle;                 // +0x10
  std::int32_t planes = 0;                 // +0x14 ordered planes (+60 rise, -60 dive, +-80 blow/crash).
  std::int32_t planes_angle = 0;           // +0x18 tenths of a degree, pitches the hull.
  std::int32_t air_pct = 100;              // +0x1c compressed air.
  std::int32_t air_fraction = 0;           // +0x20 16.16 fraction of `air_pct`.
};

// Player-only inputs of the dive update (null for AI submarines).
struct PlayerDiveFactors {
  std::int32_t damaged_planes = 0;         // Bow and stern planes damaged: 0..2.
  std::int32_t sailing_model = 0;          // Realism bits 4-5: 0 novice .. 2 expert.
};

// Returns the hull block a submarine uses at its current depth and ballast.
HullParams sub_hull_params(const SubClassParams& sub, const DiveState& dive);

// Sets the dive block of a submarine at `motion.position.y`: depth (at least
// the draft), ordered depth there, ballast surfaced or neutral, full air.
void sub_dive_state_init(DiveState& dive, const MotionState& motion, const SubClassParams& sub);

// Advances depth keeping and motion by `dt` (16.16 s); calls motion_step.
void sub_dive_update(DiveState& dive, const SubClassParams& sub, MotionState& motion, Fixed16 dt,
                     const PlayerDiveFactors* player, std::int32_t sea_state, const SeaFloor& floor);

}  // namespace sh
