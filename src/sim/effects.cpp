#include "sim/effects.h"

#include <memory>

#include "assets/shp.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kFrameSeconds = 0xccd;   // 0.05 s per animation frame.
constexpr std::int32_t kSplashTick = 0x2666;    // 0.15 s.
constexpr std::int32_t kBlastLifetime = 0xb40000;  // 180 s.
constexpr std::int32_t kDebrisLifetime = 0x5dc0000;  // 1500 s.
constexpr std::int32_t kSfxSplash = 0x1b;
constexpr std::int32_t kSfxFlash = 0x22;

std::int32_t frame_count(const Install& install, std::string_view path) {
  return static_cast<std::int32_t>(Shp::load(install, path).frame_count());
}

}  // namespace

EffectFrames EffectFrames::load(const Install& install) {
  EffectFrames f;
  f.spout = frame_count(install, "ART/SPOUT.SHP");
  f.gunfire = frame_count(install, "ART/GUNFIRE.SHP");
  f.gunexpl = frame_count(install, "ART/GUNEXPL.SHP");
  f.expl = frame_count(install, "ART/EXPL-1.SHP");
  f.fire = frame_count(install, "ART/FIRE-0.SHP");
  f.sinking_expl = {frame_count(install, "ART/EXPL-0.SHP"), f.expl};
  return f;
}

Splash::Splash(std::int32_t x, std::int32_t z, bool play_sound, std::int32_t frame_count)
    : position_{x, 0, z}, play_sound_(play_sound), frame_count_(frame_count) {}

void Splash::init(SimContext& ctx) {
  if (!play_sound_) {
    return;
  }
  const PlayerSub* player = context_player(ctx);
  if (player == nullptr) {
    return;
  }
  std::int32_t volume = distance_volume(player_range_yards(ctx, position_.x, position_.z), 2000);
  if (volume < 0) {
    volume = 0;
  }
  if (player->depth_ft() <= player->sub().bridge_height_ft) {
    ctx.sfx(kSfxSplash, volume);
  }
}

void Splash::update(SimContext& /*ctx*/, Fixed16 dt) {
  frame_timer_ += dt.raw();
  tick_timer_ += dt.raw();
  if (kFrameSeconds < frame_timer_) {
    ++frame_;
    if (frame_count_ <= frame_) {
      frame_ = 0;
      remove();
    }
    while (kFrameSeconds < frame_timer_) {
      frame_timer_ -= kFrameSeconds;
    }
  }
  if (kSplashTick < tick_timer_) {
    ++ticks_;
    while (kSplashTick < tick_timer_) {
      tick_timer_ -= kSplashTick;
    }
  }
}

ExplosionFx::ExplosionFx(const fx::Vec3& position, ExplosionKind kind, std::int32_t frame_count)
    : position_(position), kind_(kind), frame_count_(frame_count) {}

void ExplosionFx::init(SimContext& ctx) {
  const std::int32_t yards = player_range_yards(ctx, position_.x, position_.z);
  std::int32_t volume = distance_volume(yards, 50000);
  if (volume < 1) {
    volume = yards < 50000 ? 5 : 0;
  }
  ctx.sfx(kind_ != ExplosionKind::kMuzzleFlash ? kSfxFlash + 1 : kSfxFlash, volume);
}

void ExplosionFx::update(SimContext& /*ctx*/, Fixed16 dt) {
  frame_timer_ += dt.raw();
  if (kFrameSeconds < frame_timer_) {
    ++frame_;
    if (frame_count_ <= frame_) {
      frame_ = 0;
      remove();
    }
    while (kFrameSeconds < frame_timer_) {
      frame_timer_ -= kFrameSeconds;
    }
  }
}

void UnderwaterBlast::init(SimContext& /*ctx*/) { lifetime_ = kBlastLifetime; }

void UnderwaterBlast::update(SimContext& /*ctx*/, Fixed16 dt) {
  lifetime_ -= dt.raw();
  if (lifetime_ < 1) {
    remove();
  }
}

void Debris::init(SimContext& /*ctx*/) { lifetime_ = kDebrisLifetime; }

void Debris::update(SimContext& /*ctx*/, Fixed16 dt) {
  lifetime_ -= dt.raw();
  if (lifetime_ < 1) {
    remove();
  }
}

Debris& spawn_debris(SimContext& ctx, const fx::Vec3& boat) {
  return ctx.sim->add(std::make_unique<Debris>(fx::Vec3{boat.x, 0, boat.z}));
}

PlayerSub* context_player(SimContext& ctx) {
  return ctx.sim != nullptr ? dynamic_cast<PlayerSub*>(ctx.sim->player()) : nullptr;
}

EffectFrames context_effect_frames(SimContext& ctx) {
  if (ctx.sim != nullptr && ctx.sim->player_combat() != nullptr) {
    return ctx.sim->player_combat()->effects;
  }
  return {};
}

std::int32_t player_range_yards(SimContext& ctx, std::int32_t x, std::int32_t z) {
  const PlayerSub* player = context_player(ctx);
  if (player == nullptr) {
    return 0;
  }
  const fx::Vec3 p = player->position();
  return static_cast<std::int32_t>(fx::approx_distance(p.x - x, p.z - z) / kUnitsPerYard);
}

Splash& spawn_splash(SimContext& ctx, std::int32_t x, std::int32_t z, bool play_sound) {
  return ctx.sim->add(std::make_unique<Splash>(x, z, play_sound, context_effect_frames(ctx).spout));
}

ExplosionFx& spawn_explosion(SimContext& ctx, const fx::Vec3& position, ExplosionKind kind) {
  const EffectFrames frames = context_effect_frames(ctx);
  const std::int32_t count = kind == ExplosionKind::kMuzzleFlash ? frames.gunfire
                             : kind == ExplosionKind::kShellHit  ? frames.gunexpl
                                                                 : frames.expl;
  return ctx.sim->add(std::make_unique<ExplosionFx>(position, kind, count));
}

void broadcast_contact(SimContext& ctx, const fx::Vec3& position, Vehicle* target) {
  if (ctx.sim == nullptr) {
    return;
  }
  // Indexed: a receiver may spawn vehicles.
  const auto& vehicles = ctx.sim->vehicles();
  for (std::size_t i = 0; i < vehicles.size(); ++i) {
    vehicles[i]->receive_contact(ctx, position, target);
  }
}

}  // namespace sh
