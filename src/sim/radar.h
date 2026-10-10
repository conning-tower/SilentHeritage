// The player's radars (docs/structures.md "Radar"): the SJ surface search set (or the
// improved SJ, or the ST periscope radar) with its rotating beam, and the SD
// air search set (`radar_init` 0x660a7, `radar_update` 0x661f5,
// `sj_radar_sweep` 0x66210, `sj_radar_land_echoes` 0x6677d, `sd_radar_sweep`
// 0x66c5d, `sj_radar_set_power` 0x66b27, `sd_radar_set_power` 0x67048).
//
// Echo strength = RCS (slot +0x68, 16.16 raw) / max(1, 4 x range / range
// max); a blip needs more than 0.1. Blips live 8 sweeps (SD: one frame).
#pragma once

#include <cstdint>
#include <vector>

#include "core/fixed.h"
#include "sim/context.h"
#include "sim/sensor_environment.h"

namespace sh {

class PlayerSub;

// A radar echo (0x24 bytes).
struct RadarBlip {
  // +4: the vehicle's index in the list; -(row x width + column) for a land
  // cell; 100 + n x index for the extra planes of an SD group.
  std::int32_t source = 0;
  Angle10 bearing;              // +8, relative to the boat's heading (SJ).
  std::int32_t range_yd = 0;    // +0xc
  std::int32_t strength = 0;    // +0x10, 16.16.
  std::int32_t age = 0;         // +0x14, sweeps left.
  std::int32_t random = 0;      // +0x20, 0..3599 (the PPI's jitter).
};

// The SJ set's mode (`0x125558`).
enum class SjMode : std::int32_t {
  kOff = 0,
  kAScope = 1,
  kPpi = 2,
};

// Returns the SJ range of the fit in options bits 16-17: 0, 15000 yd, or
// 35000 yd for the improved SJ and the ST (`sj_radar_fitted_range`).
std::int32_t sj_fitted_range(std::uint32_t options);
// Returns the SD range: 17600 yd when fitted (`sd_radar_fitted_range`).
std::int32_t sd_fitted_range(std::uint32_t options);

class RadarSets {
 public:
  // The SJ set (`0x125558..0x12558c`, `0x1255c4..0x1255d4`).
  struct Sj {
    SjMode mode = SjMode::kOff;
    std::int32_t beam = 0;        // 0x12555c, 16.16 tenths relative to the bow.
    std::int32_t scale = 4;       // 0x125560, display range 2500 << scale yd.
    bool hold = false;            // 0x125564, the beam trained by hand.
    std::int32_t nearest_yd = 0;  // 0x125568, the nearest echo (10 yd steps), 0 none.
    std::vector<RadarBlip> blips;  // 0x12557c
    bool auto_armed = false;      // 0x1255c4
    bool last_auto = false;       // 0x1255d0
    std::int32_t power_timer = 0; // 0x1255d4, 16.16 s since switched.
  };
  // The SD set (`0x12559c..0x1255b0`, `0x1255c8..0x1255d8`).
  struct Sd {
    bool on = false;              // 0x12559c
    std::int32_t scale = 4;       // 0x1255a0
    std::int32_t nearest_yd = 0;  // 0x1255a4
    std::vector<RadarBlip> blips;  // 0x1255a8
    bool auto_armed = false;      // 0x1255c8
    bool last_auto = false;       // 0x1255cc
    std::int32_t power_timer = 0; // 0x1255d8
  };
  // The operator's pending reports, read by the contact reports.
  struct Reports {
    std::int32_t sj_bearing = -1;      // 0x11a8b0, degrees relative, -1 none.
    std::int32_t sd_range = -1;        // 0x11a8b4, yards, -1 none.
    std::int32_t sj_timer = 0x3c0000;  // 0x11a8b8, 16.16 s since the last SJ report.
    std::int32_t sd_timer = 0x3c0000;  // 0x11a8bc, never grows (the SD reports once).
  };

  // Resets both sets, marks an unfitted set destroyed in the boat's damage
  // state and switches the fitted ones on without a report (`radar_init`).
  void init(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env);
  // Sweeps for `dt` (16.16 s): SJ then SD (`radar_update`).
  void update(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env, std::int32_t dt);
  // Switches the SJ (`sj_radar_set_power`): on (PPI mode when `ppi` and the
  // scope is fitted), `release_hold` frees the beam, `toggle` inverts the
  // current state instead. Refused below the radar depth (periscope depth
  // for the ST). Reports #43/#44 (ST #47/#48) when turned on or off.
  void set_sj_power(SimContext& ctx, const PlayerSub& player, bool on, bool ppi, bool release_hold, bool toggle);
  // Switches the SD (`sd_radar_set_power`), reports #45/#46.
  void set_sd_power(SimContext& ctx, const PlayerSub& player, bool on, bool toggle);

  const Sj& sj() const { return sj_; }
  Sj& sj() { return sj_; }
  const Sd& sd() const { return sd_; }
  Sd& sd() { return sd_; }
  Reports& reports() { return reports_; }
  const Reports& reports() const { return reports_; }

 private:
  void sj_sweep(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env, std::int32_t dt);
  void sj_land_echoes(SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env);
  void sd_sweep(SimContext& ctx, PlayerSub& player, const SensorEnvironment& env);

  Sj sj_;
  Sd sd_;
  Reports reports_;
  bool quiet_ = false;  // 0x10ccb4: no crew report when switching.
};

}  // namespace sh
