#include "sim/ship_combat.h"

#include <algorithm>

#include "assets/dat_tables.h"
#include "sim/ai_ship.h"
#include "sim/effects.h"
#include "sim/player_sub.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/torpedo.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kAnimFrame = 0x2666;  // 0.15 s.
constexpr std::int32_t kSfxSinkingFar = 0x15;
constexpr std::int32_t kSfxSinkingNear = 0x16;  // Heard at the surface; +2 when faint.

}  // namespace

void ship_effects_init(SimContext& ctx, ShipEffects& fx) {
  // veh_fx_state_init (0x8be3c)
  fx = ShipEffects{};
  fx.sink_rate = ctx.random.random_int(6) + 4;
  fx.explosion_sprite = ctx.random.random_int(2);
}

bool ship_take_damage(SimContext& ctx, Ship& ship, Vehicle* attacker, std::int32_t amount) {
  // ship_take_damage (0x82383)
  ShipSetup& setup = ship.setup();
  if (attacker != nullptr && attacker->class_id() == VehicleClass::kPlayer) {
    const std::int32_t level = std::min<std::int32_t>(2, static_cast<std::int32_t>((ctx.env.options & 0xf) >> 2));
    amount *= 3 - level;
  } else {
    setup.damage = std::max(setup.damage + amount, 0);
  }
  setup.hit_points = std::max(setup.hit_points - amount, 0);
  const ShipClass* cls = setup.ship_class;
  if (cls != nullptr && setup.hit_points <= static_cast<std::int32_t>(cls->hull_strength) / 2 &&
      (ship.orders() == ShipOrder::kConvoyLeader || ship.orders() == ShipOrder::kConvoyMember)) {
    ship_hand_over_convoy_lead(ship, ctx);
    if (ship.orders() == ShipOrder::kConvoyMember && ctx.sim != nullptr) {
      // A member takes the leader's route.
      if (const Ship* leader = convoy_find_leader(ctx.sim->vehicles(), setup.convoy_group)) {
        setup.route_index = leader->setup().route_index;
        setup.route = leader->setup().route;
      }
    }
    ship.set_orders(ShipOrder::kFollowPath);
  }
  ship.combat().hit = true;
  ship.combat().attacker = attacker;
  return true;
}

void ship_update_damage_state(SimContext& ctx, Ship& ship) {
  // ship_update_damage_state (0x817e3)
  const ShipClass* cls = ship.setup().ship_class;
  ShipDamageState& state = ship.combat();
  if (cls == nullptr) {
    state.hit = false;
    return;
  }
  const auto strength = static_cast<std::int32_t>(cls->hull_strength);
  const std::int32_t hp = ship.setup().hit_points;
  if (strength <= hp) {
    state.hit = false;
    return;
  }
  MotionState& m = ship.motion();
  if (state.hit) {
    if (strength / 20 < hp || ship.sinking()) {
      if (hp <= strength / 2) {
        state.effects.burning = true;
      }
    } else {
      ship.set_sinking();
      state.effects.sinking = true;
      m.power = 0;
      m.ordered_power = 0;
      if (state.attacker != nullptr && state.attacker->class_id() == VehicleClass::kPlayer) {
        ctx.events.ships_sunk.push_back(cls);
        ctx.crew_message(kMsgShipSinking);
      }
      ship_hand_over_convoy_lead(ship, ctx);
      ship.set_orders(ShipOrder::kNone);
      const std::int32_t yards = player_range_yards(ctx, m.position.x, m.position.z);
      std::int32_t volume = distance_volume(yards, 10000);
      if (volume < 1) {
        volume = yards < 50000 ? 1 : 0;
      }
      const PlayerSub* own = context_player(ctx);
      std::int32_t sfx = own != nullptr && own->depth_ft() <= own->sub().bridge_height_ft ? kSfxSinkingNear
                                                                                           : kSfxSinkingFar;
      if (volume < 0x32) {
        sfx += 2;
      }
      ctx.sfx(sfx, volume);
    }
    state.hit = false;
  }
  if (!ship.sinking()) {
    const std::int32_t cap = fx::imul(hp * 100 / strength, ship.params().max_power) / 100;
    if (cap < m.ordered_power) {
      m.ordered_power = cap;
    }
  }
}

void ship_update_effects(SimContext& ctx, Ship& ship, Fixed16 dt_s) {
  const ShipClass* cls = ship.setup().ship_class;
  vehicle_update_effects(ctx, ship, ship.combat().effects, ship.motion(),
                         static_cast<std::int32_t>(cls != nullptr ? cls->visual_size : 0),
                         static_cast<std::int32_t>(cls != nullptr ? cls->draft_ft : 0), dt_s);
}

void vehicle_update_effects(SimContext& ctx, Vehicle& vehicle, ShipEffects& fx, MotionState& m,
                            std::int32_t visual_size_ft, std::int32_t draft_ft, Fixed16 dt_s) {
  // veh_fx_state_update (0x8be83)
  const std::int32_t dt = dt_s.raw();
  const EffectFrames frames = context_effect_frames(ctx);
  if (m.speed_tenths > 0) {
    fx.bow_wave_timer += dt;
    const std::int32_t period =
        std::clamp(static_cast<std::int32_t>(0x140000 / static_cast<std::int64_t>(m.speed_tenths)), 0x2666, 0xc000);
    if (period < fx.bow_wave_timer) {
      if (++fx.bow_wave_frame > 2) {
        fx.bow_wave_frame = 0;
      }
      fx.bow_wave_timer -= period;
    }
  }
  if (fx.burning) {
    if (!fx.sinking) {
      fx.fire_timer += dt;
      if (kAnimFrame < fx.fire_timer) {
        if (frames.fire <= ++fx.fire_frame) {
          fx.fire_frame = 0;
        }
        fx.fire_timer -= kAnimFrame;
      }
    } else {
      fx.burning = false;
      fx.fire_frame = 0;
      fx.fire_timer = 0;
    }
  }
  if (!fx.sinking) {
    return;
  }
  fx.sink_timer += dt;
  fx.fire_timer += dt;
  if (0x10000 < fx.sink_timer) {
    fx.sink_timer -= 0x10000;
    m.position.y -= fx.sink_rate;
    m.keel_clearance = ctx.floor.height_at(m.position.x, m.position.z) + (draft_ft * kUnitsPerFoot - m.position.y);
    m.pos_y = static_cast<std::int64_t>(m.position.y) << 16;
    if (m.position.y / kUnitsPerFoot <= -visual_size_ft) {
      vehicle.remove();
    }
  }
  if (kAnimFrame < fx.fire_timer) {
    if (frames.fire <= ++fx.fire_frame) {
      fx.fire_frame = 0;
    }
    fx.fire_timer -= kAnimFrame;
    if (fx.explosion_frame >= 0) {
      ++fx.explosion_frame;
      if (frames.sinking_expl.at(static_cast<std::size_t>(fx.explosion_sprite)) <= fx.explosion_frame) {
        fx.explosion_frame = -1;
      }
    }
  }
}

}  // namespace sh
