// Gun rounds (class 7, vtable 0x101fc8) and aerial bombs (class 8,
// 0x1020b4) on ballistic flights (`shell_*`, `bomb_*`).
//
// A shell hits what its box touches (other than its owner) for
// (caliber - 3) x 2 + rand(caliber / 4) points of kind 0, or kind 5 for the
// machine-gun and AA rounds of caliber 0, which only fly 3 s; it splashes
// where it comes down over water. A bomb explodes at its fuse depth, on
// contact or on the bottom, hurting what it hit (40 + rand(40)) or else
// the player boat by its distance (within 1000 ft).
#pragma once

#include <cstdint>

#include "core/fixed.h"
#include "sim/ballistic.h"
#include "sim/vehicle.h"

namespace sh {

class Shell : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A round of `caliber` inches (0: machine gun / AA) fired by `owner`
  // from `position` along `heading` at `elevation` with `speed_fps`
  // (`shell_spawn`, 0x86aed).
  Shell(const fx::Vec3& position, Angle10 heading, Angle10 elevation, std::int32_t speed_fps, std::int32_t caliber,
        Vehicle* owner);

  VehicleClass class_id() const override { return VehicleClass::kShell; }
  // `shell_init` (0x86c04): a gun sound for rounds heard by a surfaced boat.
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 0; }
  std::int32_t max_time_compression() const override { return 0x20; }
  // `shell_update` (0x86d0d).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return flight_.position; }
  // `shell_update_bounds` (0x870cb).
  void update_bounds() override;
  // `shell_drop_dead_owner`.
  void forget(const Vehicle& removed) override;

  std::int32_t caliber() const { return caliber_; }
  Vehicle* owner() const { return owner_; }
  // The heading it was fired along (+0x100), for the view.
  Angle10 flight_heading() const { return heading_; }

 private:
  Ballistic flight_;               // +0xd8
  Angle10 heading_;                // +0x100
  Angle10 elevation_;              // +0x104
  std::int32_t speed_fps_;         // +0x108
  std::int32_t caliber_;           // +0x10c
  Vehicle* owner_;                 // +0x110
  std::int32_t lifetime_ = 0;      // +0xd0, rounds of caliber 0.
};

class Bomb : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A bomb dropped by `owner` from `position` along `heading` at
  // `speed_fps`, fused for `fuse_depth_ft` (`bomb_spawn`, 0x874eb).
  Bomb(const fx::Vec3& position, Angle10 heading, std::int32_t speed_fps, std::int32_t fuse_depth_ft, Vehicle* owner);

  VehicleClass class_id() const override { return VehicleClass::kBomb; }
  // `bomb_init` (0x87575).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 0; }
  std::int32_t max_time_compression() const override { return 0x20; }
  // `bomb_update` (0x87672).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return flight_.position; }
  // `bomb_update_bounds` (0x87c6c).
  void update_bounds() override;
  // `bomb_drop_dead_owner`.
  void forget(const Vehicle& removed) override;

  std::int32_t depth_ft() const { return depth_ft_; }
  // The heading it was dropped along (+0x120) and the splash animation
  // frame on entering the water (-1 before), for the view.
  Angle10 flight_heading() const { return heading_; }
  std::int32_t splash_frame() const { return splash_frame_; }

 private:
  bool check_detonation(SimContext& ctx);       // bomb_check_detonation (0x87840)
  void move(SimContext& ctx, std::int32_t dt);  // bomb_move (0x876ec)

  Ballistic flight_;               // +0xd4
  std::int64_t y16_ = 0;           // +0x104
  std::int32_t fuse_depth_ft_;     // +0x10c
  std::int32_t depth_ft_ = 0;      // +0x110
  std::int32_t bottom_ft_ = 0;     // +0x114
  std::int32_t splash_frame_ = -1; // +0x118
  std::int32_t splash_timer_ = 0;  // +0x11c
  Angle10 heading_;                // +0x120
  std::int32_t speed_fps_;         // +0x124
  Vehicle* owner_;                 // +0x12c
};

}  // namespace sh
