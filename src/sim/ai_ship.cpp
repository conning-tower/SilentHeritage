#include "sim/ai_ship.h"

#include <array>
#include <cstddef>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "sim/aircraft.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/intercept.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/ship_combat.h"
#include "sim/simulation.h"
#include "sim/sonar.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kSecond = 0x10000;

Angle10 add(Angle10 a, std::int32_t b) { return fx::angle_add(a, Angle10(b)); }
Angle10 sub(Angle10 a, std::int32_t b) { return fx::angle_sub(a, Angle10(b)); }

// The ordered power for `speed_raw` (16.16 kn) (`motion_power_for_speed`
// into +0x2b8).
std::int32_t power_for(const Ship& ship, const SimContext& ctx, std::int32_t speed_raw) {
  return motion_power_for_speed(Fixed16::from_raw(speed_raw), ship.params().max_power, ship.params().hull,
                                ctx.env.sea_state, 100);
}

const std::vector<std::unique_ptr<Vehicle>>& vehicle_list(const SimContext& ctx) {
  static const std::vector<std::unique_ptr<Vehicle>> kNone;
  return ctx.sim != nullptr ? ctx.sim->vehicles() : kNone;
}

// The leader's route with the array overlaps of the original object: x[25]
// is z[0], z[25] the route index.
std::int32_t route_x(const ShipSetup& s, std::int32_t i) {
  return i < Route::kCapacity ? s.route.x[static_cast<std::size_t>(i)] : s.route.z[0];
}
std::int32_t route_z(const ShipSetup& s, std::int32_t i) {
  return i < Route::kCapacity ? s.route.z[static_cast<std::size_t>(i)] : s.route_index;
}

// Copies the ship's leadership fields to `heir` (`ship_hand_over_convoy_lead`).
void make_leader(Ship& heir, const Ship& old) {
  heir.set_orders(ShipOrder::kConvoyLeader);
  heir.setup().zigzag = old.setup().zigzag;
  heir.setup().route_index = old.setup().route_index;
  heir.setup().route.count = old.setup().route.count;
  heir.setup().crew = old.setup().crew;
  heir.setup().route.x = old.setup().route.x;
  heir.setup().route.z = old.setup().route.z;
}

// ai_ship_receive_contact (0x12ac9) is the ship's slot +0xc8, below.

// ai_ship_steer_to_contact (0x12b68): a friendly ship close to the contact
// turns the attack into a gun circle; returns -1 then, else the range.
std::int32_t steer_to_contact(Ship& ship) {
  ShipAi& ai = ship.ai();
  if (ai.friendly_near_contact) {
    ai.gun_circle_mode = ai.mode;
    ai.mode = ShipAiMode::kGunCircleFriendly;
    return -1;
  }
  return ai_steer_to_point(ship, ai.contact.position.x, ai.contact.position.z);
}

void fire_guns(Ship& ship, SimContext& ctx, std::int32_t dt, std::span<const Angle10> friendly) {
  Sensors* sensors = context_sensors(ctx);
  if (sensors != nullptr && sensors->environment().weapons != nullptr) {
    sensors->environment().weapons->fire_guns(ship, ctx, dt, friendly);
  }
}

// ai_ship_depth_charge_run (0x12bba): steers the stern over the contact
// (led by 800 units along its course by elite crews) and drops a pattern
// when within 20 yd or once the range has opened for a second inside
// 300 yd.
void depth_charge_run(Ship& ship, SimContext& ctx, std::int32_t dt) {
  ShipAi& ai = ship.ai();
  MotionState& m = ship.motion();
  const fx::Vec3 stern = fx::transform(m.orientation, {0, 0, ai.cls.length_ft / 2 * -kUnitsPerFoot});
  ai.dropping = true;
  if (ai.contact.vehicle != nullptr && ai.contact.vehicle == context_player(ctx)) {
    ai.attacked_player = true;
  }
  std::int32_t lead_x = 0;
  std::int32_t lead_z = 0;
  if (ship.setup().crew == 2 && ai.contact.speed.raw() > 0) {
    const fx::SinCos sc = fx::angle_sincos(ai.contact.heading);
    lead_x = fx::mul(800, sc.sin);
    lead_z = fx::mul(800, sc.cos);
  }
  const std::int32_t range =
      ai_steer_to_point(ship, ai.contact.position.x + lead_x, ai.contact.position.z + lead_z, stern.x, stern.z);
  std::int32_t depth_ft = -ai.contact.position.y / kUnitsPerFoot;
  if ((ctx.env.options & sensor_option::kRealismLevelMask) < 0xc && depth_ft > 200) {
    depth_ft = 200;
  }
  if (ship.setup().crew == 0 && depth_ft > 150) {
    depth_ft = 150;
  }
  if (range < 401) {
    if (range < 300) {
      if (range < ai.last_run_range) {
        ai.opening_time = 0;
      } else {
        ai.opening_time += dt;
      }
      ai.last_run_range = range;
    }
  } else {
    ai.opening_time = 0;
  }
  if (range < 20 || ai.opening_time > kSecond) {
    Sensors* sensors = context_sensors(ctx);
    if (sensors != nullptr && sensors->environment().weapons != nullptr) {
      sensors->environment().weapons->drop_depth_charges(ship, ctx, depth_ft);
    }
    ai.opening_time = 0;
    ai.mode = ShipAiMode::kOpening;
    ai.timer = (ctx.random.random_int(60) + 60) * kSecond;
    if (ship.setup().crew > 0) {
      m.steering = false;
      m.rudder = (ctx.random.random_int(8) + 2) * 10;
    }
  }
  ai.last_run_range = range;
}

