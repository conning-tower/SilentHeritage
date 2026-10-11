#include "sim/enemy_sub.h"

#include <algorithm>
#include <array>
#include <climits>
#include <memory>

#include "sim/ai_ship.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/intercept.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim/torpedo.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kSecond = 0x10000;
constexpr std::int32_t kMinute = 0x3c0000;
constexpr std::int32_t kThrustPerHp = 0x2260000;  // 550 x 65536.
constexpr std::int32_t kForgetAge = 0x1e00000;    // 480 s.
constexpr std::int32_t kDeepY = -0x360;           // 54 ft.
constexpr std::int32_t kScrewsReportTicks = 18000;  // 3 min in hundredths.
// Spread offsets of a salvo, degrees (0x103018).
constexpr std::array<std::int32_t, 5> kSpread = {0, -5, 5, -10, 10};

std::int32_t thrust_hp(std::int32_t thrust) { return fx::div(fx::abs(thrust), kThrustPerHp); }

Angle10 add(Angle10 a, std::int32_t b) { return fx::angle_add(a, Angle10(b)); }

HullParams ship_class_hull(const ShipClass& c) {
  HullParams h;
  h.length_ft = static_cast<std::int32_t>(c.length_ft);
  h.beam_ft = static_cast<std::int32_t>(c.beam_ft);
  h.draft_ft = static_cast<std::int32_t>(c.draft_ft);
  h.displacement = static_cast<std::int32_t>(c.displacement_tons);
  h.drag_cubic = c.drag_cubic;
  h.drag_linear = c.drag_linear;
  h.lateral_drag = c.lateral_drag;
  h.astern_drag_factor = c.astern_drag_factor;
  h.turn_factor = c.turn_factor;
  return h;
}

// The route with the original object's array overlaps: x[25] is z[0] and
// z[25] the route index.
std::int32_t route_x(const EnemySubSetup& s, std::int32_t i) {
  return i < Route::kCapacity ? s.route.x[static_cast<std::size_t>(i)] : s.route.z[0];
}
std::int32_t route_z(const EnemySubSetup& s, std::int32_t i) {
  return i < Route::kCapacity ? s.route.z[static_cast<std::size_t>(i)] : s.route_index;
}

}  // namespace

SubClassParams enemy_sub_dive_params(const ShipClass& cls) {
  SubClassParams p;
  p.length_ft = static_cast<std::int32_t>(cls.length_ft);
  p.beam_ft = static_cast<std::int32_t>(cls.beam_ft);
  p.draft_ft = 0;
  p.awash_depth_ft = static_cast<std::int32_t>(cls.deck_height_ft);
  p.displacement_surfaced = static_cast<std::int32_t>(cls.displacement_tons);
  p.ballast_neutral = static_cast<std::int32_t>(cls.ballast_neutral);
  p.ballast_max = static_cast<std::int32_t>(cls.ballast_max);
  p.drag_cubic_surfaced = cls.drag_cubic;
  p.drag_cubic_submerged = cls.drag_cubic_submerged;
  p.drag_linear_surfaced = cls.drag_linear;
  p.drag_linear_submerged = cls.drag_linear_submerged;
  p.lateral_drag = cls.lateral_drag;
  p.astern_drag_factor = cls.astern_drag_factor;
  p.turn_factor = cls.turn_factor;
  p.pitch_factor = Fixed16::from_raw(0x263333);
  return p;
}

EnemySub::EnemySub(fx::Vec3 position, Angle10 heading, Fixed16 speed, ShipOrder orders, std::int32_t side)
    : orders_(orders) {
  motion_.position = position;
  motion_.heading = heading;
  motion_.ordered_heading = heading;
  motion_.speed = speed;
  motion_.steering = true;
  set_side(side);
}

void EnemySub::set_alert(std::int32_t level) {
  // enemy_sub_set_alert (0x19b68)
  ai_.alert.previous = ai_.alert.level;
  ai_.alert.level = level;
  ai_.alert.timer = Fixed16::from_int((setup_.crew + 1) * 1800);
}

