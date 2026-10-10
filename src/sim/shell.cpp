#include "sim/shell.h"

#include <memory>

#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kRoundLifetime = 0x30000;  // 3 s.
constexpr std::int32_t kSinkRate = 0xa00000;      // 10 ft/s.
constexpr std::int32_t kFrameSeconds = 0xccd;
constexpr std::int32_t kSfxPlayerAa = 0x28;       // 0x29 for the others' guns.
constexpr std::int32_t kSfxEntry = 0x1c;
constexpr std::int32_t kSfxBlast = 0x19;

bool owned_by_player(const Vehicle* owner) { return owner != nullptr && owner->class_id() == VehicleClass::kPlayer; }

// player_is_surfaced (0x607a8): the bridge is above the water.
bool player_surfaced(const PlayerSub& sub) {
  return 0 < sub.sub().bridge_height_ft * kUnitsPerFoot + sub.position().y;
}

}  // namespace

Shell::Shell(const fx::Vec3& position, Angle10 heading, Angle10 elevation, std::int32_t speed_fps,
             std::int32_t caliber, Vehicle* owner)
    : heading_(heading), elevation_(elevation), speed_fps_(speed_fps), caliber_(caliber), owner_(owner) {
  flight_.position = position;
}

void Shell::init(SimContext& ctx) {
  // shell_init (0x86c04)
  flight_ = ballistic_init(speed_fps_, flight_.position, heading_, elevation_);
  update_bounds();
  if (caliber_ != 0) {
    return;
  }
  const PlayerSub* own = context_player(ctx);
  if (own != nullptr && player_surfaced(*own)) {
    const std::int32_t volume =
        distance_volume(player_range_yards(ctx, flight_.position.x, flight_.position.z), 6000);
    if (volume != 0) {
      ctx.sfx(owned_by_player(owner_) ? kSfxPlayerAa : kSfxPlayerAa + 1, volume);
    }
  }
  lifetime_ = kRoundLifetime;
}

void Shell::update(SimContext& ctx, Fixed16 dt) {
  // shell_update (0x86d0d)
  if (!collision().collided) {
    if (flight_.position.y < 1) {
      remove();
      if (ctx.floor.height_at(flight_.position.x, flight_.position.z) < 0) {
        Splash& splash = spawn_splash(ctx, flight_.position.x, flight_.position.z, caliber_ != 0);
        if (caliber_ == 0) {
          splash.set_size(splash.size() / 20);
        }
      }
    }
  } else {
    Vehicle* hit = collision().partner;
    if (hit != owner_ && hit != nullptr) {
      const std::int32_t roll = ctx.random.random_int(caliber_ / 4);
      const DamageKind kind = caliber_ == 0 ? DamageKind::kGunfire : DamageKind::kShellfire;
      if (!hit->take_damage(ctx, owner_, kind, 0, roll + (caliber_ - 3) * 2)) {
        // Through what does not take damage.
        collision().partner = nullptr;
        collision().collided = false;
      } else {
        if (caliber_ != 0) {
          spawn_explosion(ctx, flight_.position, ExplosionKind::kShellHit);
        }
        remove();
      }
    }
  }
  if (caliber_ == 0) {
    lifetime_ -= dt.raw();
    if (lifetime_ < 1) {
      remove();
    }
  }
  if (!removed()) {
    ballistic_step(flight_, dt.raw());
  }
}

void Shell::update_bounds() {
  collision().box = make_projectile_box(flight_.position, heading_, 0x20, 0x20, 0x50, 0x50);
}

void Shell::forget(const Vehicle& removed) {
  if (owner_ == &removed) {
    owner_ = nullptr;
  }
}

Bomb::Bomb(const fx::Vec3& position, Angle10 heading, std::int32_t speed_fps, std::int32_t fuse_depth_ft,
           Vehicle* owner)
    : fuse_depth_ft_(fuse_depth_ft), heading_(heading), speed_fps_(speed_fps), owner_(owner) {
  flight_.position = position;
}

void Bomb::init(SimContext& ctx) {
  // bomb_init (0x87575)
  splash_frame_ = -1;
  splash_timer_ = 100 << 16;
  flight_ = ballistic_init(speed_fps_, flight_.position, heading_, Angle10(0));
  y16_ = static_cast<std::int64_t>(flight_.position.y) << 16;
  depth_ft_ = -flight_.position.y / kUnitsPerFoot;
  bottom_ft_ = -ctx.floor.height_at(flight_.position.x, flight_.position.z) / kUnitsPerFoot;
  update_bounds();
}

