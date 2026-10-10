// The state-file layouts of the blocks several objects embed (state_io.h):
// the motion block (0x144 bytes: player 0x11a9c4, ship +0x228, enemy sub
// +0x1e4, torpedo +0xec), the dive block (0x24: player 0x11ab08, enemy sub
// +0x328), the routes (x[25], z[25], count), the effects block (0x38: ship
// +0x36c, enemy sub +0x9f4), the ballistic block (0x28) and the collision
// box (vehicle +0x04..+0xaf). `base` is the block's offset in the image.
#pragma once

#include <cstdint>

#include "core/error.h"
#include "sim/ballistic.h"
#include "sim/collision.h"
#include "sim/dive.h"
#include "sim/motion.h"
#include "sim/ship_combat.h"
#include "sim/state_io.h"

namespace sh {

template <class Io, class M>
void bind_motion(Io& io, std::uint32_t base, M& m) {
  io.vec3(base + 0x00, m.position);
  io.i64(base + 0x0c, m.pos_x);
  io.i64(base + 0x14, m.pos_y);
  io.i64(base + 0x1c, m.pos_z);
  io.vec3(base + 0x24, m.velocity);
  io.mat3(base + 0x30, m.orientation);
  io.mat3(base + 0x54, m.inverse);
  io.angle(base + 0x78, m.heading);
  io.i32(base + 0x7c, m.heading_rad);
  io.angle(base + 0x80, m.ordered_heading);
  io.angle(base + 0x84, m.pitch);
  io.i32(base + 0x88, m.pitch_rad);
  io.i32(base + 0x8c, m.power);
  io.i32(base + 0x90, m.ordered_power);
  io.fixed(base + 0x94, m.speed);
  io.i32(base + 0x98, m.speed_tenths);
  io.i32(base + 0x9c, m.rudder);
  io.flag(base + 0xa0, m.steering);
  io.i32(base + 0xa4, m.log_units);
  io.i32(base + 0xa8, m.log_miles);
  io.i32(base + 0xac, m.keel_clearance);
  io.vec3(base + 0xb0, m.acceleration);
  io.i32(base + 0xbc, m.wake_timer);
  io.i32(base + 0xc0, m.wake_count);
  for (std::uint32_t i = 0; i < MotionState::kMaxWakePoints; ++i) {
    io.vec3(base + 0xc4 + i * 12, m.wake[i]);
  }
  io.i32(base + 0x13c, m.wake_interval_s);
  io.i32(base + 0x140, m.rudder_max);
}

template <class Io, class D>
void bind_dive(Io& io, std::uint32_t base, D& d) {
  io.i32(base + 0x00, d.flood);
  io.i32(base + 0x04, d.depth_ft);
  io.i32(base + 0x08, d.ordered_depth_ft);
  io.i32(base + 0x0c, d.ballast);
  io.enumeration(base + 0x10, d.mode);
  io.i32(base + 0x14, d.planes);
  io.i32(base + 0x18, d.planes_angle);
  io.i32(base + 0x1c, d.air_pct);
  io.i32(base + 0x20, d.air_fraction);
}

// A route: x[25] at `x`, z[25] at `z`, the count at `count`. Reading
// throws `DataError` for a count outside 0..25.
template <class Io, class R>
void bind_route(Io& io, std::uint32_t x, std::uint32_t z, std::uint32_t count, R& r) {
  for (std::uint32_t i = 0; i < Route::kCapacity; ++i) {
    io.i32(x + i * 4, r.x[i]);
    io.i32(z + i * 4, r.z[i]);
  }
  io.i32(count, r.count);
  if constexpr (Io::kReading) {
    if (r.count < 0 || r.count > Route::kCapacity) {
      throw DataError("state lists: route count out of range");
    }
  }
}

// The waypoint index of a route at `off`; reading throws `DataError` for a
// negative one (an index at or past the count is a finished route).
template <class Io>
void bind_route_index(Io& io, std::uint32_t off, std::int32_t& index) {
  io.i32(off, index);
  if constexpr (Io::kReading) {
    if (index < 0) {
      throw DataError("state lists: negative route index");
    }
  }
}

template <class Io, class E>
void bind_effects(Io& io, std::uint32_t base, E& e) {
  io.i32(base + 0x00, e.bow_wave_frame);
  io.i32(base + 0x04, e.bow_wave_timer);
  io.flag(base + 0x08, e.burning);
  io.i32(base + 0x0c, e.fire_frame);
  io.i32(base + 0x10, e.fire_timer);
  io.flag(base + 0x14, e.sinking);
  io.i32(base + 0x18, e.sink_rate);
  io.i32(base + 0x1c, e.sink_timer);
  io.i32(base + 0x20, e.explosion_frame);
  io.i32(base + 0x24, e.explosion_sprite);
}

template <class Io, class B>
void bind_ballistic(Io& io, std::uint32_t base, B& b) {
  io.vec3(base + 0x00, b.position);
  io.vec3(base + 0x0c, b.origin);
  io.vec3(base + 0x18, b.velocity);
  io.i32(base + 0x24, b.time);
}

// The collision box at +0x04 (corners as x[8], y[8], z[8]).
template <class Io, class C>
void bind_box(Io& io, C& box) {
  io.i32(0x04, box.radius);
  io.mat3(0x08, box.axes);
  io.vec3(0x2c, box.centre);
  io.i32(0x38, box.x_max);
  io.i32(0x3c, box.x_min);
  io.i32(0x40, box.y_max);
  io.i32(0x44, box.y_min);
  io.i32(0x48, box.z_max);
  io.i32(0x4c, box.z_min);
  for (std::uint32_t i = 0; i < 8; ++i) {
    io.i32(0x50 + i * 4, box.corners[i].x);
    io.i32(0x70 + i * 4, box.corners[i].y);
    io.i32(0x90 + i * 4, box.corners[i].z);
  }
}

}  // namespace sh