// ship_update_motion (0x81351): the formation moves, else motion_step.
void ship_update_motion(Ship& ship, SimContext& ctx, std::int32_t dt) {
  ShipAi& ai = ship.ai();
  MotionState& m = ship.motion();
  if (ship.sinking() || ship.orders() == ShipOrder::kNone) {
    return;
  }
  const auto step = [&] {
    motion_step(m, ship.params().hull, ctx.env.sea_state, Fixed16::from_raw(dt), 100, ctx.floor);
  };
  switch (ship.orders()) {
    case ShipOrder::kEscort:
      if (ai.leader != nullptr) {
        // The leader's moves gather; whole units shift the patrol points.
        ai.leader_dx += ai.leader->ai().leader_dx;
        ai.leader_dz += ai.leader->ai().leader_dz;
        const std::int64_t whole_x = ai.leader_dx >> 16;
        const std::int64_t whole_z = ai.leader_dz >> 16;
        ai.leader_dx -= whole_x * 0x10000;
        ai.leader_dz -= whole_z * 0x10000;
        Route& route = ship.setup().route;
        for (std::int32_t i = 0; i < route.count; ++i) {
          route.x[static_cast<std::size_t>(i)] += static_cast<std::int32_t>(whole_x);
          route.z[static_cast<std::size_t>(i)] += static_cast<std::int32_t>(whole_z);
        }
      }
      step();
      return;
    case ShipOrder::kConvoyLeader: {
      const std::int64_t x0 = m.pos_x;
      const std::int64_t z0 = m.pos_z;
      step();
      ai.leader_dx = m.pos_x - x0;
      ai.leader_dz = m.pos_z - z0;
      return;
    }
    case ShipOrder::kConvoyMember: {
      if (ai.leader == nullptr) {
        return;
      }
      const Ship& leader = *ai.leader;
      const MotionState& lm = leader.motion();
      m.pos_x += leader.ai().leader_dx;
      m.pos_z += leader.ai().leader_dz;
      m.position.x = static_cast<std::int32_t>(m.pos_x >> 16);
      m.position.z = static_cast<std::int32_t>(m.pos_z >> 16);
      m.velocity = lm.velocity;
      m.orientation = lm.orientation;
      m.inverse = lm.inverse;
      m.pitch = lm.pitch;
      m.pitch_rad = lm.pitch_rad;
      m.power = lm.power;
      m.ordered_power = lm.ordered_power;
      m.speed = lm.speed;
      m.speed_tenths = lm.speed_tenths;
      m.rudder = lm.rudder;
      m.heading = lm.heading;
      m.heading_rad = lm.heading_rad;
      m.ordered_heading = lm.ordered_heading;
      m.steering = lm.steering;
      m.acceleration = lm.acceleration;
      motion_update_wake(m, dt);
      m.keel_clearance = ctx.floor.height_at(m.position.x, m.position.z) + ai.cls.draft_ft * kUnitsPerFoot -
                         m.position.y;
      return;
    }
    default:
      step();
      return;
  }
}

}  // namespace

ShipAiClass ShipAiClass::from(const ShipClass& c) {
  ShipAiClass a;
  a.type = c.ship_type;
  a.role = c.role;
  a.screw = c.screw_sound;
  a.has_sonar = c.has_sonar;
  a.depth_charges = static_cast<std::int32_t>(c.depth_charges);
  a.rack_charges = static_cast<std::int32_t>(c.dc_racks);
  a.thrower_charges = static_cast<std::int32_t>(c.dc_throwers);
  a.gun_count = static_cast<std::int32_t>(c.main_gun_count);
  a.length_ft = static_cast<std::int32_t>(c.length_ft);
  a.draft_ft = static_cast<std::int32_t>(c.draft_ft);
  a.visual_size = static_cast<std::int32_t>(c.visual_size);
  a.lookout_ft = static_cast<std::int32_t>(c.deck_height_ft);
  return a;
}

void ai_ship_init(Ship& ship, SimContext& ctx) {
  // The rest of ship_init (0x80e13) before its motion setup.
  ShipAi& ai = ship.ai();
  if (ship.setup().ship_class != nullptr) {
    ai.cls = ShipAiClass::from(*ship.setup().ship_class);
  }
  ai.depth_charges = ai.cls.depth_charges;
  // Route pruning: while a point is in shallow water (less than 1.5 drafts)
  // or its leg to the next crosses land, the *next* point is dropped and
  // the check moves on past it (as the original's loop does). With the
  // rules' corrections the failing point is dropped (the point in shallow
  // water, or the next one when only the leg crosses land) and the check
  // runs again on the point's new leg (not in the original).
  ShipSetup& s = ship.setup();
  Route& r = s.route;
  const Sensors* sensors = context_sensors(ctx);
  const TerrainGrid* grid = sensors != nullptr ? sensors->environment().grid : nullptr;
  const bool fixed = ctx.env.rules.fix_obvious_errors;
  for (std::int32_t i = 0; i < r.count; ++i) {
    const std::int32_t x = r.x[static_cast<std::size_t>(i)];
    const std::int32_t z = r.z[static_cast<std::size_t>(i)];
    std::int32_t next_x = route_x(s, i + 1);
    std::int32_t next_z = route_z(s, i + 1);
    if (r.count - 1 == i) {
      if (next_x != -1) {
        break;
      }
      next_x = r.x[0];
      next_z = r.z[0];
    }
    const bool deep = ctx.floor.height_at(x, z) < ai.cls.draft_ft * -0x30 / 2;
    if (deep && (grid == nullptr || !land_on_line(*grid, {x, z}, {next_x, next_z}).land)) {
      break;
    }
    const std::int32_t dropped = fixed && !deep ? i : i + 1;
    for (std::int32_t j = dropped; j <= r.count && j < Route::kCapacity; ++j) {
      r.x[static_cast<std::size_t>(j)] = route_x(s, j + 1);
      r.z[static_cast<std::size_t>(j)] = route_z(s, j + 1);
    }
    --r.count;
    if (fixed) {
      --i;
    }
  }
}

void ai_ship_update(Ship& ship, SimContext& ctx, Fixed16 dt_s) {
  // ship_update (0x81170), after the damage state.
  const std::int32_t dt = dt_s.raw();
  ShipAi& ai = ship.ai();
  Sensors* sensors = context_sensors(ctx);
  const PlayerSub* player = context_player(ctx);
  const SensorEnvironment no_sensors;
  const SensorEnvironment& env = sensors != nullptr ? sensors->environment() : no_sensors;
  if (player != nullptr) {
    // Its sighting range from the player's periscope height (+0x1f4).
    const fx::Vec3 pos = ship.position();
    const fx::Vec3 eye{player->position().x, player->sub().periscope_height_ft * kUnitsPerFoot,
                       player->position().z};
    ai.sighting_range_yd =
        sighting_check(env.sighting(ctx.env), env.grid, eye, {pos.x, ship.visual_height(), pos.z}).range.range_yd;
  }
  if (sensors != nullptr) {
    ai_ship_think(ship, ctx, dt);
  }
  ship_update_motion(ship, ctx, dt);
  ship_update_effects(ctx, ship, dt_s);
  ShipSightingRecord& seen = ai.sighting;
  if (seen.last_x >= 0) {
    seen.age += dt;
    if (seen.track_time != 0) {
      const bool selected = &ship == env.tdc_target || &ship == env.gun_target;
      if (!ship.sighted() || !selected) {
        seen.track_time -= dt;
        if (seen.track_time < 0) {
          seen.track_time = 0;
        }
      }
    }
    if (seen.age > 0x1e00000) {
      seen.last_x = -1;
      seen.track_time = 0;
      seen.sighted = false;
    }
  }
  if (ship.orders() == ShipOrder::kNone) {
    MotionState& m = ship.motion();
    m.velocity = {};
    m.speed = Fixed16();
    m.wake_count = 0;
    m.speed_tenths = 0;
  }
  if (player != nullptr) {
    // The player's lookouts on it where it now is, in clear weather
    // without Limited Visibility.
    const fx::Vec3 pos = ship.position();
    SightingConditions conditions{0, 0, 100};
    if ((ctx.env.options & sensor_option::kLimitedVisibility) != 0) {
      conditions = env.sighting(ctx.env);
    }
    const SightingCheck check = player_sighting_check(player_lookout_state(*player, env.crew), conditions,
                                                      env.grid, pos.x, ship.visual_height(), pos.z, ship.exposure());
    ai.player_range_yd = check.range.range_yd;
    ai.player_distance_yd = check.distance_yd;
  }
}