void EnemySub::init(SimContext& ctx) {
  // enemy_sub_init (0x7f12f)
  ai_.think_time = 0;
  ai_.contact = ShipAiContact{};
  ai_.contact.age = -1;
  sighting_ = ShipSightingRecord{};
  ai_.mode = EnemySubMode::kCruise;
  ai_.attacked_player = false;
  ai_.timer = 0;
  ai_.opening_time = 0;
  ai_.timer2 = 0;
  ai_.periscope_time = 0;
  ai_.cooldown = 0;
  ai_.torpedoes = 5;
  const ShipClass* cls = setup_.ship_class;
  if (cls != nullptr) {
    params_ = enemy_sub_dive_params(*cls);
    hull_ = ship_class_hull(*cls);
    max_power_ = cls->max_power.raw();
    submerged_power_ = cls->submerged_power.raw();
  }
  combat_.hit = true;

  // Route pruning, as the ships' (the next point is dropped).
  Route& r = setup_.route;
  const Sensors* sensors = context_sensors(ctx);
  const TerrainGrid* grid = sensors != nullptr ? sensors->environment().grid : nullptr;
  const std::int32_t draft = hull_.draft_ft;
  for (std::int32_t i = 0; i < r.count; ++i) {
    const std::int32_t x = r.x[static_cast<std::size_t>(i)];
    const std::int32_t z = r.z[static_cast<std::size_t>(i)];
    std::int32_t next_x = route_x(setup_, i + 1);
    std::int32_t next_z = route_z(setup_, i + 1);
    if (r.count - 1 == i) {
      if (next_x != -1) {
        break;
      }
      next_x = r.x[0];
      next_z = r.z[0];
    }
    if (ctx.floor.height_at(x, z) < draft * -0x30 / 2 &&
        (grid == nullptr || !land_on_line(*grid, {x, z}, {next_x, next_z}).land)) {
      break;
    }
    for (std::int32_t j = i + 1; j <= r.count && j < Route::kCapacity; ++j) {
      r.x[static_cast<std::size_t>(j)] = route_x(setup_, j + 1);
      r.z[static_cast<std::size_t>(j)] = route_z(setup_, j + 1);
    }
    --r.count;
  }

  sub_dive_state_init(dive_, motion_, params_);
  const std::int32_t sea = ctx.env.sea_state;
  motion_.ordered_power = motion_power_for_speed(motion_.speed, max_power_, hull_, sea, 100);
  motion_.power = motion_.ordered_power;
  ship_effects_init(ctx, combat_.effects);
  motion_.speed = motion_speed_for_power(motion_.ordered_power, hull_, sea, 100);
  motion_.power = motion_.ordered_power;
  motion_init(motion_, ctx.floor);
  update_bounds();
}

void EnemySub::update(SimContext& ctx, Fixed16 dt_s) {
  // enemy_sub_update (0x7f772)
  const std::int32_t dt = dt_s.raw();
  Sensors* sensors = context_sensors(ctx);
  const SensorEnvironment no_sensors;
  const SensorEnvironment& env = sensors != nullptr ? sensors->environment() : no_sensors;
  const PlayerSub* player = context_player(ctx);
  const auto visual_size = static_cast<std::int32_t>(setup_.ship_class != nullptr ? setup_.ship_class->visual_size : 0);
  if (player != nullptr) {
    // The eye is the player's depth in feet x 16, as the original passes it.
    const fx::Vec3 eye{player->position().x, player->depth_ft() * kUnitsPerFoot, player->position().z};
    ai_.sighting_range_yd =
        sighting_check(env.sighting(ctx.env), env.grid, eye,
                       {motion_.position.x, visual_size * kUnitsPerFoot, motion_.position.z})
            .range.range_yd;
  }
  update_damage_state(ctx);
  if (sensors != nullptr) {
    think(ctx, dt);
  }
  // enemy_sub_update_depth (0x7f926): the ordered depth (feet) is clamped
  // with world units, as the original.
  if (!sinking()) {
    const std::int32_t floor_y = ctx.floor.height_at(motion_.position.x, motion_.position.z) - 0x140;
    if (dive_.ordered_depth_ft * -kUnitsPerFoot < floor_y) {
      dive_.ordered_depth_ft = floor_y;
    }
    sub_dive_update(dive_, params_, motion_, dt_s, nullptr, ctx.env.sea_state, ctx.floor);
  }
  vehicle_update_effects(ctx, *this, combat_.effects, motion_, visual_size, hull_.draft_ft, dt_s);
  if (sighting_.last_x >= 0) {
    sighting_.age += dt;
    if (sighting_.track_time != 0) {
      const bool selected = this == env.tdc_target || this == env.gun_target;
      if (!sighted() || !selected) {
        sighting_.track_time = std::max(sighting_.track_time - dt, 0);
      }
    }
    if (sighting_.age > kForgetAge) {
      sighting_.last_x = -1;
      sighting_.track_time = 0;
      sighting_.sighted = false;
    }
  }
  if (player != nullptr) {
    SightingConditions conditions{0, 0, 100};
    if ((ctx.env.options & sensor_option::kLimitedVisibility) != 0) {
      conditions = env.sighting(ctx.env);
    }
    const SightingCheck check =
        player_sighting_check(player_lookout_state(*player, env.crew), conditions, env.grid, motion_.position.x,
                              visual_height(), motion_.position.z, exposure());
    player_range_yd_ = check.range.range_yd;
    player_distance_yd_ = check.distance_yd;
  }
}

