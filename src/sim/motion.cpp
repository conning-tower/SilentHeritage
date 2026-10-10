#include "sim/motion.h"

#include "sim/geometry.h"

namespace sh {

using namespace geom;

namespace {

constexpr std::int32_t kSecondsPerHour = 3600;
constexpr std::int32_t kHorsepower16 = 550 << 16;        // ft-lbf/s per hp.
constexpr std::int32_t kPoundsPerTon16 = 2200 << 16;
constexpr std::int32_t kThrustSlewPerSecond = 1100000;   // Raw thrust units per second.
constexpr std::int32_t kMinDisplayedSpeed = -5 << 16;    // Knots.

// Sea states 0..2 at 0x10fa98: depth (ft) where the waves stop affecting a
// submarine.
constexpr std::array<std::int32_t, 3> kWaveDepthFt = {0, 80, 80};

// Returns speed (16.16 kn) in 16.16 world units per second.
std::int32_t knots_to_units(std::int32_t speed) {
  return fx::div(fx::mul(speed, kUnitsPerNauticalMile), kSecondsPerHour);
}

// Returns a forward velocity (16.16 units/s) in 16.16 knots.
std::int32_t units_to_knots(std::int32_t v) {
  return fx::div(fx::mul(v, kSecondsPerHour), kUnitsPerNauticalMile);
}

// Returns the tenths-of-a-knot display value of `speed`, floored at -5 kn.
std::int32_t speed_in_tenths(std::int32_t speed) {
  const std::int32_t s = speed < kMinDisplayedSpeed ? kMinDisplayedSpeed : speed;
  const std::int32_t t = fx::imul(s, 10);
  return fx::trunc(t);
}

// Returns the sea-state multiplier step: state 2 counts as 3.
std::int32_t sea_factor(std::int32_t sea_state) { return sea_state == 2 ? 3 : sea_state; }

// Returns `drag` increased by sea_state x pct % (32-bit arithmetic as the
// original, which wraps for very large drags).
std::int32_t apply_sea(std::int32_t drag, std::int32_t sea_state, std::int32_t wave_pct) {
  return drag + fx::imul(fx::imul(drag, sea_factor(sea_state)), wave_pct) / 100;
}

// Returns (c v)^3 + b v for v in 16.16 units/s, x the astern factor when
// `astern`.
std::int32_t hull_drag(const HullParams& hull, std::int32_t v, bool astern) {
  const std::int32_t cv = fx::mul(hull.drag_cubic.raw(), v);
  const std::int32_t cube = fx::mul(fx::mul(cv, fx::abs(cv)), fx::abs(cv));
  std::int32_t drag = cube + fx::mul(hull.drag_linear.raw(), v);
  if (astern) {
    drag = fx::mul(hull.astern_drag_factor.raw(), drag);
  }
  return drag;
}

}  // namespace

void motion_init(MotionState& m, const SeaFloor& floor) {
  if (m.power > 0) {
    m.power = fx::mul(m.power, kHorsepower16);
  }
  m.rudder = 0;
  m.wake_count = 0;
  m.wake_interval_s = 20;
  m.wake_timer = m.wake_interval_s << 16;
  m.rudder_max = 250;
  m.pos_x = static_cast<std::int64_t>(m.position.x) << 16;
  m.pos_y = static_cast<std::int64_t>(m.position.y) << 16;
  m.pos_z = static_cast<std::int64_t>(m.position.z) << 16;
  m.heading_rad = tenths_to_radians16(m.heading);
  m.pitch_rad = tenths_to_radians16(m.pitch);
  m.orientation = fx::mat3_from_euler(Angle10(-m.heading.tenths()), m.pitch, Angle10(0));
  m.inverse = fx::transpose(m.orientation);
  m.speed_tenths = speed_in_tenths(m.speed.raw());
  m.velocity = fx::transform(m.orientation, {0, 0, knots_to_units(m.speed.raw())});
  m.keel_clearance = floor.height_at(m.position.x, m.position.z);
}

void motion_step(MotionState& m, const HullParams& hull, std::int32_t sea_state, Fixed16 dt_s, std::int32_t wave_pct,
                 const SeaFloor& floor) {
  const std::int32_t dt = dt_s.raw();
  fx::Vec3 body = fx::transform(m.inverse, m.velocity);
  m.speed = Fixed16::from_raw(units_to_knots(body.z));
  m.speed_tenths = speed_in_tenths(m.speed.raw());

  // Resistance in the body frame: sideslip damping, vertical damping, drag.
  fx::Vec3 resistance;
  if (body.x != 0) {
    const Angle10 slip_angle = fx::heading_from_delta(fx::abs(body.x), fx::abs(body.z));
    const std::int32_t slip = fx::div(slip_angle.tenths() << 16, 250 << 16);  // Per 25 deg.
    resistance.x = fx::mul(fx::mul(hull.drag_cubic.raw(), fx::mul(hull.lateral_drag.raw(), slip)), -body.x);
  }
  resistance.y = -body.y;
  if (body.z != 0) {
    resistance.z = apply_sea(hull_drag(hull, -body.z, body.z < 0), sea_state, wave_pct);
  }
  const std::int32_t mass = fx::mul(hull.displacement, kPoundsPerTon16);

  // Thrust slews toward the ordered power.
  const std::int32_t ordered = fx::mul(m.ordered_power, kHorsepower16);
  const std::int32_t delta = ordered - m.power;
  if (delta != 0) {
    std::int32_t step = fx::mul(dt, kThrustSlewPerSecond) * fx::sign(delta);
    if (fx::abs(delta) < fx::abs(step)) {
      step = delta;
    }
    m.power += step;
  }
  fx::Vec3 thrust{0, 0, m.power};
  if (m.keel_clearance > 0) {
    // Aground: the boat stops and the thrust fades out over half a foot.
    resistance = {};
    m.velocity = {};
    body = {};
    std::int32_t grip = fx::div(static_cast<std::int16_t>(m.keel_clearance) << 16, 8 << 16);
    grip = grip > fx::kOne ? fx::kOne : (grip < 0 ? 0 : grip);
    thrust = thrust - fx::scale(thrust, grip);
  }
  m.acceleration = fx::scale(fx::divide(thrust + resistance, mass), 16 << 16);
  body = body + fx::scale(m.acceleration, dt);

  // Yaw: the forward speed vector turned by the rudder, its lateral part over
  // the yaw arm (half the length x turn factor).
  std::int32_t yaw = 0;
  if (m.keel_clearance < 1) {
    const fx::Point2 turned = fx::point_rotate_scale({0, fx::trunc(-body.z)}, {0, 0}, Angle10(m.rudder));
    const std::int32_t lateral = fx::mul(fx::imul(turned.x, 0x10000), dt);
    const std::int32_t arm = fx::mul(fx::imul(hull.length_ft, 0x10000) >> 1, hull.turn_factor.raw());
    yaw = fx::div(lateral, arm);
  }

  m.log_units += fx::trunc(fx::mul(body.z, dt));
  while (m.log_units >= kUnitsPerNauticalMile) {
    ++m.log_miles;
    m.log_units -= kUnitsPerNauticalMile;
  }
  m.velocity = fx::transform(m.orientation, body);

  std::int32_t rad = m.heading_rad + yaw;
  while (rad >= kTwoPi16) {
    rad -= kTwoPi16;
  }
  while (rad < 0) {
    rad += kTwoPi16;
  }
  m.heading_rad = rad;
  m.heading = radians16_to_tenths(rad);
  m.orientation = fx::mat3_from_euler(Angle10(-m.heading.tenths()), m.pitch, Angle10(0));
  m.inverse = fx::transpose(m.orientation);

  m.pos_x += fx::mul(m.velocity.x, dt);
  m.pos_y += fx::mul(m.velocity.y, dt);
  m.pos_z += fx::mul(m.velocity.z, dt);
  m.position = {static_cast<std::int32_t>(m.pos_x >> 16), static_cast<std::int32_t>(m.pos_y >> 16),
                static_cast<std::int32_t>(m.pos_z >> 16)};
  m.keel_clearance = floor.height_at(m.position.x, m.position.z) + hull.draft_ft * kUnitsPerFoot - m.position.y;

  if (m.steering) {
    // Rudder for the next step: 250 at 60 deg of error, reversed astern.
    std::int32_t error = fx::angle_diff(m.ordered_heading, m.heading);
    if (body.z < 0) {
      error = -error;
    }
    if (body.z == 0 || m.ordered_power == 0) {
      error = 0;
    }
    if (fx::abs(error) > 600) {
      error = fx::sign(error) * 600;
    }
    m.rudder = m.rudder_max * error / 600;
  }

  motion_update_wake(m, dt);
}

void motion_update_wake(MotionState& m, std::int32_t dt) {
  m.wake_timer += dt;
  if (m.wake_interval_s * 0x10000 <= m.wake_timer) {
    const auto drop_oldest = [&m] {
      for (int i = 0; i < m.wake_count - 1; ++i) {
        m.wake[static_cast<std::size_t>(i)] = m.wake[static_cast<std::size_t>(i + 1)];
      }
      --m.wake_count;
    };
    if (m.speed.raw() == 0) {
      if (m.wake_count != 0) {
        drop_oldest();
      }
    } else {
      if (m.wake_count == MotionState::kMaxWakePoints) {
        drop_oldest();
      }
      if (m.wake_count == 0 || !(m.position == m.wake[static_cast<std::size_t>(m.wake_count - 1)])) {
        m.wake[static_cast<std::size_t>(m.wake_count++)] = m.position;
      }
    }
    m.wake_timer = 0;
  }
}

std::int32_t motion_power_for_speed(Fixed16 speed, std::int32_t max_power, const HullParams& hull,
                                    std::int32_t sea_state, std::int32_t wave_pct) {
  const std::int32_t v = knots_to_units(speed.raw());
  const std::int32_t drag = apply_sea(hull_drag(hull, v, v < 0), sea_state, wave_pct);
  const std::int32_t power = fx::div(drag, kHorsepower16);
  return power > max_power ? max_power : power;
}

Fixed16 motion_speed_for_power(std::int32_t power, const HullParams& hull, std::int32_t sea_state,
                               std::int32_t wave_pct) {
  constexpr std::int32_t kStep = 0x199a;  // 0.1 kn.
  constexpr std::int32_t kPowerLimit = 30000;
  std::int32_t speed = 0;
  std::int32_t reached = 0;
  while (fx::abs(reached) < fx::abs(power) && reached < kPowerLimit) {
    speed += fx::sign(power) * kStep;
    reached = motion_power_for_speed(Fixed16::from_raw(speed), kPowerLimit, hull, sea_state, wave_pct);
  }
  return Fixed16::from_raw(speed);
}

void motion_steer_to_waypoint(MotionState& m, Fixed16 turn_factor, const Route& route, std::int32_t& index) {
  if (route.count == 0 || route.count == index) {
    return;
  }
  const auto i = static_cast<std::size_t>(index);
  const std::int32_t dx = route.x[i] - m.position.x;
  const std::int32_t dz = route.z[i] - m.position.z;
  m.ordered_heading = fx::heading_from_delta(dx, dz);
  m.steering = true;
  std::int32_t off = fx::angle_sub(m.ordered_heading, m.heading).tenths();
  if (off > 1800) {
    off = 3600 - off;
  }
  const auto range_yd = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
  if (range_yd < fx::trunc(turn_factor.raw()) * 850 / 25 * off / 1800 + 100) {
    ++index;
    if (index == route.count && index < Route::kCapacity && route.x[static_cast<std::size_t>(index)] == -1) {
      index = 0;
    }
  }
}

std::int32_t sub_wave_effect_pct(std::int32_t sea_state, std::int32_t depth_ft, std::int32_t surface_depth_ft) {
  if (sea_state == 0) {
    return 0;
  }
  if (depth_ft <= surface_depth_ft) {
    return 100;
  }
  const std::int32_t calm_depth = kWaveDepthFt.at(static_cast<std::size_t>(sea_state));
  if (calm_depth <= depth_ft) {
    return 0;
  }
  return 100 - (depth_ft - surface_depth_ft) * 100 / (calm_depth - surface_depth_ft);
}

}  // namespace sh
