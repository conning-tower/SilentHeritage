#include "sim/deck_gun.h"

#include <algorithm>
#include <cstdlib>
#include <memory>

#include "sim/ballistic.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/torpedo.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kSpeedFactor = 0x8cdf0;  // gun_lead_bearing's knots factor.
constexpr std::int32_t kCrewQuality = 80;
constexpr std::int32_t kCrewRangeYd = 7000;
constexpr std::size_t kGun1 = 14;

// player_is_surfaced (0x607a8)
bool surfaced(const PlayerSub& sub) { return 0 < sub.sub().bridge_height_ft * kUnitsPerFoot + sub.position().y; }

}  // namespace

Angle10 gun_lead_bearing(std::int32_t velocity_fps, Angle10 course, Fixed16 speed, std::int32_t tx, std::int32_t tz,
                         std::int32_t gx, std::int32_t gz) {
  // gun_lead_bearing (0x3855f)
  const std::uint32_t d = fx::approx_distance(tx - gx, tz - gz);
  const auto t = static_cast<std::int32_t>(((d >> 4) << 16) / static_cast<std::uint32_t>(velocity_fps / 4));
  const fx::SinCos sc = fx::angle_sincos(course);
  const std::int32_t v = fx::mul(speed.raw(), kSpeedFactor);
  const std::int32_t dx = fx::trunc(fx::mul(fx::mul(v, sc.sin), t));
  const std::int32_t dz = fx::trunc(fx::mul(fx::mul(v, sc.cos), t));
  return fx::heading_from_delta(tx + dx - gx, tz + dz - gz);
}

Angle10 deck_gun_clamp_train(std::int32_t mount, Angle10 train) {
  // deckgun_clamp_train (0x3786b)
  if (mount == 0) {
    train = fx::angle_add(train, Angle10(1800));
  }
  std::int32_t t = std::clamp(train.tenths(), 70, 3530);
  while (t > 3599) {
    t -= 3600;
  }
  while (t < 0) {
    t += 3600;
  }
  Angle10 r(t);
  if (mount == 0) {
    r = fx::angle_add(r, Angle10(1800));
  }
  return r;
}

void DeckGun::reset(const SubClass& boat, std::int32_t ammo, std::array<SystemState, 24>& systems) {
  // deckgun_reset_state (0x37cdc)
  boat_ = &boat;
  ammo_ = ammo;
  target_ = nullptr;
  follow_ = false;
  locked_ = false;
  blink_ticks_ = 0;
  ticks_ = 0;
  blink_ = false;
  mounts_[0].ready = false;
  mounts_[1].ready = false;
  // player_init (0x5bb60): the forward gun trained ahead, the aft one astern.
  for (Mount& mt : mounts_) {
    mt.elevation = Angle10(0);
    mt.reload = 0;
  }
  mounts_[0].train = Angle10(0);
  mounts_[1].train = Angle10(1800);
  selected_ = 0;
  manual_ = true;
  range_yd_ = 0;
  if (boat.deck_gun_count == 1) {
    selected_ = static_cast<std::int32_t>(boat.single_gun_mount);
    systems[kGun1 + 1].flags = 2;
  }
}

bool DeckGun::usable(const PlayerSub& sub) const {
  const auto& systems = sub.systems();
  bool ok = systems[kGun1].flags == 0;
  if (boat_->deck_gun_count > 1) {
    ok = systems[kGun1 + 1].flags == 0 || ok;
  }
  return ok;
}

void DeckGun::set_manned(SimContext& ctx, const PlayerSub& sub, bool manual, bool toggle) {
  // deckgun_set_manned (0x38731)
  if (toggle) {
    manual = !manual_;
  }
  if (!surfaced(sub) && !manual) {
    return;
  }
  const bool ok = usable(sub);
  std::int32_t message = kMsgCannotMan;
  if (manual || ok) {
    manual_ = manual;
    if (!ok || !toggle) {
      return;
    }
    message = manual ? kMsgManned : kMsgUnmanned;
  }
  ctx.crew_message(message, static_cast<std::int32_t>(boat_->deck_gun_count));
}