void ai_ship_think(Ship& ship, SimContext& ctx, std::int32_t dt) {
  // ai_ship_think (0x11000)
  ShipAi& ai = ship.ai();
  MotionState& m = ship.motion();
  Sensors& sensors = *context_sensors(ctx);
  const SensorEnvironment& env = sensors.environment();
  ShipAlert& alert = ship.alert();
  ShipReactionRolls& rolls = ship.reaction_rolls();
  const std::int32_t max_power = ship.params().max_power;
  const std::int32_t crew = ship.setup().crew;
  if (env.station != SensorStation::kChart) {
    ai.sonar.chart_beam = -1;
    ai.sonar.chart_echo = false;
  }
  ai.think_time += dt;
  if (ai.think_time < kSecond) {
    return;
  }
  if (ctx.clock.effective_compression() == 1 && ship.list_index() % 8 != sensors.frame_counter()) {
    return;
  }
  ai.attacked_player = false;
  const std::int32_t elapsed = ai.think_time;
  ai.think_time = 0;
  if (ship.sinking() || ship.orders() == ShipOrder::kNone) {
    return;
  }
  if (ship.orders() == ShipOrder::kFishing) {
    ai_ship_navigate_orders(ship, ctx, elapsed);
    return;
  }

  // The lookouts' scan.
  std::int32_t gun_bearing = -1;      // A friendly's predicted bearing on a collision course.
  std::int32_t best = 0x7fffffff;     // The nearest hostile seen.
  const std::int32_t unset_best = 0x7fffffff;  // The original compares with a value it never updates.
  std::int32_t contact_bearing = -1;
  bool submerged_contact = false;
  std::int32_t self_index = 0;
  const ShipAiMode mode_at_start = ai.mode;
  std::vector<Angle10> friendly_bearings;  // 0x1161b0
  bool collision_course = false;
  ai.friendly_near_contact = false;
  bool saw_debris = false;
  ai.dropping = false;
  PlayerSub* player = context_player(ctx);
  PlayerLookoutState player_state;
  if (player != nullptr) {
    player_state = player_lookout_state(*player, env.crew);
  }
  const SightingConditions conditions = env.sighting(ctx.env);
  const fx::Vec3 own = m.position;
  const fx::Vec3 eye{own.x, ai.cls.lookout_ft * kUnitsPerFoot + own.y, own.z};
  if (ship.orders() != ShipOrder::kConvoyMember || ai.cls.gun_count > 0) {
    const auto& vehicles = vehicle_list(ctx);
    for (std::size_t index = 0; index < vehicles.size(); ++index) {
      Vehicle& v = *vehicles[index];
      if (&v == &ship) {
        self_index = static_cast<std::int32_t>(index);
        continue;
      }
      if (!v.is_contact() || v.is_aircraft() || v.plane_count() != 0 || v.is_pilot()) {
        continue;
      }
      const fx::Vec3 tpos = v.position();
      const Angle10 theading = v.heading();
      AiSightingTarget target{{tpos.x, v.visual_height(), tpos.z}, theading, nullptr, v.is_torpedo()};
      if (&v == player) {
        target.player = &player_state;
      }
      const AiSighting seen = ai_detect_target(conditions, ctx.env.sea_state, env.grid, eye, target, crew,
                                               ai.cls.role == ShipRole::kMerchant);
      if (!seen.seen()) {
        continue;
      }
      const std::int32_t d = seen.distance_yd;
      if (&v == player && (ctx.env.options & sensor_option::kContactGroups) != 0 &&
          env.contact_groups != nullptr) {
        env.contact_groups->engaged(ship);
      }
      const Fixed16 tspeed = v.speed();
      const Angle10 bearing = fx::heading_from_delta(tpos.x - own.x, tpos.z - own.z);
      if (v.is_debris()) {
        saw_debris = true;
        if (&v != ai.debris) {
          ai.debris = &v;
          // A crew that the debris does not fool hunts it as a contact.
          if (!rolls.fooled_by_debris && contact_bearing < 0) {
            ai.contact.position.x = tpos.x;
            ai.contact.position.y = ctx.random.random_int(300) << 4;
            ai.contact.position.z = tpos.z;
            ai.contact.heading = theading;
            ai.contact.age = 0;
            submerged_contact = false;
            ai.contact.speed = tspeed;
            best = d;
            ai.contact.vehicle = &v;
          }
        }
        continue;
      }
      const bool torpedo = v.is_torpedo();
      if (!torpedo && v.side() != ship.side() && d < best) {
        ai.contact.position = tpos;
        ai.contact.heading = theading;
        ai.contact.speed = tspeed;
        best = d;
        contact_bearing = bearing.tenths();
        submerged_contact = !seen.aspect_applied;
        ai.contact.vehicle = &v;
        ai.contact.age = 0;
        broadcast_contact(ctx, ai.contact.position, &v);
      }
      if (torpedo || v.side() == ship.side() || v.side() == 2) {
        if (torpedo && ai.contact.age > 0x1e0000) {
          if (const std::optional<fx::Vec3> origin = v.torpedo_launch_point()) {
            // Torpedoes give their firing point away.
            ai.contact.position = {origin->x, 0, origin->z};
            ai.contact.speed = Fixed16();
            ai.contact.age = 0;
            ai.contact.heading = theading;
            ai.contact.vehicle = nullptr;
            broadcast_contact(ctx, ai.contact.position, nullptr);
          }
        }
        if (!torpedo && v.side() != 2) {
          const auto near = static_cast<std::int32_t>(
              fx::approx_distance(ai.contact.position.x - tpos.x, ai.contact.position.z - tpos.z) / kUnitsPerYard);
          if (near < 2000) {
            friendly_bearings.push_back(bearing);
            if (ai.contact.age >= 0 && !v.is_escort() && near < 300) {
              ai.friendly_near_contact = true;
            }
            if (ai.contact.age >= 0 && ship.dropping_charges()) {
              collision_course = true;
            }
          }
        }
      }
      if (ship.orders() == ShipOrder::kConvoyMember) {
        continue;
      }
      const std::int32_t t = motion_intercept_time({own.x, own.z, m.heading, m.speed},
                                                   {tpos.x, tpos.z, theading, tspeed}, 72000, 200);
      if (t < 0) {
        continue;
      }
      if (!torpedo && v.side() != ship.side() && v.side() != 2) {
        if (&v == player && t < 31 && d < 16000) {
          sensors.set_collision_warning(true);
        }
      } else if (t < 61 && d < unset_best) {
        gun_bearing = ai_friendly_ahead_bearing(own, tpos, theading, tspeed, t).tenths();
        break;
      }
    }
  }

  const auto guns = [&] { fire_guns(ship, ctx, elapsed, friendly_bearings); };
  if (ai.cls.gun_count < 1 || contact_bearing < 0 || gun_bearing >= 0 || ai.mode == ShipAiMode::kGunCircle ||
      ai.mode == ShipAiMode::kGunCircleFriendly) {
    if (!ship.is_escort()) {
      ai.gun_accuracy = 1000;
    }
  } else if (!ship.is_escort()) {
    guns();
  } else if (!submerged_contact && ai.mode != ShipAiMode::kGunAttack) {
    ai.mode = ShipAiMode::kGunApproach;
    ai.sonar.active = false;
  } else if (submerged_contact && ai.mode != ShipAiMode::kAttackRun && ai.mode != ShipAiMode::kOpening) {
    ai.mode = ShipAiMode::kAttackStart;
  }
  if (!ai.sonar.active) {
    if (alert.level != 0 && ai.cls.has_sonar && ai.mode == ShipAiMode::kCruise) {
      ai.sonar.active = true;
    }
  } else if (submerged_contact) {
    ai.sonar.echo = true;
  }
  ai_ship_sonar_search(ship, ctx, elapsed);
  if (ai.previous_clearance < m.keel_clearance && ai.cls.draft_ft * -16 / 2 <= m.keel_clearance) {
    m.ordered_power = max_power / 7;
  }
  if (gun_bearing >= 0 && mode_at_start != ShipAiMode::kEvading && mode_at_start != ShipAiMode::kToWater) {
    ai.mode = ShipAiMode::kEvadeStart;
  }
  if (mode_at_start == ShipAiMode::kEvading) {
    ai.mode = ShipAiMode::kEvading;
  }
  if (m.keel_clearance > 0 && mode_at_start != ShipAiMode::kBackingOff) {
    ai.mode = ShipAiMode::kAgroundStart;
  }
  if (mode_at_start == ShipAiMode::kBackingOff) {
    ai.mode = ShipAiMode::kBackingOff;
  }

  // The shared tails of the states.
  const auto restore_power = [&] { m.ordered_power = ai.saved_power; };
  const auto stand_down = [&] {
    restore_power();
    ai.contact.age = -1;
    alert.previous = alert.level;
    alert.level = 0;
  };
  const auto fooled = [&] { return ai.charges_dropped != 0 && saw_debris && rolls.fooled_by_debris; };
  const auto give_up_fooled = [&] {
    rolls.fooled_by_debris = false;
    ai.mode = ShipAiMode::kCruise;
    ai.sonar.active = false;
    alert.timer = Fixed16();
  };

  switch (ai.mode) {
    case ShipAiMode::kCruise: {
      if (ship.is_escort() && alert.level != alert.previous) {
        if (alert.level == 0) {
          if (ship.orders() != ShipOrder::kEscort) {
            m.ordered_power = max_power * 2 / 3;
          } else if (const Ship* leader = convoy_find_leader(vehicle_list(ctx), ship.setup().convoy_group)) {
            m.ordered_power = power_for(ship, ctx, leader->motion().speed.raw() * 2);
          }
        } else {
          std::int32_t speed = 0xc0000;
          if (ship.orders() == ShipOrder::kEscort) {
            const Ship* leader = convoy_find_leader(vehicle_list(ctx), ship.setup().convoy_group);
            if (leader != nullptr && speed < leader->motion().speed.raw()) {
              speed = leader->motion().speed.raw() * 3 / 2;
            }
          }
          m.ordered_power = power_for(ship, ctx, speed);
        }
      }
      if (alert.level != 0) {
        alert.timer -= Fixed16::from_raw(elapsed);
        if (alert.timer.raw() < 1) {
          alert.timer = Fixed16();
          alert.previous = alert.level;
          alert.level = 0;
        }
      }
      if (alert.level != 0 && ship.is_escort() && ai.contact.age == 0) {
        ai.mode = ShipAiMode::kInvestigate;
      }
      if (ship.orders() != ShipOrder::kConvoyLeader && ship.orders() != ShipOrder::kFollowPath) {
        // A merchant of an alerted convoy may break away on the leader's
        // route (its budget allowing).
        if (ship.orders() == ShipOrder::kConvoyMember && ai.cls.role == ShipRole::kMerchant &&
            alert.level != alert.previous && ai.leader != nullptr && ai.leader->reaction_rolls().breakaways_left != 0 &&
            ctx.random.random_int(5) == 0) {
          ship.set_orders(ShipOrder::kFollowPath);
          ship.setup().route = ai.leader->setup().route;
          ship.setup().route_index = ai.leader->setup().route_index;
          rolls.flee_angle = (ctx.random.random_int(180) + 90) * 10;
          --ai.leader->reaction_rolls().breakaways_left;
        }
        ai_ship_navigate_orders(ship, ctx, elapsed);
        break;
      }
      if (alert.level == alert.previous) {
        rolls.flee_angle = (ctx.random.random_int(90) + 135) * 10;
        ai_ship_navigate_orders(ship, ctx, elapsed);
        break;
      }
      // An alerted leader or straggler runs away from the contact.
      m.ordered_power = max_power;
      m.ordered_heading =
          add(fx::heading_from_delta(ai.contact.position.x - own.x, ai.contact.position.z - own.z), rolls.flee_angle);
      break;
    }
    case ShipAiMode::kCloseOnContact:
      if (ai_steer_to_point(ship, ai.contact.position.x, ai.contact.position.z) < 1000) {
        ai.mode = ShipAiMode::kHunt;
      }
      break;
    case ShipAiMode::kEvadeStart:
      ai.saved_steering = m.steering;
      m.steering = false;
      ai.saved_rudder = m.rudder;
      ai.evade_power = m.ordered_power;
      m.rudder = m.rudder_max;
      m.ordered_power = 0;
      if (fx::angle_sub(Angle10(gun_bearing), m.heading).tenths() < 1800) {
        m.rudder = -m.rudder;
      }
      ai.timer = 0;
      ai.mode = ShipAiMode::kEvading;
      ai.resume_mode = mode_at_start;
      break;
    case ShipAiMode::kEvading:
      ai.timer -= elapsed;
      if (ai.timer < 0) {
        ai.timer = 0;
      }
      if (ai.timer == 0) {
        if (gun_bearing < 0) {
          ai.mode = ai.resume_mode;
          m.ordered_power = ai.evade_power;
          m.steering = ai.saved_steering;
          m.rudder = ai.saved_rudder;
        } else {
          ai.timer = 30 * kSecond;
        }
      }
      break;
    case ShipAiMode::kHuntShort:
    case ShipAiMode::kHunt: {
      const bool short_hunt = ai.mode == ShipAiMode::kHuntShort;
      if (!ai.cls.has_sonar) {
        ai.mode = ShipAiMode::kCruise;
        break;
      }
      ai.contact.age = 1;
      ai.sonar.active = true;
      ai.sonar.echo = false;
      ai.mode = ShipAiMode::kSearch;
      ai.timer = (ctx.random.random_int(120) + 30) * kSecond;
      ai.saved_power = m.ordered_power;
      m.ordered_power = power_for(ship, ctx, 0x50000);
      const std::int32_t minutes = short_hunt ? ctx.random.random_int(3) + 2
                                              : ctx.random.random_int((crew + 1) * 10) + (crew + 1) * 20;
      ai.timer2 = minutes * 0x3c0000;
      break;
    }
    case ShipAiMode::kSearch:
      if (ai.contact.age == 0) {
        ai.mode = ShipAiMode::kInvestigate;
        restore_power();
      }
      ai.timer -= elapsed;
      if (ai.timer < 1) {
        ai.timer = (ctx.random.random_int(60) + 60) * kSecond;
        if (ctx.random.random_int(1) == 0) {
          m.ordered_heading = sub(m.heading, ctx.random.random_int(300) + 500);
        } else {
          m.ordered_heading = add(m.heading, ctx.random.random_int(300) + 500);
        }
        m.steering = true;
      }
      if (fooled()) {
        give_up_fooled();
        stand_down();
      }
      if (ai.contact.age <= ai.timer2) {
        break;
      }
      ai.mode = ShipAiMode::kCruise;
      ai.sonar.active = false;
      ai.contact.age = -1;
      restore_power();
      break;
    case ShipAiMode::kAttackStart: {
      ai.opening_time = 0;
      ai.gun_accuracy = 1000;
      ai.mode = ShipAiMode::kAttackRun;
      ai.sonar.active = true;
      ai.last_run_range = static_cast<std::int32_t>(ShipAiMode::kAttackStart);
      ai.saved_power = m.ordered_power;
      std::int32_t speed = 0xc0000;
      if (0xc0000 < ai.contact.speed.raw()) {
        speed = ai.contact.speed.raw() + 0x20000;
      }
      m.ordered_power = power_for(ship, ctx, speed);
      break;
    }
    case ShipAiMode::kAttackRun:
      if (ai.friendly_near_contact) {
        ai.mode = ShipAiMode::kGunCircleFriendly;
        ai.gun_circle_mode = ShipAiMode::kAttackRun;
        break;
      }
      if (collision_course) {
        ai.mode = ShipAiMode::kGunCircleCollision;
        ai.gun_circle_mode = ShipAiMode::kAttackRun;
        break;
      }
      if (submerged_contact) {
        guns();
      }
      if (ai.depth_charges != 0) {
        depth_charge_run(ship, ctx, elapsed);
        break;
      }
      ai.mode = ShipAiMode::kCruise;
      break;
    case ShipAiMode::kOpening:
      if (ai.depth_charges == 0) {
        ai.mode = ShipAiMode::kCruise;
        restore_power();
        // The original returns here, skipping the contact's ageing.
        return;
      }
      ai.timer -= elapsed;
      if (ai.timer > 0) {
        break;
      }
      ai.mode = ShipAiMode::kReturn;
      m.ordered_power = power_for(ship, ctx, 0x50000);
      break;
    case ShipAiMode::kReturn: {
      const std::int32_t range = steer_to_contact(ship);
      if (range < 0) {
        break;
      }
      if (ai.contact.age != 0) {
        if (ai.contact.age > 0xb3ffff) {
          ai.mode = ShipAiMode::kHunt;
          restore_power();
          break;
        }
        if (fooled()) {
          rolls.fooled_by_debris = false;
          ai.mode = ShipAiMode::kCruise;
          ai.sonar.active = false;
          ai.contact.age = -1;
          alert.timer = Fixed16();
          m.ordered_power = ai.saved_power;
          alert.previous = alert.level;
          alert.level = 0;
          break;
        }
        if (range < 300) {
          ai.mode = ShipAiMode::kAttackStart;
          restore_power();
          break;
        }
      }
      ai.mode = ShipAiMode::kInvestigate;
      restore_power();
      break;
    }
    case ShipAiMode::kGunApproach:
    case ShipAiMode::kGunAttack: {
      if (ai.mode == ShipAiMode::kGunApproach) {
        if (steer_to_contact(ship) < 0) {
          break;
        }
        ai.gun_accuracy = 1000;
        ai.saved_power = m.ordered_power;
        ai.mode = ShipAiMode::kGunAttack;
        m.ordered_power = max_power;
      }
      if (contact_bearing >= 0) {
        if (ai.cls.gun_count != 0) {
          if (steer_to_contact(ship) >= 0) {
            guns();
          }
          break;
        }
        ai.mode = ShipAiMode::kCruise;
        restore_power();
        break;
      }
      ai.mode = ShipAiMode::kInvestigate;
      restore_power();
      break;
    }
    case ShipAiMode::kInvestigate:
      ai.saved_power = m.ordered_power;
      m.ordered_power = power_for(ship, ctx, 0x110000);
      ai.sonar.active = true;
      ai.mode = ShipAiMode::kInvestigating;
      break;
    case ShipAiMode::kInvestigating: {
      const std::int32_t range = steer_to_contact(ship);
      if (range < 0 || range > 1500) {
        break;
      }
      if (ai.contact.age > 0x3c0000) {
        if (!fooled()) {
          ai.mode = ShipAiMode::kCircleSearch;
          const std::int32_t minutes = ctx.random.random_int(6) + (crew + 1) * 2;
          ai.timer = minutes * 0x3c0000;
          ai.timer2 = ai.timer / 2;
          ai.turn_left = ctx.random.random_int(2) != 0;
          m.steering = false;
          m.rudder = 0;
          m.ordered_power = power_for(ship, ctx, 0x50000);
          ai.sonar.active = true;
          break;
        }
        give_up_fooled();
        stand_down();
        break;
      }
      ai.mode = ShipAiMode::kAttackStart;
      restore_power();
      break;
    }
    case ShipAiMode::kCircleSearch:
      if (ai.contact.age != 0) {
        if (!fooled()) {
          ai.timer -= elapsed;
          if (ai.timer < 1) {
            ai.mode = ShipAiMode::kHunt;
            restore_power();
          }
          ai.timer2 -= elapsed;
          if (ai.timer2 < 1) {
            m.rudder = m.rudder_max / 6;
            if (ai.turn_left) {
              m.rudder = -m.rudder;
            }
          }
          break;
        }
        give_up_fooled();
        stand_down();
        break;
      }
      ai.mode = ShipAiMode::kAttackStart;
      restore_power();
      break;
    case ShipAiMode::kGunCircleFriendly:
    case ShipAiMode::kGunCircleCollision:
      ai.circle_for_collision = ai.mode == ShipAiMode::kGunCircleCollision;
      ai.mode = ShipAiMode::kGunCircle;
      ai.circle_phase = -1;
      ai.saved_steering = m.steering;
      m.steering = true;
      ai.saved_rudder = m.rudder;
      ai.saved_power = m.ordered_power;
      m.ordered_heading = ai.contact.heading;
      break;
    case ShipAiMode::kGunCircle: {
      if (ai.contact.age >= 0x12c0001) {
        ai.mode = ShipAiMode::kHunt;
      } else if (ai.circle_for_collision ? collision_course : ai.friendly_near_contact) {
        if (ai.contact.heading != m.ordered_heading) {
          m.ordered_heading = ai.contact.heading;
        }
        if (submerged_contact || contact_bearing >= 0) {
          guns();
        }
        // Each ship takes its own station on a 1000 yd circle.
        std::int32_t offset = self_index * 0xa0 + 0x546;
        while (offset > 0x8ca) {
          offset -= 900;
        }
        const fx::SinCos sc = fx::angle_sincos(add(ai.contact.heading, offset));
        ai_steer_to_point(ship, ai.contact.position.x + fx::mul(sc.sin, 48000),
                          ai.contact.position.z + fx::mul(sc.cos, 48000));
        const auto range = static_cast<std::int32_t>(
            fx::approx_distance(ai.contact.position.x - own.x, ai.contact.position.z - own.z));
        if (ai.circle_phase != 0 && range < 0xce40 && range > 0xa8c0) {
          ai.circle_phase = 0;
          m.ordered_power = power_for(ship, ctx, ai.contact.speed.raw());
        }
        if (ai.circle_phase != 1 && range < 0xa8c0) {
          ai.circle_phase = 1;
          m.ordered_power = 0;
        }
        if (ai.circle_phase == 2 || range < 0xce41) {
          break;
        }
        ai.circle_phase = 2;
        m.ordered_power = power_for(ship, ctx, ai.contact.speed.raw() + 0x50000);
        break;
      } else {
        ai.mode = ai.gun_circle_mode;
      }
      m.steering = ai.saved_steering;
      m.rudder = ai.saved_rudder;
      restore_power();
      break;
    }
    case ShipAiMode::kAgroundStart:
      ai.aground_power = m.ordered_power;
      ai.aground_mode = mode_at_start;
      ai.aground_steering = m.steering;
      ai.aground_rudder = m.rudder;
      ai.mode = ShipAiMode::kBackingOff;
      m.ordered_power = -max_power;
      if (m.power < 0) {
        m.ordered_power = max_power;
      }
      break;
    case ShipAiMode::kBackingOff:
    case ShipAiMode::kFindWater:
      if (ai.mode == ShipAiMode::kBackingOff) {
        m.steering = false;
        m.rudder = 0;
        if (ai.cls.draft_ft * -32 <= m.keel_clearance) {
          break;
        }
        m.ordered_power = ai.aground_power;
        ai.mode = ai.aground_mode;
        m.steering = ai.aground_steering;
        m.rudder = ai.aground_rudder;
        if (submerged_contact) {
          guns();
        }
      }
      ai.saved_steering = m.steering;
      ai.saved_rudder = m.ordered_heading.tenths();
      ai.water_power = m.ordered_power;
      m.ordered_power = max_power / 8;
      m.steering = true;
      m.ordered_heading = ai_find_water_heading(ctx, own.x, own.z, m.heading);
      ai.mode = ShipAiMode::kToWater;
      ai.resume_mode = mode_at_start;
      break;
    case ShipAiMode::kToWater:
      if (ai.previous_clearance < m.keel_clearance && ai.cls.draft_ft * 32 <= m.keel_clearance) {
        break;
      }
      ai.mode = ai.resume_mode;
      m.steering = ai.saved_steering;
      m.ordered_heading = Angle10(ai.saved_rudder);
      m.ordered_power = ai.water_power;
      break;
  }
  if (ai.contact.age >= 0) {
    ai.contact.age += elapsed;
  }
  ai.previous_clearance = m.keel_clearance;
}