void EnemySub::update_damage_state(SimContext& ctx) {
  // enemy_sub_update_damage_state (0x7f9ff)
  const ShipClass* cls = setup_.ship_class;
  if (cls == nullptr) {
    combat_.hit = false;
    return;
  }
  const auto strength = static_cast<std::int32_t>(cls->hull_strength);
  const std::int32_t hp = setup_.hit_points;
  if (strength <= hp) {
    combat_.hit = false;
    return;
  }
  if (combat_.hit) {
    if (strength / 20 < hp || sinking()) {
      if (hp <= strength / 2) {
        combat_.effects.burning = true;
      }
    } else {
      set_sinking();
      combat_.effects.sinking = true;
      motion_.power = 0;
      motion_.ordered_power = 0;
      if (combat_.attacker != nullptr && combat_.attacker->class_id() == VehicleClass::kPlayer) {
        ctx.events.ships_sunk.push_back(cls);
        ctx.crew_message(kMsgShipSinking);
      }
      const std::int32_t yards = player_range_yards(ctx, motion_.position.x, motion_.position.z);
      std::int32_t volume = distance_volume(yards, 10000);
      if (volume < 1) {
        volume = yards < 50000 ? 1 : 0;
      }
      // The near sound when the player's depth is within the sub's bridge
      // height, which its parameter block leaves at 0.
      const PlayerSub* own = context_player(ctx);
      std::int32_t sfx = own != nullptr && own->depth_ft() <= params_.bridge_height_ft ? 0x16 : 0x15;
      if (volume < 0x32) {
        sfx += 2;
      }
      ctx.sfx(sfx, volume);
    }
    combat_.hit = false;
  }
  if (!sinking()) {
    // The product wraps as in the original (0x7f9ff).
    const std::int32_t cap = fx::imul(hp * 100 / strength, max_power_) / 100;
    if (cap < motion_.ordered_power) {
      motion_.ordered_power = cap;
    }
  }
}

bool EnemySub::take_damage(SimContext& ctx, Vehicle* attacker, DamageKind /*kind*/, std::int32_t /*distance*/,
                           std::int32_t amount) {
  // enemy_sub_take_damage (0x7f982)
  if (attacker != nullptr && attacker->class_id() == VehicleClass::kPlayer) {
    const std::int32_t level = std::min<std::int32_t>(2, static_cast<std::int32_t>((ctx.env.options & 0xf) >> 2));
    amount *= 3 - level;
  } else {
    setup_.damage = std::max(setup_.damage + amount, 0);
  }
  setup_.hit_points = std::max(setup_.hit_points - amount, 0);
  combat_.hit = true;
  combat_.attacker = attacker;
  return true;
}

void EnemySub::forget(const Vehicle& removed) {
  if (combat_.attacker == &removed) {
    combat_.attacker = nullptr;
  }
  if (ai_.contact.vehicle == &removed) {
    ai_.contact.vehicle = nullptr;
  }
}

void EnemySub::receive_contact(SimContext& /*ctx*/, const fx::Vec3& position, Vehicle* target) {
  // ai_sub_receive_contact (0x1492d)
  if (ai_.contact.age == 0) {
    return;
  }
  const auto range = static_cast<std::int32_t>(
      fx::approx_distance(position.x - motion_.position.x, position.z - motion_.position.z) / kUnitsPerYard);
  if (range > ai_.sighting_range_yd) {
    return;
  }
  ai_.alert.level = 1;
  ai_.contact.heading = Angle10(0);
  ai_.contact.speed = Fixed16();
  ai_.contact.age = 0;
  ai_.contact.position = position;
  ai_.alert.timer = Fixed16::from_int((setup_.crew + 1) * 600);
  ai_.contact.vehicle = target;
}

std::int32_t EnemySub::power_for_depth() const {
  return motion_.position.y < 0 ? submerged_power_ : max_power_;
}

