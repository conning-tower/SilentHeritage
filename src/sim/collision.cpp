#include "sim/collision.h"

#include <algorithm>
#include <cstdlib>

#include "sim/geometry.h"
#include "sim/vehicle.h"

namespace sh {

namespace {

// The bottom edges clipped against the other rectangle (0x10fa6c).
constexpr std::array<std::array<std::size_t, 2>, 4> kBottomEdges = {{{0, 1}, {4, 5}, {0, 4}, {1, 5}}};

// Returns the outcode of (x, z) against the box rectangle: 1 beyond z_max,
// 2 below z_min, 4 beyond x_max, 8 below x_min.
std::uint32_t outcode(const CollisionBox& box, std::int32_t x, std::int32_t z) {
  std::uint32_t code = box.z_max < z ? 1u : 0u;
  if (z < box.z_min) {
    code |= 2;
  }
  if (box.x_max < x) {
    code |= 4;
  }
  if (x < box.x_min) {
    code |= 8;
  }
  return code;
}

// Returns `p` in the frame of `box`.
fx::Vec3 to_box_frame(const CollisionBox& box, const fx::Vec3& p) { return fx::transform(box.axes, p - box.centre); }

std::int64_t abs_diff(std::int32_t a, std::int32_t b) { return std::llabs(static_cast<std::int64_t>(a) - b); }

}  // namespace

CollisionBox make_hull_box(const fx::Vec3& position, const fx::Mat3& orientation, const fx::Mat3& inverse,
                           std::int32_t half_x, std::int32_t half_z, std::int32_t y_bottom, std::int32_t y_top,
                           std::int32_t radius) {
  CollisionBox box;
  const std::int32_t half_y = (y_top - y_bottom) / 2;
  box.radius = radius >= 0 ? radius : std::max({half_y, half_x, half_z});
  box.centre = {position.x, y_bottom + half_y, position.z};
  box.axes = inverse;
  box.x_max = half_x;
  box.x_min = -half_x;
  box.y_max = half_y;
  box.y_min = -half_y;
  box.z_max = half_z;
  box.z_min = -half_z;
  const fx::Vec3 a = fx::transform(orientation, {half_x, 0, half_z});
  const fx::Vec3 b = fx::transform(orientation, {-half_x, 0, half_z});
  const auto at = [&](std::int32_t dx, std::int32_t dz, std::int32_t y) {
    return fx::Vec3{position.x + dx, y, position.z + dz};
  };
  box.corners[0] = at(-a.x, -a.z, y_bottom);
  box.corners[1] = at(b.x, b.z, y_bottom);
  box.corners[2] = at(b.x, b.z, y_top);
  box.corners[3] = at(-a.x, -a.z, y_top);
  box.corners[4] = at(-b.x, -b.z, y_bottom);
  box.corners[5] = at(a.x, a.z, y_bottom);
  box.corners[6] = at(a.x, a.z, y_top);
  box.corners[7] = at(-b.x, -b.z, y_top);
  return box;
}

CollisionBox make_projectile_box(const fx::Vec3& position, Angle10 heading, std::int32_t half_x, std::int32_t half_y,
                                 std::int32_t half_z, std::int32_t radius) {
  const fx::Mat3 orientation = fx::mat3_from_euler(Angle10(-heading.tenths()), Angle10(0), Angle10(0));
  return make_hull_box(position, orientation, fx::transpose(orientation), half_x, half_z, position.y - half_y,
                       position.y + half_y, radius);
}

bool boxes_intersect(const CollisionBox& a, const CollisionBox& b) {
  // veh_boxes_intersect (0x7ea93)
  if (a.radius == 0 || b.radius == 0) {
    return false;
  }
  const std::int64_t reach = static_cast<std::int64_t>(a.radius) + b.radius;
  const std::int64_t dx = abs_diff(a.centre.x, b.centre.x);
  const std::int64_t dy = abs_diff(a.centre.y, b.centre.y);
  const std::int64_t dz = abs_diff(a.centre.z, b.centre.z);
  if (dx > reach || dy > reach || dz > reach ||
      fx::approx_distance_3d(static_cast<std::int32_t>(dx), static_cast<std::int32_t>(dy),
                             static_cast<std::int32_t>(dz)) > reach) {
    return false;
  }

  // b's corners in a's frame.
  std::array<fx::Vec3, 8> local{};
  std::array<std::uint32_t, 8> codes{};
  std::int32_t inside = 0;
  std::uint32_t common = 0;
  for (std::size_t i = 0; i < 8; ++i) {
    local[i] = to_box_frame(a, b.corners[i]);
    codes[i] = outcode(a, local[i].x, local[i].z);
    if (codes[i] == 0) {
      ++inside;
    }
    common = i == 0 ? codes[i] : (common & codes[i]);
  }
  // The vertical overlap compares corner 3 (top) and corner 0 (bottom).
  if (common != 0 || local[3].y < a.y_min || a.y_max < local[0].y) {
    return false;
  }
  if (inside != 0) {
    return true;
  }

  // b's bottom edges clipped against a's rectangle, two steps each.
  for (const auto& edge : kBottomEdges) {
    std::int32_t x0 = local[edge[0]].x;
    std::int32_t z0 = local[edge[0]].z;
    const std::int32_t x1 = local[edge[1]].x;
    const std::int32_t z1 = local[edge[1]].z;
    std::uint32_t code0 = codes[edge[0]];
    const std::uint32_t code1 = codes[edge[1]];
    for (int step = 2; (code1 & code0) == 0 && step != 0; --step) {
      const std::int32_t span_z = z1 - z0;
      const std::int32_t span_x = x1 - x0;
      std::int32_t nx = 0;
      std::int32_t nz = 0;
      if ((code0 & 1) != 0) {
        nz = a.z_max;
        nx = x0 + fx::imul(a.z_max - z0, span_x) / span_z;
      } else if ((code0 & 2) != 0) {
        nz = a.z_min;
        nx = x0 + fx::imul(a.z_min - z0, span_x) / span_z;
      } else if ((code0 & 4) != 0) {
        nx = a.x_max;
        nz = z0 + fx::imul(a.x_max - x0, span_z) / span_x;
      } else {
        nx = a.x_min;
        nz = z0 + fx::imul(a.x_min - x0, span_z) / span_x;
      }
      code0 = outcode(a, nx, nz);
      if (code0 == 0) {
        return true;
      }
      x0 = nx;
      z0 = nz;
    }
  }

  // a's corners in b's frame (horizontal test only).
  for (const fx::Vec3& corner : a.corners) {
    const fx::Vec3 p = to_box_frame(b, corner);
    if (outcode(b, p.x, p.z) == 0) {
      return true;
    }
  }
  return false;
}

void check_collisions(const std::vector<std::unique_ptr<Vehicle>>& vehicles, std::size_t index) {
  // veh_check_collisions (0x7e9b3)
  Vehicle& self = *vehicles[index];
  if (self.removed()) {
    return;
  }
  CollisionState& own = self.collision();
  for (std::size_t j = 0; j < vehicles.size(); ++j) {
    Vehicle& other = *vehicles[j];
    if (j == index || other.removed() || other.collision().collided) {
      continue;
    }
    CollisionState& theirs = other.collision();
    if (!boxes_intersect(own.box, theirs.box)) {
      if (own.partner == &other) {
        own.partner = nullptr;
      }
      if (theirs.partner == &self) {
        theirs.partner = nullptr;
      }
    } else {
      if (own.partner == nullptr) {
        own.collided = true;
        own.partner = &other;
      }
      if (theirs.partner == nullptr) {
        theirs.collided = true;
        theirs.partner = &self;
      }
    }
    if (own.collided) {
      return;
    }
  }
}

}  // namespace sh
