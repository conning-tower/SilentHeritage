#include "sim/tdc.h"

#include <algorithm>
#include <cstdlib>
#include <string>

#include "core/error.h"
#include "sim/enemy_sub.h"
#include "sim/geometry.h"
#include "sim/ship.h"
#include "sim/torpedo.h"
#include "sim/vehicle.h"
#include "sim/weapon_math.h"

namespace sh {

using fx::angle_sincos;
using fx::angle_sub;

namespace {

constexpr std::int32_t kTimerDropAfter = -0x3b;  // Run timers stop 59 s past the hit.

// Returns the WEAPONS.DAT torpedo of the selected type (`weapons_find`).
const Torpedo& selected_weapon(const GameTables& tables, const std::array<TorpedoChoice, 3>& choices,
                               std::int32_t index) {
  const Torpedo* w = tables.find_torpedo_by_id(choices.at(static_cast<std::size_t>(index)).weapon_id);
  if (w == nullptr) {
    throw DataError("TDC torpedo type " + choices.at(static_cast<std::size_t>(index)).name + " not in WEAPONS.DAT");
  }
  return *w;
}

// Returns the speed setting used with `w` (0 when it has fewer settings).
std::size_t setting_for(const Torpedo& w, std::int32_t setting) {
  return setting >= static_cast<std::int32_t>(w.speed_settings) ? 0 : static_cast<std::size_t>(setting);
}

std::int32_t speed_kts(const Torpedo& w, std::size_t setting) {
  return static_cast<std::int32_t>(setting == 0 ? w.speed_low_kts : w.speed_high_kts);
}

// Returns the launch point of the bow (offset > 0) or stern tubes.
geom::WorldXZ launch_point(const OwnBoat& own, std::int32_t offset) {
  const fx::Vec3 l = fx::transform(own.motion.orientation, {0, 0, offset});
  return {own.motion.position.x + l.x, own.motion.position.z + l.z};
}

}  // namespace

TargetState target_state(const Vehicle& target, const TargetSensing& sensing) {
  TargetState s;
  s.position = target.position();
  s.heading = target.heading();
  s.speed = target.speed();
  s.track_time = sensing.track_time;
  // ship_max_speed: atoi of the class's ASCII top speed; uncapped for
  // other vehicles and ships without a class. enemy_sub_max_speed
  // (0x19ba0): the submerged speed once its y is below 0.
  s.max_speed_kts = 0x7fff;
  if (const auto* ship = dynamic_cast<const Ship*>(&target)) {
    if (const ShipClass* cls = ship->setup().ship_class) {
      s.max_speed_kts = std::atoi(cls->top_speed_kts.c_str());
    }
  } else if (const auto* sub = dynamic_cast<const EnemySub*>(&target)) {
    if (const ShipClass* cls = sub->setup().ship_class) {
      const std::string& speed = sub->position().y < 0 ? cls->submerged_speed_kts : cls->top_speed_kts;
      s.max_speed_kts = std::atoi(speed.c_str());
    }
  }
  return s;
}

SolutionEstimate tdc_estimate_solution(const TargetState& target, std::int32_t sighting_range_yd,
                                       std::int32_t offset_x, std::int32_t offset_z, const OwnBoat& own) {
  // tdc_estimate_solution (0x7bd2e)
  SolutionEstimate e;
  const std::int32_t ox = own.motion.position.x + offset_x;
  const std::int32_t oz = own.motion.position.z + offset_z;
  const std::int32_t dx = target.position.x - ox;
  const std::int32_t dz = target.position.z - oz;
  e.bearing = angle_sub(fx::heading_from_delta(dx, dz), own.motion.heading);
  const std::int32_t far = sighting_range_yd - sighting_range_yd / 7;
  e.range = static_cast<std::int32_t>(fx::approx_distance(dx, dz));
  std::int32_t beyond = e.range / kUnitsPerYard - sighting_range_yd / 7;
  if (beyond < 0) {
    beyond = 0;
  }
  if (far < beyond) {
    beyond = far;
  }
  std::int32_t floor_q = 100 - 35 * static_cast<std::int32_t>((own.options & 0xf) >> 2);
  if (floor_q < 0) {
    floor_q = 0;
  }
  e.quality = 0;
  if (far > 0) {
    const std::int32_t by_range = 100 - beyond * (100 - floor_q) / far;
    e.quality = std::max(by_range * 2 - 100, 0);
    e.quality += fx::trunc(target.track_time.raw()) * 100 / ((3 - own.crew) * 120);
    e.quality = std::min(e.quality, by_range);
    e.quality = std::max(e.quality, floor_q);
  }
  const std::int32_t error = (100 - e.quality) * 2 / 10;
  e.course = fx::angle_add(target.heading, Angle10(error * 450 / 100));
  std::int32_t speed = target.speed.raw();
  speed += fx::imul(speed, error) / 100;
  if (fx::imul(target.max_speed_kts, 0x10000) < speed) {
    speed = fx::imul(target.max_speed_kts, 0x10000);
  }
  e.speed = Fixed16::from_raw(speed);
  e.speed_tenths = fx::trunc(fx::imul(speed, 10));
  e.range += fx::imul(e.range, error) / 100;
  // The estimated position: the range along the true bearing, scaled down
  // into 15 bits for point_rotate_scale.
  std::int32_t shift = 0;
  std::int32_t scaled = e.range;
  while (scaled > 0x7ffe) {
    scaled >>= 1;
    ++shift;
  }
  fx::Point2 p = fx::point_rotate_scale({0, scaled}, {0, 0}, fx::angle_add(e.bearing, own.motion.heading));
  for (std::int32_t i = 0; i < shift; ++i) {
    p.x <<= 1;
    p.y <<= 1;
  }
  e.x = ox - p.x;
  e.z = oz + p.y;
  return e;
}

void Tdc::reset() {
  // tdc_reset_state (0x7b047)
  mode_ = Mode::kAuto;
  target_ = nullptr;
  follow_bearing_ = false;
  locked_ = false;
  tracking_ = false;
  blink_ticks_ = 0;
  solution_ticks_ = 0;
  blink_ = false;
  quality_ = 0;
  timer_ticks_ = 0;
  depth_ft_ = 10;
  speed_setting_ = 1;
  spread_ = 0;
  set_target_position(0, 0);
  tracked_.fill(nullptr);
}

void Tdc::set_mode(Mode mode) {
  // tdc_set_mode (0x7c76c)
  mode_ = mode;
  if (mode == Mode::kOff) {
    locked_ = false;
    gyro_ = {};
    return;
  }
  if (mode == Mode::kManual) {
    locked_ = false;
  }
}

void Tdc::mark_target(const OwnBoat& own, Angle10 view_bearing, const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                      const TargetSensingFn& sensing) {
  // tdc_mark_target (0x7c83e)
  if (mode_ == Mode::kManual) {
    bearing_ = view_bearing;
    tracking_ = false;
    const fx::SinCos sc = angle_sincos(fx::angle_add(view_bearing, own.motion.heading));
    std::int32_t r = fx::imul(range_yd_, 0x10000);
    const bool far = range_yd_ > 30000;
    if (far) {
      r >>= 2;
    }
    const std::int32_t yards_per_step = far ? kUnitsPerYard * 4 : kUnitsPerYard;
    const std::int32_t dx = fx::trunc(fx::mul(r, sc.sin)) * yards_per_step;
    const std::int32_t dz = fx::trunc(fx::mul(r, sc.cos)) * yards_per_step;
    set_target_position(dx + own.motion.position.x, dz + own.motion.position.z);
    return;
  }
  if (mode_ == Mode::kAuto) {
    if (locked_) {
      locked_ = false;
      return;
    }
    pick_target_on_bearing(own, view_bearing, vehicles, sensing);
    if (target_ != nullptr) {
      locked_ = true;
    }
  }
}

void Tdc::pick_target_on_bearing(const OwnBoat& own, Angle10 view_bearing,
                                 const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                                 const TargetSensingFn& sensing) {
  // tdc_pick_target_on_bearing (0x7c4ac)
  if (locked_) {
    return;
  }
  target_ = nullptr;
  if (mode_ != Mode::kAuto) {
    return;
  }
  const std::int32_t wanted = fx::angle_add(own.motion.heading, view_bearing).tenths();
  Vehicle* best = nullptr;
  std::uint32_t best_yd = 0;
  for (const std::unique_ptr<Vehicle>& v : vehicles) {
    // Slot +0x54: only surface targets; the player (vehicle 0) is not one.
    if (!v->is_surface_target() || v->removed()) {
      continue;
    }
    const TargetSensing s = sensing ? sensing(*v) : TargetSensing{};
    if ((!s.spotted && (own.options & 0x80) != 0) || v->sinking() || !s.line_of_sight) {
      continue;
    }
    const fx::Vec3 p = v->position();
    const std::int32_t dx = p.x - own.motion.position.x;
    const std::int32_t dy = p.y - own.motion.position.y;
    const std::int32_t dz = p.z - own.motion.position.z;
    const std::int32_t brg = fx::heading_from_delta(dx, dz).tenths();
    const std::uint32_t yd = fx::approx_distance_3d(dx, dy, dz) / kUnitsPerYard;
    // The bearings are compared without wrapping at north.
    if (std::abs(brg - wanted) < 9 && (best == nullptr || yd < best_yd)) {
      best_yd = yd;
      best = v.get();
    }
  }
  if (best != nullptr) {
    target_ = best;
  }
}

void Tdc::toggle_bearing_follow(bool off) {
  // tdc_toggle_bearing_follow (0x7cc69)
  if (off) {
    follow_bearing_ = false;
    return;
  }
  if (locked_) {
    follow_bearing_ = !follow_bearing_;
  }
}

void Tdc::cycle_type(const std::array<TorpedoChoice, 3>& choices) {
  // tdc_cycle_torpedo_type (0x7ce18)
  do {
    ++type_index_;
    if (type_index_ >= 3) {
      type_index_ = 0;
    }
  } while (!choices[static_cast<std::size_t>(type_index_)].available);
}

std::int32_t Tdc::update(std::int32_t frame_ticks, std::int32_t compression, const OwnBoat& own,
                         const GameTables& tables, const std::array<TorpedoChoice, 3>& choices,
                         const TargetSensingFn& sensing) {
  // tdc_sim_update (0x7b394)
  if (target_ != nullptr) {
    const TargetSensing s = sensing ? sensing(*target_) : TargetSensing{};
    if (target_->sinking() || ((own.options & 0x80) != 0 && !s.spotted) || s.visual_height < 1) {
      target_ = nullptr;
      locked_ = false;
    }
  }
  if (follow_bearing_ && !locked_) {
    follow_bearing_ = false;
  }
  solution_ticks_ += frame_ticks;
  blink_ticks_ += frame_ticks / compression;
  timer_ticks_ += frame_ticks;
  // (The sky drift 0x170f6c/0x170f70 the original also advances here belongs
  // to the sky.)
  const std::int32_t seconds = timer_ticks_ / 100;
  if (seconds > 0) {
    for (std::size_t i = 0; i < kTrackedTorpedoes; ++i) {
      if (tracked_[i] != nullptr) {
        countdown_[i] -= seconds;
        if (countdown_[i] < kTimerDropAfter) {
          tracked_[i] = nullptr;
        }
      }
    }
    while (timer_ticks_ > 99) {
      timer_ticks_ -= 100;
    }
  }
  if (blink_ticks_ > 25) {
    blink_ticks_ = 0;
    blink_ = !blink_;
  }
  if (solution_ticks_ <= 4) {
    return -1;
  }
  const std::int32_t dt = fx::div(fx::imul(static_cast<std::int16_t>(solution_ticks_), 0x10000), 100 << 16);
  solution_ticks_ = 0;
  quality_ = 0;
  if (mode_ == Mode::kOff) {
    speed_tenths_ = 0;
    range_yd_ = 0;
    course_ = Angle10(0);
    bearing_ = Angle10(0);
    tracking_ = false;
    return -1;
  }
  if (mode_ == Mode::kManual) {
    advance_solution(dt, own, tables, choices);
    if (!tracking_) {
      return -1;
    }
  } else {
    speed_tenths_ = 0;
    range_yd_ = 0;
    course_ = Angle10(0);
    bearing_ = Angle10(0);
    gyro_ = {};
    tracking_ = false;
    if (target_ == nullptr) {
      quality_ = 0;
      solution_ticks_ = 0;
      return -1;
    }
    estimate_target(own, sensing);
  }
  tracking_ = true;
  const std::int32_t follow = follow_bearing_ ? bearing_.tenths() : -1;
  update_tube_solutions(own, tables, choices);
  return follow;
}

void Tdc::estimate_target(const OwnBoat& own, const TargetSensingFn& sensing) {
  // tdc_update_target_solution (0x7bc79)
  const TargetSensing s = sensing ? sensing(*target_) : TargetSensing{};
  const SolutionEstimate e = tdc_estimate_solution(target_state(*target_, s), sighting_range_, 0, 0, own);
  quality_ = e.quality;
  bearing_ = e.bearing;
  range_yd_ = e.range / kUnitsPerYard;
  course_ = e.course;
  speed_ = e.speed;
  speed_tenths_ = e.speed_tenths;
  set_target_position(e.x, e.z);
}

void Tdc::set_target_position(std::int32_t x, std::int32_t z) {
  target_x_ = x;
  target_z_ = z;
  target_x16_ = static_cast<std::int64_t>(x) << 16;
  target_z16_ = static_cast<std::int64_t>(z) << 16;
}

void Tdc::advance_solution(std::int32_t dt, const OwnBoat& own, const GameTables& tables,
                           const std::array<TorpedoChoice, 3>& choices) {
  // tdc_advance_solution (0x7bfae): dead reckoning of the dialled solution.
  speed_ = Fixed16::from_raw(fx::imul(speed_tenths_, 0x10000) / 10);
  if (speed_.raw() != 0) {
    const fx::SinCos sc = angle_sincos(course_);
    const std::int32_t v = fx::mul(speed_.raw(), kUnitsPerSecondPerKnot);
    target_x16_ += fx::mul(fx::mul(v, sc.sin), dt);
    target_z16_ += fx::mul(fx::mul(v, sc.cos), dt);
    target_x_ = static_cast<std::int32_t>(target_x16_ >> 16);
    target_z_ = static_cast<std::int32_t>(target_z16_ >> 16);
  }
  if (!tracking_) {
    return;
  }
  const std::int32_t dx = target_x_ - own.motion.position.x;
  const std::int32_t dz = target_z_ - own.motion.position.z;
  range_yd_ = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
  bearing_ = angle_sub(fx::heading_from_delta(dx, dz), own.motion.heading);
  const Torpedo& w = selected_weapon(tables, choices, type_index_);
  const std::size_t s = setting_for(w, speed_setting_);
  std::int32_t offset = (own.length_ft / 2 + static_cast<std::int32_t>(w.length_ft)) * kUnitsPerFoot;
  const std::int32_t speed = speed_kts(w, s) << 16;
  const std::int32_t reach = w.reach_ft[s] << 16;
  for (std::int32_t tube = 0; tube < 2; ++tube) {
    if (tube == kStern) {
      offset = -offset;
    }
    const geom::WorldXZ l = launch_point(own, offset);
    run_time_[static_cast<std::size_t>(tube)] = tdc_round(fx::div(reach, fx::mul(speed, kFeetPerSecondPerKnot)));
    Angle10 heading = own.motion.heading;
    if (tube == kStern) {
      heading = fx::angle_add(heading, Angle10(1800));
    }
    add_intercept_time(tube, target_x_, target_z_, l.x, l.z, heading, speed);
  }
}

void Tdc::update_tube_solutions(const OwnBoat& own, const GameTables& tables,
                                const std::array<TorpedoChoice, 3>& choices) {
  // tdc_update_tube_solutions (0x7b622)
  const Torpedo& w = selected_weapon(tables, choices, type_index_);
  const std::size_t s = setting_for(w, speed_setting_);
  std::int32_t offset = (own.length_ft / 2 + static_cast<std::int32_t>(w.length_ft)) * kUnitsPerFoot;
  const std::int32_t speed = speed_kts(w, s) << 16;
  const std::int32_t reach = w.reach_ft[s] << 16;
  const std::int32_t reach_units = w.reach_ft[s] << 20;
  for (std::int32_t tube = 0; tube < 2; ++tube) {
    const auto t = static_cast<std::size_t>(tube);
    run_time_[t] = 0;
    if (tube == kStern) {
      offset = -offset;
    }
    geom::WorldXZ l = launch_point(own, offset);
    // The torpedo first runs its reach straight along the tube axis.
    const std::int32_t reach_time = fx::div(reach, fx::mul(speed, kFeetPerSecondPerKnot));
    run_time_[t] = tdc_round(reach_time);
    Angle10 heading = own.motion.heading;
    if (tube == kStern) {
      heading = fx::angle_add(heading, Angle10(1800));
    }
    const fx::SinCos axis = angle_sincos(heading);
    l.x += fx::trunc(fx::mul(reach_units, axis.sin));
    l.z += fx::trunc(fx::mul(reach_units, axis.cos));
    // ... while the target moves on (feet per second x 16).
    const fx::SinCos crs = angle_sincos(course_);
    const std::int32_t vt = fx::mul(speed_.raw(), kFeetPerSecondPerKnot);
    const std::int32_t tx = target_x_ + fx::trunc(fx::imul(fx::mul(fx::mul(vt, crs.sin), reach_time), 16));
    const std::int32_t tz = target_z_ + fx::trunc(fx::imul(fx::mul(fx::mul(vt, crs.cos), reach_time), 16));
    const Angle10 brg = fx::heading_from_delta(tx - l.x, tz - l.z);
    // Lead: asin(Vt sin(bearing - course) / Vtorpedo).
    const fx::SinCos aob = angle_sincos(Angle10(-angle_sub(course_, brg).tenths()));
    const std::int32_t lead = fx::fixed_asin(fx::div(fx::mul(speed_.raw(), aob.sin), speed));
    Angle10 gyro = angle_sub(angle_sub(brg, own.motion.heading), Angle10(lead));
    if (tube == kStern) {
      gyro = fx::angle_add(gyro, Angle10(1800));
    }
    gyro_[t] = gyro;
    add_intercept_time(tube, tx, tz, l.x, l.z, heading, speed);
  }
}

void Tdc::add_intercept_time(std::int32_t tube_end, std::int32_t tx, std::int32_t tz, std::int32_t lx, std::int32_t lz,
                             Angle10 tube_heading, std::int32_t torpedo_speed) {
  // tdc_tube_intercept_time (0x7bad7): the time at which the torpedo's and
  // the target's x (or z, when x is equal) coordinates meet.
  const auto t = static_cast<std::size_t>(tube_end);
  const fx::SinCos target_axis = angle_sincos(course_);
  const fx::SinCos torpedo_axis =
      angle_sincos(fx::angle_add(tube_heading, Angle10(gyro_[t].tenths() + spread_angle().tenths())));
  std::int32_t target_c = tx;
  std::int32_t launch_c = lx;
  std::int32_t target_f = target_axis.sin;
  std::int32_t torpedo_f = torpedo_axis.sin;
  if (tx == lx) {
    target_c = tz;
    launch_c = lz;
    target_f = target_axis.cos;
    torpedo_f = torpedo_axis.cos;
  }
  const std::int32_t vt = fx::mul(speed_.raw(), kFeetPerSecondPerKnot);
  const std::int32_t vp = fx::mul(torpedo_speed, kFeetPerSecondPerKnot);
  const std::int32_t closing = fx::mul(vt, target_f) - fx::mul(vp, torpedo_f);
  if (closing == 0) {
    run_time_[t] = -1;
    return;
  }
  run_time_[t] += fx::div((launch_c - target_c) / kUnitsPerFoot, closing);
}

Angle10 Tdc::spread_angle() const {
  return spread_ > 200 ? angle_sub(Angle10(spread_), Angle10(400)) : Angle10(spread_);
}

void Tdc::track_torpedo(std::int32_t tube_end, const TorpedoVehicle* torpedo) {
  // tdc_track_fired_torpedo (0x7cc89)
  const std::int32_t time = run_time_[static_cast<std::size_t>(tube_end)];
  for (std::size_t i = 0; i < kTrackedTorpedoes; ++i) {
    if (tracked_[i] == nullptr) {
      tracked_[i] = torpedo;
      countdown_[i] = time;
      return;
    }
  }
  if (time != -1) {
    for (std::size_t i = 0; i + 1 < kTrackedTorpedoes; ++i) {
      countdown_[i] = countdown_[i + 1];
      tracked_[i] = tracked_[i + 1];
    }
    tracked_[kTrackedTorpedoes - 1] = torpedo;
    countdown_[kTrackedTorpedoes - 1] = time;
  }
}

void Tdc::untrack_torpedo(const TorpedoVehicle* torpedo) {
  // tdc_untrack_torpedo (0x7cd03)
  for (std::size_t i = 0; i < kTrackedTorpedoes; ++i) {
    if (tracked_[i] == torpedo) {
      tracked_[i] = nullptr;
      return;
    }
  }
}

std::optional<std::int32_t> Tdc::shortest_run_timer() const {
  std::optional<std::int32_t> best;
  for (std::size_t i = 0; i < kTrackedTorpedoes; ++i) {
    if (tracked_[i] != nullptr && (!best || countdown_[i] < *best)) {
      best = countdown_[i];
    }
  }
  return best;
}

void Tdc::forget(const Vehicle& removed) {
  if (target_ == &removed) {
    target_ = nullptr;
  }
  // A torpedo untracks itself as it goes (torpedo_check_arming); this only
  // keeps the slots from outliving it otherwise.
  for (const TorpedoVehicle*& t : tracked_) {
    if (t != nullptr && static_cast<const Vehicle*>(t) == &removed) {
      t = nullptr;
    }
  }
}

}  // namespace sh