void EnemySub::navigate_orders(SimContext& ctx) {
  // ai_sub_navigate_orders (0x14378)
  if (orders_ == ShipOrder::kIntercept) {
    if (setup_.target_x == -1) {
      const PlayerSub* player = context_player(ctx);
      if (player != nullptr) {
        setup_.target_x = player->position().x;
        setup_.target_z = player->position().z;
      }
    }
    const std::int32_t range =
        ai_steer_to_point(motion_, hull_.turn_factor, setup_.target_x, setup_.target_z);
    if (range < 1000 && setup_.route.count > 0) {
      orders_ = ShipOrder::kFollowPath;
    }
  } else if (orders_ == ShipOrder::kFollowPath && setup_.route.count != 0 &&
             setup_.route_index < setup_.route.count) {
    motion_steer_to_waypoint(motion_, hull_.turn_factor, setup_.route, setup_.route_index);
  }
}

std::int32_t EnemySub::fire_torpedoes(SimContext& ctx) {
  // ai_sub_fire_torpedoes (0x1444a)
  if (setup_.torpedo == nullptr || ctx.sim == nullptr) {
    return 0;
  }
  const Torpedo& weapon = *setup_.torpedo;
  std::int32_t fired = 0;
  ai_.salvo = 1;
  std::int32_t offset = 0;
  for (std::int32_t i = 0; i < ai_.salvo; ++i, offset += 800) {
    const fx::Vec3 bow = fx::transform(motion_.orientation, {0, 0, hull_.length_ft * 0x20 + offset});
    const fx::Vec3 muzzle{motion_.position.x + bow.x, motion_.position.y - 0x20, motion_.position.z + bow.z};
    const std::int32_t setting = ai_.speed_setting;
    const auto speed_kn = static_cast<std::int32_t>(
        setting == 0 ? weapon.speed_low_kts : weapon.speed_high_kts);
    // Torpedo speed in 16.16 feet per second.
    const std::int32_t fps = fx::mul(speed_kn << 16, kFeetPerSecondPerKnot);
    std::int32_t aim_x = ai_.contact.position.x;
    std::int32_t aim_z = ai_.contact.position.z;
    if (setup_.crew > 0) {
      // Lead: where the contact will be when a torpedo running straight
      // reaches its present range.
      const auto feet =
          static_cast<std::int32_t>(fx::approx_distance(aim_x - motion_.position.x, aim_z - motion_.position.z) >> 4);
      const std::int32_t run_s = fx::div(fx::imul(feet, 0x10000), fps);
      const fx::SinCos sc = fx::angle_sincos(ai_.contact.heading);
      const std::int32_t contact_fps = fx::mul(ai_.contact.speed.raw(), kFeetPerSecondPerKnot);
      aim_x += fx::trunc(fx::imul(fx::mul(fx::mul(contact_fps, sc.sin), run_s), kUnitsPerFoot));
      aim_z += fx::trunc(fx::imul(fx::mul(fx::mul(contact_fps, sc.cos), run_s), kUnitsPerFoot));
    }
    const Angle10 bearing = fx::heading_from_delta(aim_x - motion_.position.x, aim_z - motion_.position.z);
    std::int32_t own = motion_.heading.tenths();
    if (own > 1800) {
      own -= 3600;
    }
    std::int32_t to = bearing.tenths();
    if (to > 1800) {
      to -= 3600;
    }
    if (fx::abs(own - to) > (3 - setup_.crew) * 250) {
      continue;
    }
    const auto feet =
        static_cast<std::int32_t>(fx::approx_distance(aim_x - motion_.position.x, aim_z - motion_.position.z) >> 4);
    const std::int32_t wait = (ctx.random.random_int(1) + 1) * kMinute;
    ai_.cooldown = wait + fx::div(fx::imul(feet, 0x10000), fps);
    const Angle10 course = add(bearing, kSpread[static_cast<std::size_t>(i % 5)] * 10);
    const Fixed16 speed = Fixed16::from_int(speed_kn);
    auto torpedo = std::make_unique<TorpedoVehicle>(weapon, this, muzzle, bearing, motion_.pitch, speed, setting,
                                                    course, -ai_.contact.position.y / kUnitsPerFoot);
    ctx.sim->add(std::move(torpedo));
    ++fired;
  }
  return fired;
}

