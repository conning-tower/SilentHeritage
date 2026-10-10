// A depth charge (class 6, vtable 0x101edc), rolled off the stern racks or
// thrown by a K-gun (`depth_charge_*`, `dc_*`): it falls through the air,
// sinks at 10 ft/s and explodes at its set depth (or is lost on the bottom),
// leaving a splash and a blast noise. Only the player boat is hurt, by the
// distance in feet with the vertical distance counted twice: within
// 2000 ft it takes damage (`player_apply_system_damage(2, distance)`),
// below 300 ft for sure and below 30 ft possibly lethal.
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "sim/ballistic.h"
#include "sim/vehicle.h"

namespace sh {

class DepthCharge : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A charge at `position` launched along `heading` and `pitch` (a pitch
  // above 0 is a K-gun throw at 60 ft/s, else a rack drop), set to explode
  // at `depth_ft` (`depth_charge_spawn`, 0x860dc).
  DepthCharge(const fx::Vec3& position, Angle10 heading, Angle10 pitch, std::int32_t depth_ft);

  VehicleClass class_id() const override { return VehicleClass::kDepthCharge; }
  // `depth_charge_init` (0x86169).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 3; }
  std::int32_t max_time_compression() const override { return 0x40; }
  // `dc_update` (0x8627b).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return flight_.position; }
  // `dc_update_bounds` (0x867e7): a box of radius 0 (never collides).
  void update_bounds() override;

  std::int32_t depth_ft() const { return depth_ft_; }
  std::int32_t set_depth_ft() const { return set_depth_ft_; }
  // The splash animation frame on entering the water (-1 before, -2 after).
  std::int32_t splash_frame() const { return splash_frame_; }
  // The heading it was launched along (+0xf8), for the view.
  Angle10 flight_heading() const { return heading_; }

 private:
  bool check_detonation(SimContext& ctx);  // dc_check_detonation (0x8641d)
  void move(SimContext& ctx, std::int32_t dt);  // dc_move (0x862f5)

  Ballistic flight_;                 // +0xd0
  Angle10 heading_;                  // +0xf8
  Angle10 pitch_;                    // +0xfc
  std::int64_t y16_ = 0;             // +0x100
  std::int32_t set_depth_ft_ = 0;    // +0x108
  std::int32_t depth_ft_ = 0;        // +0x10c
  std::int32_t bottom_ft_ = 0;       // +0x110
  std::int32_t splash_frame_ = -1;   // +0x114
  std::int32_t splash_timer_ = 0;    // +0x118
};

// Spawns a depth charge (the escorts' `ai_ship_drop_depth_charges` pattern
// calls it per charge); returns it.
DepthCharge& spawn_depth_charge(SimContext& ctx, const fx::Vec3& position, Angle10 heading, Angle10 pitch,
                                std::int32_t depth_ft);

}  // namespace sh