void ai_ship_navigate_orders(Ship& ship, SimContext& ctx, std::int32_t dt) {
  // ai_ship_navigate_orders (0x12816)
  ShipAi& ai = ship.ai();
  MotionState& m = ship.motion();
  ShipSetup& s = ship.setup();
  const auto follow_route = [&] {
    if (s.route.count != 0 && s.route_index < s.route.count) {
      motion_steer_to_waypoint(m, ship.params().hull.turn_factor, s.route, s.route_index);
    }
  };
  switch (ship.orders()) {
    case ShipOrder::kNone:
    case ShipOrder::kConvoyMember:
    case ShipOrder::kIntercept:
      return;
    case ShipOrder::kEscort:
    case ShipOrder::kPatrol:
    case ShipOrder::kFollowPath:
      follow_route();
      return;
    case ShipOrder::kFishing:
      ai.timer -= dt;
      if (ai.timer > 0) {
        return;
      }
      ai.timer = (ctx.random.random_int(300) + 180) * kSecond;
      m.ordered_power = ctx.random.random_int(ship.params().max_power);
      return;
    case ShipOrder::kConvoyLeader:
      break;
  }
  if (s.zigzag == 0 || s.crew == 0) {
    follow_route();
    return;
  }
  ShipZigzag& zig = ai.zigzag;
  if (zig.leg < 0) {
    const std::int32_t before = s.route_index;
    motion_steer_to_waypoint(m, ship.params().hull.turn_factor, s.route, s.route_index);
    if (s.route.count == 0 || s.route.count <= s.route_index) {
      s.zigzag = 0;
    }
    if (zig.leg == -1 && before != s.route_index) {
      // A waypoint reached: zigzag again 5 minutes later.
      zig.timer = 0x12c0000;
      zig.leg = -2;
    }
  }
  zig.timer -= dt;
  if (zig.timer > 0 || zig.leg == -1) {
    return;
  }
  if (zig.leg < 0) {
    zig.leg = 0;
    zig.base_course = m.ordered_heading;
  }
  if (s.route.count > 0 && s.route_index < s.route.count) {
    const auto i = static_cast<std::size_t>(s.route_index);
    if (fx::approx_distance(s.route.x[i] - m.position.x, s.route.z[i] - m.position.z) / kUnitsPerYard < 5000) {
      // Steer straight for the waypoint when near it.
      zig.leg = -1;
    }
  }
  if (zig.leg < 0) {
    return;
  }
  if (zig.leg == 0) {
    zig.random = ctx.random.random_int(20);
  }
  // Legs of the plan (SH.EXE 0x10304c): minutes, degrees (both x crew).
  static const std::array<std::int32_t, 8> kLegs = exe_i32_array<8>(game_exe(), 0x10304c);
  const auto leg = static_cast<std::size_t>(zig.leg) * 2;
  const std::int32_t crew = s.crew;
  zig.timer = (zig.random / 5 + kLegs[leg]) / crew * 0x3c0000;
  m.ordered_heading = add(zig.base_course, (kLegs[leg + 1] * crew + zig.random) * 10);
  ++zig.leg;
  if (zig.leg > 3) {
    zig.leg = 0;
  }
  m.steering = true;
}

