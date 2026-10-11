#include "sim/torpedo.h"

#include <cstdlib>
#include <memory>
#include <tuple>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "sim/ai_ship.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kDepthRate = 0xa00000;    // 10 ft/s in 16.16 world units.
constexpr std::int32_t kArmingRun = 0x2580;      // 200 yd.
constexpr std::int32_t kPrematureRun = 0x3840;   // 300 yd.
constexpr std::int32_t kBoxRun = 0x3c0;          // 20 yd.
constexpr std::int32_t kMsgDidNotArm = 0x12;
constexpr std::int32_t kMsgDud = 0x13;
constexpr std::int32_t kMsgPremature = 0x14;
constexpr std::int32_t kSfxHitFar = 0x17;        // Heard submerged.
constexpr std::int32_t kSfxHitNear = 0x18;       // Heard at the surface (half volume).

}  // namespace

const TorpedoFixDates& torpedo_fix_dates() {
  static const TorpedoFixDates dates = [] {
    constexpr std::uint32_t kAddress = 0x10e3c0;
    const auto v = exe_i32_array<18>(game_exe(), kAddress);
    std::array<FaultDate, 6> d;
    for (std::size_t i = 0; i < d.size(); ++i) {
      d[i] = {v[3 * i], v[3 * i + 1], v[3 * i + 2]};
      check_exe_table(d[i].month >= 0 && d[i].month < 12 && d[i].day >= 1 && d[i].day <= 31, "torpedo fix date",
                      kAddress);
    }
    return TorpedoFixDates{d[0], {d[1], d[2], d[3], d[4]}, d[5]};
  }();
  return dates;
}

bool before_fix(const GameClock& clock, const FaultDate& date, const MissionRules& rules) {
  if (rules.fix_obvious_errors) {
    // Not in the original: until the fix's day.
    return std::tuple(clock.year(), clock.month(), clock.day()) < std::tuple(date.year, date.month, date.day);
  }
  return clock.year() < date.year || (clock.year() == date.year && clock.month() < date.month - 1);
}

TorpedoVehicle::TorpedoVehicle(const Torpedo& weapon, Vehicle* launcher, const fx::Vec3& position, Angle10 heading,
                               Angle10 pitch, Fixed16 speed, std::int32_t setting, Angle10 course,
                               std::int32_t depth_ft)
    : weapon_(weapon), launcher_(launcher), setting_(setting), course_(course), set_depth_ft_(depth_ft) {
  motion_.position = position;
  motion_.heading = heading;
  motion_.pitch = pitch;
  motion_.speed = speed;
}

void TorpedoVehicle::init(SimContext& ctx) {
  // torpedo_init (0x8571d) / torpedo_motion_init (0x8c208)
  update_bounds();
  if (static_cast<std::int32_t>(weapon_.speed_settings) <= setting_) {
    setting_ = static_cast<std::int32_t>(weapon_.speed_settings) - 1;
  }
  const auto s = static_cast<std::size_t>(setting_);
  motion_.steering = true;
  motion_.log_miles = 0;
  motion_.ordered_power = weapon_.power[s].raw();
  motion_.power = motion_.ordered_power;
  motion_.log_units = 0;
  motion_.ordered_heading = course_;
  motion_init(motion_, ctx.floor);
  motion_.wake_interval_s = 2;
  depth_ft_ = -motion_.position.y / kUnitsPerFoot;
  run_left_ = weapon_.run_time_s[s];
  run_fraction_ = 0;

  const bool faulty = (ctx.env.options & combat_option::kDudTorpedoes) != 0 && weapon_.id == kMark14Id;
  if (faulty && before_fix(ctx.clock, torpedo_fix_dates().deep_running, ctx.env.rules)) {
    set_depth_ft_ += ctx.random.random_int(10);
  }
  premature_timer_ = -1;
  sighted_ = false;
  const PlayerCombat* combat = ctx.sim != nullptr ? ctx.sim->player_combat() : nullptr;
  if (faulty && combat != nullptr &&
      before_fix(ctx.clock, torpedo_fix_dates().magnetic_exploder_off.at(static_cast<std::size_t>(combat->command)),
                 ctx.env.rules)) {
    if (ctx.random.random_int(100) < 20) {
      premature_timer_ = ctx.random.random_int(60) << 16;
    }
  }
  update_bounds();
}