void EnemySub::think(SimContext& ctx, std::int32_t dt) {
  // ai_sub_think (0x13471)
  Sensors& sensors = *context_sensors(ctx);
  const SensorEnvironment& env = sensors.environment();
  MotionState& m = motion_;
  ai_.think_time += dt;
  if (ai_.think_time < kSecond) {
    return;
  }
  if (ctx.clock.effective_compression() == 1 && list_index() % 8 != sensors.frame_counter()) {
    return;
  }
  ai_.attacked_player = false;
  const std::int32_t elapsed = ai_.think_time;
  ai_.think_time = 0;
  if (sinking() || orders_ == ShipOrder::kNone || setup_.ship_class == nullptr) {
    return;
  }
  const ShipClass& cls = *setup_.ship_class;
  const EnemySubMode mode_at_start = ai_.mode;
  const std::int32_t crew = setup_.crew;
  if (ai_.torpedoes < 5) {
    ai_.reload_timer -= elapsed;
    if (ai_.reload_timer < 1) {
      ai_.torpedoes = std::min(ai_.torpedoes + 1, 5);
      ai_.reload_timer = (crew + 1) * 300 * kSecond;
    }
  } else {
    ai_.reload_timer = 0;
  }
  ai_.cooldown = std::max(ai_.cooldown - elapsed, 0);

  // The lookouts' scan.
  std::int32_t avoid_bearing = -1;
  std::int32_t best = INT_MAX;
  std::int32_t contact_bearing = -1;
  ai_.friendly_near_contact = false;
  PlayerSub* player = context_player(ctx);
  PlayerLookoutState player_state;
  if (player != nullptr) {
    player_state = player_lookout_state(*player, env.crew);
  }
  const SightingConditions conditions = env.sighting(ctx.env);
  const fx::Vec3 own = m.position;
  const fx::Vec3 eye{own.x, static_cast<std::int32_t>(cls.visual_size) * kUnitsPerFoot + own.y, own.z};
  const auto& vehicles = ctx.sim->vehicles();
  for (std::size_t index = 0; index < vehicles.size(); ++index) {
    Vehicle& v = *vehicles[index];
    if (&v == this || !v.is_contact() || v.is_aircraft() || v.plane_count() != 0 || v.is_pilot()) {
      continue;
    }
    const fx::Vec3 tpos = v.position();
    const Angle10 theading = v.heading();
    AiSightingTarget target{{tpos.x, v.visual_height(), tpos.z}, theading, nullptr, v.is_torpedo()};
    if (&v == player) {
      target.player = &player_state;
    }
    const AiSighting seen =
        ai_detect_target(conditions, ctx.env.sea_state, env.grid, eye, target, crew, cls.role == ShipRole::kMerchant);
    if (!seen.seen()) {
      continue;
    }
    const std::int32_t d = seen.distance_yd;
    if (&v == player && (ctx.env.options & sensor_option::kContactGroups) != 0 && env.contact_groups != nullptr) {
      env.contact_groups->engaged(*this);
    }
    const Fixed16 tspeed = v.speed();
    const Angle10 bearing = fx::heading_from_delta(tpos.x - own.x, tpos.z - own.z);
    const bool torpedo = v.is_torpedo();
    if (!torpedo && v.side() != side() && d < best) {
      ai_.contact.position = tpos;
      ai_.contact.heading = theading;
      ai_.contact.speed = tspeed;
      ai_.contact.vehicle = &v;
      ai_.contact.age = 0;
      best = d;
      contact_bearing = bearing.tenths();
      broadcast_contact(ctx, ai_.contact.position, &v);
    }
    const bool friendly = torpedo || v.side() == side() || v.side() == 2;
    if (friendly) {
      if (torpedo && ai_.contact.age > 0x1e0000) {
        if (const std::optional<fx::Vec3> origin = v.torpedo_launch_point()) {
          ai_.contact.position = {origin->x, 0, origin->z};
          ai_.contact.speed = Fixed16();
          ai_.contact.age = 0;
          ai_.contact.heading = theading;
          ai_.contact.vehicle = nullptr;
          broadcast_contact(ctx, ai_.contact.position, nullptr);
        }
      }
      if (!torpedo && v.side() != 2) {
        const auto near = static_cast<std::int32_t>(
            fx::approx_distance(ai_.contact.position.x - tpos.x, ai_.contact.position.z - tpos.z) / kUnitsPerYard);
        if (near < 2000 && ai_.contact.age >= 0 && !v.is_escort() && near < 300) {
          ai_.friendly_near_contact = true;
        }
      }
    }
    const std::int32_t t =
        motion_intercept_time({own.x, own.z, m.heading, m.speed}, {tpos.x, tpos.z, theading, tspeed}, 72000, 200);
    if (t < 0) {
      continue;
    }
    if (!friendly) {
      if (&v == player && t < 31 && d < 16000) {
        sensors.set_collision_warning(true);
      }
      continue;
    }
    if (t > 60) {
      continue;
    }
    // Where the friendly will be, to turn away from (the original scales by
    // the seconds as a 16.16 product).
    const fx::SinCos sc = fx::angle_sincos(theading);
    const std::int32_t v_units = fx::mul(tspeed.raw(), kUnitsPerSecondPerKnot);
    const std::int32_t ahead_x = fx::mul(fx::mul(v_units, sc.sin), t);
    const std::int32_t ahead_z = fx::mul(fx::mul(v_units, sc.cos), t);
    avoid_bearing = fx::heading_from_delta(ahead_x + tpos.x - own.x, ahead_z + tpos.z - own.z).tenths();
    break;
  }

  using Mode = EnemySubMode;
  if (avoid_bearing >= 0 && mode_at_start != Mode::kEvading && mode_at_start != Mode::kToWater) {
    ai_.mode = Mode::kEvadeStart;
  }
  if (mode_at_start == Mode::kEvading) {
    ai_.mode = Mode::kEvading;
  }
  if (m.keel_clearance > 0 && mode_at_start != Mode::kBackingOff) {
    ai_.mode = Mode::kAgroundStart;
  }
  if (mode_at_start == Mode::kBackingOff) {
    ai_.mode = Mode::kBackingOff;
  }
  const auto steer_to_contact = [&] {
    return ai_steer_to_point(m, hull_.turn_factor, ai_.contact.position.x, ai_.contact.position.z);
  };
  const auto search_deep = [&] {
    ai_.mode = Mode::kSearchDeep;
    ai_.timer2 = 0;
  };
  const auto find_water = [&] {
    ai_.saved_steering = m.steering;
    ai_.saved_rudder = m.ordered_heading.tenths();
    ai_.periscope_time = m.ordered_power;
    m.ordered_power = max_power_ / 8;
    m.steering = true;
    m.ordered_heading = ai_find_water_heading(ctx, own.x, own.z, m.heading);
    ai_.mode = Mode::kToWater;
    ai_.water_mode = mode_at_start;
  };

  switch (ai_.mode) {
    case Mode::kCruise:
      if (contact_bearing >= 0) {
        ai_.mode = Mode::kApproachStart;
        ai_.cruise_power = m.ordered_power;
      } else if (ai_.alert.level != 0) {
        ai_.cruise_power = m.ordered_power;
        ai_.mode = crew == 0 ? Mode::kSearchDeep : Mode::kCloseOnContact;
      } else {
        navigate_orders(ctx);
      }
      break;
    case Mode::kCloseOnContact:
      dive_.ordered_depth_ft = 64;
      m.ordered_power = submerged_power_;
      if (steer_to_contact() < 1000 || contact_bearing >= 0) {
        ai_.mode = Mode::kApproachStart;
      }
      break;
    case Mode::kEvadeStart:
      dive_.ordered_depth_ft = 150;
      ai_.saved_steering = m.steering;
      m.steering = false;
      ai_.saved_rudder = m.rudder;
      ai_.evade_power = m.ordered_power;
      m.rudder = m.rudder_max;
      m.ordered_power = 0;
      if (fx::angle_sub(Angle10(avoid_bearing), m.heading).tenths() < 1800) {
        m.rudder = -m.rudder;
      }
      ai_.timer = 0;
      ai_.mode = Mode::kEvading;
      ai_.water_mode = mode_at_start;
      break;
    case Mode::kEvading:
      ai_.timer = std::max(ai_.timer - elapsed, 0);
      if (ai_.timer == 0) {
        if (avoid_bearing < 0) {
          search_deep();
          m.ordered_power = ai_.evade_power;
          m.steering = ai_.saved_steering;
          m.rudder = ai_.saved_rudder;
        } else {
          ai_.timer = 30 * kSecond;
        }
      }
      break;
    case Mode::kApproachStart:
      dive_.ordered_depth_ft = 64;
      ai_.timer = (ctx.random.random_int(4) + 10) * kMinute;
      m.ordered_power = submerged_power_ / 4;
      ai_.mode = Mode::kApproach;
      break;
    case Mode::kApproach:
      if (contact_bearing >= 0) {
        ai_.mode = Mode::kAttackStart;
        break;
      }
      steer_to_contact();
      ai_.timer -= elapsed;
      if (ai_.timer <= 0) {
        search_deep();
      }
      break;
    case Mode::kAgroundStart:
      ai_.aground_power = m.ordered_power;
      ai_.aground_mode = mode_at_start;
      ai_.aground_steering = m.steering;
      ai_.aground_rudder = m.rudder;
      ai_.mode = Mode::kBackingOff;
      m.ordered_power = m.power < 0 ? max_power_ : -max_power_;
      break;
    case Mode::kBackingOff:
      m.steering = false;
      m.rudder = 0;
      if (hull_.draft_ft * -0x20 <= m.keel_clearance) {
        break;
      }
      m.ordered_power = ai_.aground_power;
      ai_.mode = ai_.aground_mode;
      m.steering = ai_.aground_steering;
      m.rudder = ai_.aground_rudder;
      find_water();
      break;
    case Mode::kFindWater:
      find_water();
      break;
    case Mode::kToWater:
      if (ai_.previous_clearance < m.keel_clearance && hull_.draft_ft * 0x20 <= m.keel_clearance) {
        break;
      }
      ai_.mode = ai_.water_mode;
      m.steering = ai_.saved_steering;
      m.ordered_heading = Angle10(ai_.saved_rudder);
      m.ordered_power = ai_.periscope_time;
      break;
    case Mode::kAttackStart:
      ai_.mode = Mode::kAttack;
      ai_.salvo = crew + 1;
      ai_.closing_count = 0;
      ai_.opening_time = 0;
      ai_.periscope_time = 0;
      ai_.last_range = 0;
      ai_.fire_range_yd = ctx.random.random_int(1500) + 1500;
      break;
    case Mode::kAttack: {
      dive_.ordered_depth_ft = 64;
      m.ordered_power = power_for_depth();
      const std::int32_t range = steer_to_contact();
      if (dive_.depth_ft < 65) {
        ai_.periscope_time += elapsed;
      }
      if (ai_.last_range < range) {
        ++ai_.closing_count;
        ai_.opening_time += elapsed;
      } else {
        ai_.closing_count = 0;
        ai_.opening_time = 0;
      }
      ai_.last_range = range;
      const auto range_long = static_cast<std::int32_t>(setup_.torpedo != nullptr ? setup_.torpedo->range_long_yd : 0);
      const auto range_short =
          static_cast<std::int32_t>(setup_.torpedo != nullptr ? setup_.torpedo->range_short_yd : 0);
      if (ai_.min_contact_y <= ai_.contact.position.y &&
          (ai_.opening_time < 0xb40001 || ai_.closing_count < 11 ||
           (range_short <= range && range_long / 2 <= range))) {
        if (range <= ai_.fire_range_yd && ai_.periscope_time > 0x3bffff) {
          if (range < 401) {
            ai_.cooldown = 300 * kSecond;
          } else {
            ai_.speed_setting = 1;
            if (fire_torpedoes(ctx) == 0) {
              break;
            }
          }
          m.ordered_power = submerged_power_ / 8;
          m.steering = true;
          m.ordered_heading =
              ctx.random.random_int(2) == 0 ? fx::angle_sub(m.heading, Angle10(900)) : add(m.heading, 900);
          ai_.mode = Mode::kGoDeep;
        }
        break;
      }
      // The contact is opening out of reach: a last salvo, then search.
      ai_.speed_setting = range < range_short ? 1 : 0;
      fire_torpedoes(ctx);
      search_deep();
      break;
    }
    case Mode::kGoDeep:
      dive_.ordered_depth_ft = 150;
      if (ai_.cooldown == 0) {
        ai_.mode = Mode::kApproachStart;
      }
      break;
    case Mode::kSearchDeep:
      dive_.ordered_depth_ft = 100;
      m.ordered_power = power_for_depth();
      ai_.timer2 += elapsed;
      if (ai_.timer2 > 0xb3ffff) {
        m.ordered_power = power_for_depth() / 4;
      }
      if ((crew + 1) * 0x7080000 <= ai_.timer2) {
        dive_.ordered_depth_ft = 60;
        ai_.mode = Mode::kListen;
        break;
      }
      navigate_orders(ctx);
      break;
    case Mode::kListen:
      m.ordered_power = power_for_depth() / 8;
      if (dive_.depth_ft > 64 || ai_.contact.age == 0) {
        break;
      }
      if (contact_bearing < 0) {
        ai_.alert.level = 0;
        dive_.ordered_depth_ft = 0;
        ai_.mode = Mode::kCruise;
        m.ordered_power = ai_.cruise_power;
      } else {
        search_deep();
      }
      break;
  }
  if (ai_.contact.age >= 0) {
    ai_.contact.age += elapsed;
  }
  ai_.previous_clearance = m.keel_clearance;
}

