#include "sim/ship.h"

#include <algorithm>
#include <array>

namespace sh {

Ship::Ship(const ShipMotionParams& params, fx::Vec3 position, Angle10 heading, Fixed16 speed, ShipOrder orders)
    : params_(params), orders_(orders) {
  motion_.position = position;
  motion_.heading = heading;
  motion_.speed = speed;
  set_side(1);
}

void Ship::init(SimContext& ctx) {
  // The draws of ship_init, before its motion setup.
  static constexpr std::array<std::int32_t, 3> kFooledByDebrisPct = {30, 10, 2};  // 0x10fa8c
  reaction_rolls_.flee_angle = (ctx.random.random_int(90) + 135) * 10;
  reaction_rolls_.breakaways_left = ctx.random.random_int(3) + 1;
  reaction_rolls_.preserved_timer_1dc = Fixed16::from_int((ctx.random.random_int(10) + 2) * 60);
  const auto crew = static_cast<std::size_t>(std::clamp(setup_.crew, 0, 2));
  reaction_rolls_.fooled_by_debris = ctx.env.scenario_roll < kFooledByDebrisPct[crew];
  ai_ship_init(*this, ctx);
  const std::int32_t sea = ctx.env.sea_state;
  motion_.ordered_power = motion_power_for_speed(motion_.speed, params_.max_power, params_.hull, sea, 100);
  ship_effects_init(ctx, combat_.effects);
  combat_.hit = true;  // +0xd0 starts set: the damage cap applies at once.
  ship_update_damage_state(ctx, *this);
  motion_.speed = motion_speed_for_power(motion_.ordered_power, params_.hull, sea, 100);
  motion_.power = motion_.ordered_power;
  motion_init(motion_, ctx.floor);
}

void Ship::set_alert(std::int32_t level) {
  alert_.timer = Fixed16::from_int((setup_.crew + 1) * 1800);
  alert_.previous = alert_.level;
  alert_.level = level;
}

void Ship::update(SimContext& ctx, Fixed16 dt) {
  ship_update_damage_state(ctx, *this);
  ai_ship_update(*this, ctx, dt);
}

void Ship::update_bounds() {
  // ship_update_bbox (0x82116)
  if (setup_.ship_class == nullptr) {
    return;
  }
  const ShipClass& cls = *setup_.ship_class;
  const auto half_z = static_cast<std::int32_t>(cls.length_ft) * kUnitsPerFoot / 2;
  const auto half_x = static_cast<std::int32_t>(cls.beam_ft) * kUnitsPerFoot / 2;
  const std::int32_t keel = motion_.position.y - static_cast<std::int32_t>(cls.draft_ft) * kUnitsPerFoot;
  const std::int32_t top = motion_.position.y + static_cast<std::int32_t>(cls.visual_size) * kUnitsPerFoot;
  collision().box = make_hull_box(motion_.position, motion_.orientation, motion_.inverse, half_x, half_z, keel, top);
}

bool Ship::take_damage(SimContext& ctx, Vehicle* attacker, DamageKind /*kind*/, std::int32_t /*distance*/,
                       std::int32_t amount) {
  return ship_take_damage(ctx, *this, attacker, amount);
}

void Ship::forget(const Vehicle& removed) {
  if (combat_.attacker == &removed) {
    combat_.attacker = nullptr;
  }
  if (ai_.contact.vehicle == &removed) {
    ai_.contact.vehicle = nullptr;
  }
  if (ai_.debris == &removed) {
    ai_.debris = nullptr;
  }
  if (ai_.leader == &removed) {
    ai_.leader = nullptr;
  }
}

}  // namespace sh