void DeckGun::crew_set_target(Vehicle* target) {
  if (!manual_ && target != target_) {
    target_ = target;
  }
}

void DeckGun::update(SimContext& ctx, PlayerSub& sub, const OwnBoat& own, std::int32_t frame_ticks,
                     const TargetSensingFn& sensing) {
  // deckgun_sim_update (0x37d6a)
  if (target_ != nullptr) {
    const TargetSensing s = sensing ? sensing(*target_) : TargetSensing{};
    if (target_->sinking() || ((own.options & combat_option::kLimitedVisibility) != 0 && !s.spotted)) {
      target_ = nullptr;
      locked_ = false;
    }
  }
  if (!surfaced(sub)) {
    if (!manual_) {
      set_manned(ctx, sub, false, true);
    }
    return;
  }
  if (!usable(sub) && !manual_) {
    set_manned(ctx, sub, false, true);
  }
  if (follow_ && !locked_) {
    follow_ = false;
  }
  ticks_ += frame_ticks;
  const std::int32_t elapsed = ticks_;
  blink_ticks_ += frame_ticks;
  if (blink_ticks_ > 25) {
    blink_ticks_ = 0;
    blink_ = !blink_;
  }
  if (ticks_ <= 4) {
    return;
  }
  range_yd_ = 0;
  quality_ = 0;
  ticks_ = 0;
  const bool single = boat_->deck_gun_count == 1;
  // The solution quality of the mount processed (the crew fires on it, the
  // gauge shows the selected mount's).
  std::int32_t quality = 0;
  for (std::int32_t m = 0; m < 2; ++m) {
    Mount& mt = mounts_[static_cast<std::size_t>(m)];
    const bool was_ready = mt.ready;
    mt.ready = false;
    if (single && m != static_cast<std::int32_t>(boat_->single_gun_mount)) {
      continue;
    }
    const std::int32_t quarter = own.length_ft / 4 * kUnitsPerFoot;
    const fx::Vec3 off = fx::transform(own.motion.orientation, {0, 0, m == 1 ? -quarter : quarter});
    mt.x = off.x + own.motion.position.x;
    mt.z = off.z + own.motion.position.z;
    mt.elevation = Angle10(0);
    if (mt.reload > 0 && ammo_ != 0) {
      mt.reload = std::max(mt.reload - elapsed, 0);
    }
    Angle10 aim;
    if (target_ != nullptr) {
      const TargetSensing s = sensing ? sensing(*target_) : TargetSensing{};
      const SolutionEstimate e = tdc_estimate_solution(target_state(*target_, s), sighting_range_, off.x, off.z, own);
      const Angle10 elevation(ballistic_elevation_for_range(kDeckGunVelocity, e.range));
      quality = e.quality;
      if (m == selected_) {
        range_yd_ = e.range / kUnitsPerYard;
        quality_ = quality;
      }
      mt.elevation = elevation;
      aim = e.bearing;
      if (!manual_) {
        aim = fx::angle_sub(gun_lead_bearing(kDeckGunVelocity, e.course, e.speed, e.x, e.z, mt.x, mt.z),
                            own.motion.heading);
        mt.train = deck_gun_clamp_train(m, aim);
      } else if (follow_) {
        mt.train = deck_gun_clamp_train(m, aim);
      }
    }
    // The total elevation stays within 0..45 deg.
    const Angle10 total = fx::angle_add(mt.elevation_offset, mt.elevation);
    if (total.tenths() >= 0x385) {
      mt.elevation_offset = fx::angle_sub(Angle10(0), mt.elevation);
    } else if (total.tenths() > 0x1c2) {
      mt.elevation_offset = fx::angle_sub(Angle10(0x1c2), mt.elevation);
    }
    const std::size_t gun = single ? kGun1 : kGun1 + static_cast<std::size_t>(m);
    if ((sub.systems()[gun].flags & 3) != 0) {
      continue;
    }
    if (mt.reload == 0) {
      mt.ready = true;
    }
    if (target_ != nullptr && !manual_ && mt.ready && was_ready && quality >= kCrewQuality && aim == mt.train &&
        range_yd_ < kCrewRangeYd) {
      fire(ctx, sub, m, own.crew);
    }
  }
}