void TorpedoVehicle::update(SimContext& ctx, Fixed16 dt) {
  if (!check_arming(ctx, dt.raw())) {
    move(ctx, dt.raw());
  }
}

bool TorpedoVehicle::fired_by_player() const {
  return launcher_ != nullptr && launcher_->class_id() == VehicleClass::kPlayer;
}

std::int32_t TorpedoVehicle::run_yards() const {
  return motion_.log_miles * kUnitsPerNauticalMile / kUnitsPerYard + motion_.log_units / kUnitsPerYard;
}

bool TorpedoVehicle::check_arming(SimContext& ctx, std::int32_t dt) {
  // torpedo_check_arming (0x8587b)
  const bool duds = (ctx.env.options & combat_option::kDudTorpedoes) != 0;
  const bool player = fired_by_player();
  bool explodes = true;
  Vehicle* hit = nullptr;
  if (motion_.power != 0) {
    std::int32_t message = 0;
    if (!collision().collided) {
      // The magnetic exploder going off on its own.
      if (!player || !duds || premature_timer_ < 1 || (motion_.log_miles < 1 && motion_.log_units < kPrematureRun)) {
        return false;
      }
      premature_timer_ -= dt;
      if (premature_timer_ > 0) {
        return false;
      }
      message = kMsgPremature;
    } else {
      hit = collision().partner;
      if (hit == nullptr || hit->class_id() == VehicleClass::kDownedPilot) {
        return false;
      }
      if ((!player || duds) && motion_.log_miles == 0 && motion_.log_units < kArmingRun) {
        if (player) {
          ctx.crew_message(kMsgDidNotArm);
        }
        explodes = false;
      } else if (player && duds) {
        if (weapon_.id == kMark14Id && before_fix(ctx.clock, torpedo_fix_dates().contact_exploder, ctx.env.rules)) {
          // The contact exploder fails more the squarer the hit.
          std::int32_t angle = fx::angle_sub(fx::angle_add(motion_.heading, Angle10(1800)), hit->heading()).tenths();
          if (angle > 1800) {
            angle = fx::angle_sub(Angle10(angle), Angle10(1800)).tenths();
          }
          angle = (900 - std::abs(900 - angle)) / 10;
          if (angle > ctx.random.random_int(135)) {
            message = kMsgDud;
            explodes = false;
          }
        } else if (ctx.random.random_int(100) <= 2) {
          message = kMsgDud;
          explodes = false;
        }
      }
    }
    if (message != 0) {
      ctx.crew_message(message);
    }
  }
  remove();
  if (ctx.sim != nullptr && ctx.sim->player_combat() != nullptr) {
    ctx.sim->player_combat()->tdc.untrack_torpedo(this);
  }
  if (player) {
    broadcast_contact(ctx, {motion_.position.x, 0, motion_.position.z}, nullptr);
  }
  if (!explodes) {
    return true;
  }
  if (ctx.sim != nullptr) {
    spawn_splash(ctx, motion_.position.x, motion_.position.z, true);
  }
  if (collision().collided && hit != nullptr) {
    const std::int32_t yards = player_range_yards(ctx, motion_.position.x, motion_.position.z);
    std::int32_t volume = distance_volume(yards, 10000);
    if (volume < 1) {
      volume = yards < 50000 ? 1 : 0;
    }
    const PlayerSub* own = context_player(ctx);
    const bool near_surface = own != nullptr && own->depth_ft() <= own->sub().bridge_height_ft;
    if (near_surface) {
      volume >>= 1;
    }
    ctx.sfx(near_surface ? kSfxHitNear : kSfxHitFar, volume);
    const auto warhead = static_cast<std::int32_t>(weapon_.warhead_lb);
    const std::int32_t roll = ctx.random.random_int(warhead / 2);
    hit->take_damage(ctx, launcher_, DamageKind::kTorpedo, 0, (roll + (warhead - warhead / 4)) / 10);
  }
  return true;
}

