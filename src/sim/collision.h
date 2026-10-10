// The vehicles' collision boxes (header fields +0x04..+0xac of every
// vehicle) and the pairwise test of `veh_check_collisions` /
// `veh_boxes_intersect`, run after each vehicle's update (slot +0x30
// rebuilds the box first).
//
// A box is an oriented rectangle in the horizontal plane with a vertical
// extent: its extents are relative to `centre` in the vehicle's frame, and
// its eight corners are in world units (0-1 and 4-5 the bottom edges, 2-3
// and 6-7 above them). A box with a radius of 0 never collides (depth
// charges, splashes, and torpedoes for their first 20 yards).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/fixed.h"
#include "core/fixed_math.h"

namespace sh {

class Vehicle;

struct CollisionBox {
  std::int32_t radius = 0;  // +0x04, 0: no collisions.
  fx::Mat3 axes;            // +0x08, world -> box frame.
  fx::Vec3 centre;          // +0x2c
  std::int32_t x_max = 0;   // +0x38
  std::int32_t x_min = 0;   // +0x3c
  std::int32_t y_max = 0;   // +0x40
  std::int32_t y_min = 0;   // +0x44
  std::int32_t z_max = 0;   // +0x48
  std::int32_t z_min = 0;   // +0x4c
  std::array<fx::Vec3, 8> corners{};  // +0x50 x[8], +0x70 y[8], +0x90 z[8].
};

// The collision state of a vehicle (+0xb0, +0xb4).
struct CollisionState {
  CollisionBox box;
  bool collided = false;       // Set on the first overlap with `partner`.
  Vehicle* partner = nullptr;  // The vehicle overlapped, until they separate.
};

// Returns the box of a hull at `position`: `half_x` and `half_z` the half
// beam and length, from `y_bottom` to `y_top` (world y), turned by
// `orientation` (body -> world) with `inverse` as its axes. The radius is
// the largest half extent, or `radius` when given (>= 0). This is the
// common layout of the ship, player, torpedo, shell and charge boxes.
CollisionBox make_hull_box(const fx::Vec3& position, const fx::Mat3& orientation, const fx::Mat3& inverse,
                           std::int32_t half_x, std::int32_t half_z, std::int32_t y_bottom, std::int32_t y_top,
                           std::int32_t radius = -1);

// Returns the box of a small projectile (depth charge, shell, bomb):
// `half` x `half_y` x `half_z` around `position`, turned by `heading`
// (`dc_update_bounds`, `shell_update_bounds`, `bomb_update_bounds`).
CollisionBox make_projectile_box(const fx::Vec3& position, Angle10 heading, std::int32_t half_x, std::int32_t half_y,
                                 std::int32_t half_z, std::int32_t radius);

// Returns whether the boxes overlap (`veh_boxes_intersect`, 0x7ea93): a
// radius pre-check, then `b`'s corners and bottom edges against `a`'s
// rectangle, then `a`'s corners against `b`'s.
bool boxes_intersect(const CollisionBox& a, const CollisionBox& b);

// Tests vehicle `index` against every other live vehicle not already
// collided this sub-step, setting `collided` and `partner` on both at the
// first overlap of a pair and clearing the partner once they separate
// (`veh_check_collisions`, 0x7e9b3).
void check_collisions(const std::vector<std::unique_ptr<Vehicle>>& vehicles, std::size_t index);

}  // namespace sh