std::int32_t ai_steer_to_point(Ship& ship, std::int32_t x, std::int32_t z, std::int32_t offset_x,
                               std::int32_t offset_z) {
  return ai_steer_to_point(ship.motion(), ship.params().hull.turn_factor, x, z, offset_x, offset_z);
}

std::int32_t ai_steer_to_point(MotionState& m, Fixed16 turn_factor, std::int32_t x, std::int32_t z,
                               std::int32_t offset_x, std::int32_t offset_z) {
  // ai_steer_to_point (0x149cf)
  const std::int32_t dx = x - (m.position.x + offset_x);
  const std::int32_t dz = z - (m.position.z + offset_z);
  const Angle10 heading = fx::heading_from_delta(dx, dz);
  std::int32_t off = fx::angle_sub(heading, m.heading).tenths();
  if (off > 1800) {
    off = 3600 - off;
  }
  const auto range = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
  if (fx::trunc(turn_factor.raw()) * 850 / 25 * off / 1800 + 100 < range) {
    m.steering = true;
    m.ordered_heading = heading;
  }
  return range;
}

Angle10 ai_friendly_ahead_bearing(fx::Vec3 own, fx::Vec3 target, Angle10 heading, Fixed16 speed,
                                  std::int32_t seconds) {
  const fx::SinCos sc = fx::angle_sincos(heading);
  const std::int32_t v_units = fx::mul(speed.raw(), kUnitsPerSecondPerKnot);
  const std::int32_t ahead_x = fx::imul(fx::mul(v_units, sc.sin), seconds);
  const std::int32_t ahead_z = fx::imul(fx::mul(v_units, sc.cos), seconds);
  return fx::heading_from_delta(fx::wsub(fx::wadd(target.x, ahead_x), own.x),
                                fx::wsub(fx::wadd(ahead_z, target.z), own.z));
}

