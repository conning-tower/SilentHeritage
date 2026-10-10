#include "sim/depth_charge.h"

#include <memory>

#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kSinkRate = 0xa00000;  // 10 ft/s in 16.16 world units.
constexpr std::int32_t kThrowSpeedFps = 60;
constexpr std::int32_t kFrameSeconds = 0xccd;
constexpr std::int32_t kHeardWithinFt = 25000;
constexpr std::int32_t kDamageWithinFt = 2000;
constexpr std::int32_t kSfxEntry = 0x1c;   // 0x1d when faint.
constexpr std::int32_t kSfxBlast = 0x19;   // 0x1a when faint.

}  // namespace

DepthCharge::DepthCharge(const fx::Vec3& position, Angle10 heading, Angle10 pitch, std::int32_t depth_ft)
    : heading_(heading), pitch_(pitch), set_depth_ft_(depth_ft) {
  flight_.position = position;
}

void DepthCharge::init(SimContext& ctx) {
  // depth_charge_init (0x86169)
  splash_frame_ = -1;
  splash_timer_ = 100 << 16;
  flight_ = ballistic_init(pitch_.tenths() < 1 ? 0 : kThrowSpeedFps, flight_.position, heading_, pitch_);
  y16_ = static_cast<std::int64_t>(flight_.position.y) << 16;
  depth_ft_ = -flight_.position.y / kUnitsPerFoot;
  bottom_ft_ = -ctx.floor.height_at(flight_.position.x, flight_.position.z) / kUnitsPerFoot;
  update_bounds();
}

void DepthCharge::update(SimContext& ctx, Fixed16 dt) {
  // dc_update (0x8627b)
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

void DepthCharge::move(SimContext& ctx, std::int32_t dt) {
  // dc_move (0x862f5): free fall in the air, 10 ft/s in the water.
  if (flight_.position.y < 1) {
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
  if (bottom_ft_ <= depth_ft_) {
    remove();
    depth_ft_ = bottom_ft_;
  }
}

bool DepthCharge::check_detonation(SimContext& ctx) {
  // dc_check_detonation (0x8641d)
  if (depth_ft_ < set_depth_ft_) {
    return false;
  }
  spawn_splash(ctx, flight_.position.x, flight_.position.z, true);
  remove();
  ctx.sim->add(std::make_unique<UnderwaterBlast>(flight_.position));
  PlayerSub* own = context_player(ctx);
  if (own == nullptr) {
    return false;
  }
  const fx::Vec3 p = own->position();
  const std::uint32_t feet =
      fx::approx_distance_3d(p.x - flight_.position.x, (p.y - flight_.position.y) * 2, p.z - flight_.position.z) >> 4;
  if (feet >= kHeardWithinFt) {
    return false;
  }
  const std::int32_t volume = distance_volume(static_cast<std::int32_t>(feet), kHeardWithinFt);
  ctx.sfx(volume < 0x32 ? kSfxBlast + 1 : kSfxBlast, volume);
  if (feet > kDamageWithinFt) {
    return false;
  }
  ctx.events.screen_shake = true;
  PlayerCombat* combat = ctx.sim->player_combat();
  if (combat != nullptr) {
    combat->damage.apply(ctx, *own, combat->tubes, DamageKind::kDepthCharge, static_cast<std::int32_t>(feet),
                         combat->crew);
  }
  return true;
}

void DepthCharge::update_bounds() {
  collision().box = make_projectile_box(flight_.position, heading_, 0x50, 0x50, 0x50, 0);
}

DepthCharge& spawn_depth_charge(SimContext& ctx, const fx::Vec3& position, Angle10 heading, Angle10 pitch,
                                std::int32_t depth_ft) {
  return ctx.sim->add(std::make_unique<DepthCharge>(position, heading, pitch, depth_ft));
}

}  // namespace sh