void EnemySub::update_bounds() {
  // enemy_sub_update_bbox (0x7f4dd)
  const ShipClass* cls = setup_.ship_class;
  if (cls == nullptr) {
    return;
  }
  const auto half_z = static_cast<std::int32_t>(cls->length_ft) * kUnitsPerFoot / 2;
  const auto half_x = static_cast<std::int32_t>(cls->beam_ft) * kUnitsPerFoot / 2;
  const std::int32_t keel = motion_.position.y - static_cast<std::int32_t>(cls->draft_ft) * kUnitsPerFoot;
  const std::int32_t top = motion_.position.y + static_cast<std::int32_t>(cls->deck_height_ft) * kUnitsPerFoot;
  collision().box = make_hull_box(motion_.position, motion_.orientation, motion_.inverse, half_x, half_z, keel, top);
}

void EnemySub::hydrophone_check(SimContext& ctx) {
  // enemy_sub_hydrophone_check (0x7022b); the screw sound loop is left to
  // the audio layer.
  PlayerSub* player = context_player(ctx);
  const ShipClass* cls = setup_.ship_class;
  if (player == nullptr || cls == nullptr) {
    return;
  }
  const bool was_heard = heard_;
  const fx::Vec3 own = player->position();
  const std::int32_t dx = motion_.position.x - own.x;
  const std::int32_t dz = motion_.position.z - own.z;
  const auto distance = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
  heard_ = false;
  report_timer_ = std::max(report_timer_ - ctx.clock.frame_ticks(), 0);
  if (dive_.depth_ft < 1 && player->sub().bridge_height_ft < player->depth_ft()) {
    const std::int32_t relative = fx::angle_sub(fx::heading_from_delta(dx, dz), player->motion().heading).tenths();
    const bool sonar_damaged = player->systems()[static_cast<std::size_t>(PlayerSystem::kSoundHeads)].impaired();
    std::int32_t noise = player_noise_level(*player) * 8000 / 100;
    if (relative > 1700 && relative < 1900) {
      noise *= 5;
    }
    if (ctx.env.thermal_layer_ft <= player->depth_ft()) {
      noise <<= 2;
    }
    const std::int32_t screws = thrust_hp(motion_.power) * 100 / max_power_ * 6000 / 100 - noise;
    if (screws > 0 && distance < screws) {
      if (Sensors* sensors = context_sensors(ctx)) {
        sensors->reset_quiet_time();
      }
      if (!sonar_damaged && cls->screw_sound != ScrewSound::kNone) {
        heard_bearing_ = Angle10(relative);
        heard_ = true;
        if (!was_heard && report_timer_ == 0) {
          ctx.crew_message(cls->screw_sound == ScrewSound::kHighSpeed ? 41 : 42, relative / 10);
          ctx.clock.reset_compression();
          report_timer_ = kScrewsReportTicks;
        }
        if (ctx.env.rules.quiet_sonar_reacquire && report_timer_ > 0) {
          report_timer_ = kScrewsReportTicks;
        }
      }
    }
  } else {
    report_timer_ = 0;
  }
}