Angle10 ai_find_water_heading(SimContext& ctx, std::int32_t x, std::int32_t z, Angle10 heading) {
  // ai_find_water_heading (0x14c60): the cells (0, 1), (0, 0), (1, 0),
  // (1, 1) from the one at (x, z) (tables 0x10302c / 0x10303c).
  static constexpr std::array<std::int32_t, 4> kColumn = {0, 0, 1, 1};
  static constexpr std::array<std::int32_t, 4> kRow = {1, 0, 0, 1};
  Angle10 best = fx::angle_sub(heading, Angle10(1800));
  const Sensors* sensors = context_sensors(ctx);
  if (sensors == nullptr || sensors->environment().grid == nullptr) {
    return best;
  }
  const TerrainGrid& grid = *sensors->environment().grid;
  const std::int32_t cell = grid.cell_size();
  std::int32_t best_off = 3600;
  for (std::size_t k = 0; k < kColumn.size(); ++k) {
    const std::int32_t row = (grid.height() * cell - z) / cell + kRow[k];
    const std::int32_t column = x / cell + kColumn[k];
    if (grid.value(column, row) >= kCoastCell) {
      continue;
    }
    const Angle10 to_cell = fx::heading_from_delta(column * cell - x, (grid.height() - row) * cell - z);
    const std::int32_t off = fx::abs(heading.tenths() - to_cell.tenths());
    if (off < best_off) {
      best_off = off;
      best = to_cell;
    }
  }
  return best;
}