void DeckGun::fire(SimContext& ctx, PlayerSub& sub, std::int32_t m, std::int32_t crew) {
  // deckgun_fire (0x37a89)
  Mount& mt = mounts_.at(static_cast<std::size_t>(m));
  if (!mt.ready) {
    return;
  }
  Angle10 elevation = fx::angle_add(mt.elevation, mt.elevation_offset);
  const Angle10 heading = fx::angle_add(sub.motion().heading, mt.train);
  const std::uint32_t options = ctx.env.options;
  if ((options & 0xc) > 7) {
    const std::int32_t roll = swell - 30;
    elevation = roll < 0 ? fx::angle_add(elevation, Angle10(std::abs(roll)))
                         : fx::angle_sub(elevation, Angle10(std::abs(roll)));
  }
  ctx.sim->add(std::make_unique<Shell>(fx::Vec3{mt.x, 1, mt.z}, heading, elevation, kDeckGunVelocity,
                                       static_cast<std::int32_t>(boat_->deck_gun_caliber_in), &sub));
  spawn_explosion(ctx, {mt.x, 0xa0, mt.z}, ExplosionKind::kMuzzleFlash);
  broadcast_contact(ctx, {mt.x, 0, mt.z}, nullptr);
  if ((options & combat_option::kUnlimitedAmmo) == 0 && ammo_ != 0) {
    --ammo_;
  }
  const std::int32_t base = (options & combat_option::kRealisticReloads) != 0 ? (3 - crew) * 400 : 200;
  mt.reload = ctx.random.random_int(base / 4) + base;
}

void DeckGun::pick_target_on_bearing(const OwnBoat& own, const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                                     const TargetSensingFn& sensing) {
  // deckgun_pick_target_on_bearing (0x38115)
  if (locked_) {
    return;
  }
  Vehicle* previous = target_;
  target_ = nullptr;
  const std::int32_t wanted =
      fx::angle_add(own.motion.heading, mounts_[static_cast<std::size_t>(selected_)].train).tenths();
  Vehicle* best = nullptr;
  std::uint32_t best_distance = 0;
  for (const std::unique_ptr<Vehicle>& v : vehicles) {
    // Slot +0x54: surface targets only (as the TDC's pick).
    if (!v->is_surface_target() || v->removed() || v->sinking()) {
      continue;
    }
    const TargetSensing s = sensing ? sensing(*v) : TargetSensing{};
    if ((!s.spotted && (own.options & combat_option::kLimitedVisibility) != 0) || !s.line_of_sight) {
      continue;
    }
    const fx::Vec3 p = v->position();
    const std::int32_t dx = p.x - own.motion.position.x;
    const std::int32_t dy = p.y - own.motion.position.y;
    const std::int32_t dz = p.z - own.motion.position.z;
    // The bearings are compared without wrapping at north.
    if (std::abs(fx::heading_from_delta(dx, dz).tenths() - wanted) < 0x1a) {
      const std::uint32_t d = fx::approx_distance_3d(dx, dy, dz);
      if (best == nullptr || d < best_distance) {
        best_distance = d;
        best = v.get();
      }
    }
  }
  if (best != nullptr) {
    target_ = best;
    if (best != previous) {
      mounts_[static_cast<std::size_t>(selected_)].elevation_offset = Angle10(0);
    }
  }
}

void DeckGun::toggle_target_lock(const OwnBoat& own, const std::vector<std::unique_ptr<Vehicle>>& vehicles,
                                 const TargetSensingFn& sensing) {
  // deckgun_toggle_target_lock (0x384be)
  if (locked_) {
    locked_ = false;
    return;
  }
  pick_target_on_bearing(own, vehicles, sensing);
  if (target_ != nullptr) {
    locked_ = true;
  }
}

void DeckGun::toggle_bearing_follow(bool off) {
  // deckgun_toggle_bearing_follow (0x384eb)
  if (off) {
    follow_ = false;
  } else if (locked_) {
    follow_ = !follow_;
  }
}

void DeckGun::forget(const Vehicle& removed) {
  if (target_ == &removed) {
    target_ = nullptr;
  }
}

}  // namespace sh