std::int32_t EnemySub::radar_cross_section(const fx::Vec3& /*observer*/, Angle10 /*observer_heading*/) const {
  // enemy_sub_radar_cross_section (0x7fe01)
  if (motion_.position.y < kDeepY || setup_.ship_class == nullptr) {
    return 0;
  }
  return static_cast<std::int32_t>(setup_.ship_class->length_ft) * 100;
}

std::int32_t EnemySub::noise_level() const {
  // enemy_sub_noise_level (0x7fd79)
  if (sinking()) {
    return 30;
  }
  return std::min(thrust_hp(motion_.power) * 100 / std::max(max_power_, 1) + 20, 100);
}

void EnemySub::mark_sighted(const GameClock& clock) {
  // enemy_sub_mark_sighted (0x7fe23); the TDC estimate belongs to the fire
  // control.
  sighting_.sighted = true;
  sighting_.age = 0;
  sighting_.last_x = motion_.position.x;
  sighting_.last_z = motion_.position.z;
  sighting_.hour = clock.hour();
  sighting_.minute = clock.minute();
  sighting_.month = clock.month();
  sighting_.day = clock.day();
}

std::int32_t EnemySub::add_track_time(std::int32_t dt, bool selected) {
  if (dt >= 0 && selected) {
    sighting_.track_time += dt;
  }
  return sighting_.track_time;
}

void EnemySub::refresh_sighted() { sighting_.sighted = sighting_.age < kSecond && sighting_.last_x >= 0; }

std::int32_t EnemySub::visual_height() const {
  // enemy_sub_visual_height (0x19b40)
  return motion_.position.y +
         static_cast<std::int32_t>(setup_.ship_class != nullptr ? setup_.ship_class->visual_size : 0) * kUnitsPerFoot;
}

std::int32_t EnemySub::exposure() const {
  // enemy_sub_exposure (0x7ff45)
  if (motion_.position.y < kDeepY) {
    return 5;
  }
  const std::int32_t lookout = setup_.ship_class != nullptr ? static_cast<std::int32_t>(setup_.ship_class->deck_height_ft) : 0;
  return lookout * kUnitsPerFoot + motion_.position.y < 0 ? 2 : 1;
}

}  // namespace sh