Ship* convoy_find_leader(const std::vector<std::unique_ptr<Vehicle>>& vehicles, std::int32_t group) {
  // convoy_find_leader (0x8e3fa)
  for (const auto& v : vehicles) {
    if (v->convoy_group_by_role(false) == group) {
      return dynamic_cast<Ship*>(v.get());
    }
  }
  return nullptr;
}

void convoy_regroup_on_leader(SimContext& ctx, std::int32_t group) {
  // convoy_regroup_on_leader (0x8e215)
  const auto& vehicles = vehicle_list(ctx);
  Ship* leader = convoy_find_leader(vehicles, group);
  if (leader == nullptr) {
    return;
  }
  ShipAi& lead = leader->ai();
  lead.leader_dx = 0;
  lead.leader_dz = 0;
  lead.zigzag.timer = 0;
  lead.zigzag.base_course = leader->motion().ordered_heading;
  lead.zigzag.leg = 0;
  for (const auto& v : vehicles) {
    if (v->convoy_group_by_role(true) != group) {
      continue;
    }
    if (Ship* member = dynamic_cast<Ship*>(v.get())) {
      member->ai().leader = leader;
      member->ai().leader_dx = 0;
      member->ai().leader_dz = 0;
    }
  }
  for (const auto& v : vehicles) {
    if (v->escorted_group() != group) {
      continue;
    }
    if (v->is_aircraft()) {
      if (auto* aircraft = dynamic_cast<Aircraft*>(v.get())) {
        aircraft->set_escort_leader(leader);
      }
      continue;
    }
    if (Ship* escort = dynamic_cast<Ship*>(v.get())) {
      escort->ai().leader = leader;
      escort->ai().leader_dx = 0;
      escort->ai().leader_dz = 0;
      MotionState& m = escort->motion();
      m.ordered_power = power_for(*escort, ctx, leader->motion().speed.raw() * 2);
      m.speed = motion_speed_for_power(m.ordered_power, escort->params().hull, ctx.env.sea_state, 100);
      m.power = m.ordered_power;
    }
  }
}