void TorpedoVehicle::move(SimContext& ctx, std::int32_t dt) {
  // torpedo_motion_update (0x8c292)
  const auto s = static_cast<std::size_t>(setting_);
  const std::int32_t depth_error = depth_ft_ - set_depth_ft_;
  if (depth_error != 0) {
    motion_.pos_y += static_cast<std::int64_t>(fx::mul(dt, kDepthRate) * fx::sign(depth_error));
  }
  HullParams hull;
  hull.length_ft = static_cast<std::int32_t>(weapon_.length_ft);
  hull.beam_ft = static_cast<std::int32_t>(weapon_.beam_ft);
  hull.draft_ft = hull.beam_ft / 2;
  hull.displacement = static_cast<std::int32_t>(weapon_.displacement);
  hull.drag_cubic = weapon_.drag_cubic[s];
  hull.drag_linear = weapon_.drag_linear[s];
  hull.lateral_drag = weapon_.lateral_drag[s];
  hull.astern_drag_factor = Fixed16();
  hull.turn_factor = weapon_.turn_factor;
  motion_step(motion_, hull, 0, Fixed16::from_raw(dt), 100, ctx.floor);
  // It runs at least as deep as its half beam, and stops on the bottom.
  const std::int32_t ceiling = -hull.draft_ft * kUnitsPerFoot;
  if (motion_.position.y < ceiling) {
    if (motion_.keel_clearance > 0) {
      motion_.velocity = {};
      motion_.power = 0;
      motion_.ordered_power = 0;
    }
  } else {
    motion_.position.y = ceiling;
    motion_.pos_y = static_cast<std::int64_t>(ceiling) << 16;
  }
  depth_ft_ = -motion_.position.y / kUnitsPerFoot;
  if (run_left_ > 0) {
    const std::int32_t ratio =
        fx::div(fx::imul(motion_.ordered_power, 0x10000), fx::imul(weapon_.power[s].raw(), 0x10000));
    run_fraction_ += fx::mul(fx::mul(ratio, weapon_.consumption[s].raw()), dt);
    while (run_fraction_ > 0xffff) {
      run_fraction_ -= 0x10000;
      --run_left_;
    }
  }
  if (run_left_ < 1) {
    run_left_ = 0;
    motion_.power = 0;
    motion_.ordered_power = 0;
  }
}

void TorpedoVehicle::update_bounds() {
  // torpedo_update_bbox (0x85c58)
  const std::int32_t half_z = static_cast<std::int32_t>(weapon_.length_ft) * kUnitsPerFoot / 2;
  const std::int32_t half_x = static_cast<std::int32_t>(weapon_.beam_ft) * kUnitsPerFoot / 2;
  const bool armed_box = motion_.log_miles != 0 || motion_.log_units >= kBoxRun;
  const std::int32_t radius = armed_box ? std::max(half_x, half_z) : 0;
  collision().box = make_hull_box(motion_.position, motion_.orientation, motion_.inverse, half_x, half_z,
                                  motion_.position.y - half_x, motion_.position.y + half_x, radius);
}

void TorpedoVehicle::forget(const Vehicle& removed) {
  if (launcher_ == &removed) {
    launcher_ = nullptr;
  }
}

std::optional<fx::Vec3> TorpedoVehicle::torpedo_launch_point() const {
  if (motion_.wake_count == 0) {
    return std::nullopt;
  }
  return motion_.wake[0];
}

void TorpedoVehicle::add_wake_point(const fx::Vec3& p) {
  if (motion_.wake_count < MotionState::kMaxWakePoints) {
    motion_.wake[static_cast<std::size_t>(motion_.wake_count++)] = p;
  }
}

}  // namespace sh