void Bomb::update(SimContext& ctx, Fixed16 dt) {
  // bomb_update (0x87672)
  if (check_detonation(ctx)) {
    return;
  }
  move(ctx, dt.raw());
  if (splash_frame_ < 0) {
    return;
  }
  splash_timer_ += dt.raw();
  if (kFrameSeconds < splash_timer_) {
    if (context_effect_frames(ctx).spout <= ++splash_frame_) {
      splash_frame_ = -2;
    }
    while (kFrameSeconds < splash_timer_) {
      splash_timer_ -= kFrameSeconds;
    }
  }
}

void Bomb::move(SimContext& ctx, std::int32_t dt) {
  // bomb_move (0x876ec)
  if (flight_.position.y < 0) {
    if (splash_frame_ == -1) {
      splash_frame_ = 0;
      const PlayerSub* own = context_player(ctx);
      if (own != nullptr) {
        const fx::Vec3 p = own->position();
        const auto d =
            static_cast<std::int32_t>(fx::approx_distance(p.x - flight_.position.x, p.z - flight_.position.z));
        const std::int32_t volume = 0x50 - d * 0x50 / 50000;
        ctx.sfx(volume < 0x32 ? kSfxEntry + 1 : kSfxEntry, volume);
      }
    }
    y16_ -= static_cast<std::uint32_t>(fx::mul(dt, kSinkRate));
    flight_.position.y = static_cast<std::int32_t>(y16_ >> 16);
  } else {
    ballistic_step(flight_, dt);
    y16_ = static_cast<std::int64_t>(flight_.position.y) << 16;
  }
  depth_ft_ = -flight_.position.y / kUnitsPerFoot;
  bottom_ft_ = -ctx.floor.height_at(flight_.position.x, flight_.position.z) / kUnitsPerFoot;
  if (bottom_ft_ <= depth_ft_) {
    depth_ft_ = bottom_ft_;
    if (0 < bottom_ft_) {
      remove();
    }
  }
}

bool Bomb::check_detonation(SimContext& ctx) {
  // bomb_check_detonation (0x87840)
  const bool hit_something = collision().collided;
  if (depth_ft_ < fuse_depth_ft_ && !hit_something && depth_ft_ != bottom_ft_) {
    return false;
  }
  Vehicle* target = nullptr;
  if (hit_something) {
    target = collision().partner;
    if (target == owner_ && depth_ft_ < fuse_depth_ft_) {
      return false;
    }
  }
  if (depth_ft_ < 0 || (depth_ft_ == 0 && bottom_ft_ == 0)) {
    spawn_explosion(ctx, flight_.position, ExplosionKind::kBomb);
  } else {
    spawn_splash(ctx, flight_.position.x, flight_.position.z, true);
  }
  remove();
  PlayerSub* own = context_player(ctx);
  std::uint32_t feet = 0;
  if (own != nullptr) {
    const fx::Vec3 p = own->position();
    feet = fx::approx_distance_3d(p.x - flight_.position.x, p.y - flight_.position.y, p.z - flight_.position.z) >> 4;
    if (feet < 25000) {
      const std::int32_t volume = distance_volume(static_cast<std::int32_t>(feet), 25000);
      ctx.sfx(volume < 0x32 ? kSfxBlast + 1 : kSfxBlast, volume);
    }
  }
  std::uint32_t distance = 0;
  std::int32_t amount = 0;
  if (target == nullptr) {
    target = own;
    distance = feet;
  } else {
    amount = ctx.random.random_int(40) + 40;
  }
  if (target == nullptr || distance > 1000) {
    return false;
  }
  if (feet < 1000) {
    ctx.events.screen_shake = true;
  }
  target->take_damage(ctx, owner_, DamageKind::kBomb, static_cast<std::int32_t>(distance), amount);
  return false;
}

void Bomb::update_bounds() {
  collision().box = make_projectile_box(flight_.position, heading_, 0x50, 0x50, 0x50, 0x50);
}

void Bomb::forget(const Vehicle& removed) {
  if (owner_ == &removed) {
    owner_ = nullptr;
  }
}

}  // namespace sh