void ship_hand_over_convoy_lead(Ship& ship, SimContext& ctx) {
  // ship_hand_over_convoy_lead (0x825f3); the air escorts' release when no
  // ship is left belongs to the aircraft.
  const std::int32_t group = ship.setup().convoy_group;
  if (ship.orders() != ShipOrder::kConvoyLeader || group == 0) {
    return;
  }
  ship.set_orders(ShipOrder::kNone);
  const auto& vehicles = vehicle_list(ctx);
  Ship* heir = nullptr;
  for (const auto& v : vehicles) {
    if (v->convoy_group_by_role(true) == group) {
      heir = dynamic_cast<Ship*>(v.get());
      break;
    }
  }
  if (heir == nullptr) {
    for (const auto& v : vehicles) {
      if (v->escorted_group() == group) {
        if (!v->is_aircraft()) {
          heir = dynamic_cast<Ship*>(v.get());
        }
        break;
      }
    }
  }
  if (heir == nullptr) {
    return;
  }
  make_leader(*heir, ship);
  convoy_regroup_on_leader(ctx, group);
}

// ---------------------------------------------------------------------------
// The ship's sensor and AI slots.

Angle10 Ship::heading() const { return motion_.heading; }
Fixed16 Ship::speed() const { return motion_.speed; }
std::int32_t Ship::comms_range_yd() const { return ai_.sighting_range_yd; }
void Ship::hydrophone_check(SimContext& ctx) { ship_hydrophone_check(*this, ctx); }

std::int32_t Ship::radar_cross_section(const fx::Vec3& /*observer*/, Angle10 /*observer_heading*/) const {
  // ship_radar_cross_section (0x82513)
  return ai_.cls.length_ft * 100;
}

std::int32_t Ship::noise_level() const { return ship_noise_level(*this); }

void Ship::mark_sighted(const GameClock& clock) {
  // ship_mark_sighted (0x82521); the TDC estimate of the chart's contact
  // record (+0x118..) belongs to the fire control.
  ShipSightingRecord& s = ai_.sighting;
  s.sighted = true;
  s.age = 0;
  s.last_x = motion_.position.x;
  s.last_z = motion_.position.z;
  s.hour = clock.hour();
  s.minute = clock.minute();
  s.month = clock.month();
  s.day = clock.day();
}

std::int32_t Ship::add_track_time(std::int32_t dt, bool selected) {
  // veh_add_target_track_time (0x7ff11)
  if (dt >= 0 && selected) {
    ai_.sighting.track_time += dt;
  }
  return ai_.sighting.track_time;
}

void Ship::refresh_sighted() {
  // veh_update_sighted (0x7feea)
  ai_.sighting.sighted = ai_.sighting.age < kSecond && ai_.sighting.last_x >= 0;
}

std::int32_t Ship::visual_height() const { return motion_.position.y + ai_.cls.visual_size * kUnitsPerFoot; }

std::int32_t Ship::exposure() const {
  // ship_exposure (0x825c8)
  return ai_.cls.lookout_ft * kUnitsPerFoot + motion_.position.y < 0 ? 2 : 1;
}

bool Ship::is_escort() const { return ai_.cls.role == ShipRole::kEscort; }
bool Ship::is_small_craft() const { return ai_.cls.role == ShipRole::kSmallCraft; }

std::int32_t Ship::convoy_group_by_role(bool members) const {
  // ship_convoy_group_by_role (0x19c40)
  return orders_ == (members ? ShipOrder::kConvoyMember : ShipOrder::kConvoyLeader) ? setup_.convoy_group : 0;
}

std::int32_t Ship::escorted_group() const { return orders_ == ShipOrder::kEscort ? setup_.convoy_group : 0; }

void Ship::receive_contact(SimContext& /*ctx*/, const fx::Vec3& position, Vehicle* target) {
  // ai_ship_receive_contact (0x12ac9): a fresh report within its sighting
  // range alerts a ship that has no fresh contact of its own.
  if (ai_.contact.age == 0) {
    return;
  }
  const auto range = static_cast<std::int32_t>(
      fx::approx_distance(position.x - motion_.position.x, position.z - motion_.position.z) / kUnitsPerYard);
  if (range > ai_.sighting_range_yd) {
    return;
  }
  alert_.level = 1;
  ai_.contact.heading = Angle10(0);
  ai_.contact.speed = Fixed16();
  ai_.contact.age = 0;
  ai_.contact.position = position;
  alert_.timer = Fixed16::from_int((setup_.crew + 1) * 600);
  ai_.contact.vehicle = target;
}

void Ship::receive_radar_intercept(const fx::Vec3& position, Vehicle* target) {
  if (ai_.contact.age >= 0 && ai_.contact.age < 0x1e0000) {
    return;
  }
  alert_.level = 1;
  ai_.contact.heading = Angle10(0);
  ai_.contact.speed = Fixed16();
  ai_.contact.age = 0;
  ai_.contact.position = position;
  alert_.timer = Fixed16::from_int((setup_.crew + 1) * 600);
  ai_.contact.vehicle = target;
}

}  // namespace sh
