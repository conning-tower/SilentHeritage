#include "sim/aircraft.h"

#include <algorithm>
#include <climits>
#include <memory>

#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

constexpr std::int32_t kSecond = 0x10000;
constexpr std::int32_t kMinute = 0x3c0000;
constexpr std::int32_t kMile = 0x14a00;  // 1760 yd in world units.
constexpr std::int32_t kSfxCrash = 0x2a;
constexpr std::int32_t kSfxFlyby = 0xe;
// Bomb release range by crew, yd (0x103084).
constexpr std::array<std::int32_t, 3> kReleaseRange = {350, 300, 200};
// The guns' reload (16.16 s) and burst length by crew (0x10306c).
constexpr std::array<std::array<std::int32_t, 2>, 3> kGunBurst = {{{0x18000, 5}, {0x10000, 10}, {0x8000, 15}}};

Angle10 add(Angle10 a, std::int32_t b) { return fx::angle_add(a, Angle10(b)); }
Angle10 sub(Angle10 a, std::int32_t b) { return fx::angle_sub(a, Angle10(b)); }

std::int32_t fold(std::int32_t a) { return a > 1800 ? a - 3600 : a; }

// The bank (or turn) target toward `wanted` from `heading`: the error
// clamped to 90 deg, x 750 / 900.
std::int32_t bank_toward(Angle10 heading, std::int32_t wanted) {
  std::int32_t err = fold(fx::angle_sub(heading, Angle10(wanted)).tenths());
  if (fx::abs(err) < 2) {
    err = 0;
  }
  if (fx::abs(err) > 900) {
    err = err / fx::abs(err) * 900;
  }
  return err * 750 / 900;
}

}  // namespace

std::int32_t strafe_bearing(std::int32_t contact_bearing, const MissionRules& rules) {
  // The original folds the bearing by 900 instead of 3600.
  const std::int32_t fold_by = rules.fix_obvious_errors ? 3600 : 900;
  return contact_bearing > 1800 ? contact_bearing - fold_by : contact_bearing;
}

AircraftParams AircraftParams::from(const AircraftType& type) {
  AircraftParams p;
  p.max_speed_kn = static_cast<std::int32_t>(type.max_speed_mph);
  p.length_ft = static_cast<std::int32_t>(type.length_ft);
  p.span_ft = static_cast<std::int32_t>(type.span_ft);
  p.height_ft = static_cast<std::int32_t>(type.height_ft);
  p.guns = static_cast<std::int32_t>(type.guns);
  p.roll_rate = type.roll_rate;
  return p;
}

void Aircraft::reset_flight() {
  AircraftFlight& f = flight_;
  f.pos_x = static_cast<std::int64_t>(f.position.x) << 16;
  f.pos_y = static_cast<std::int64_t>(f.position.y) << 16;
  f.pos_z = static_cast<std::int64_t>(f.position.z) << 16;
  f.heading_acc = static_cast<std::int64_t>(f.heading.tenths()) << 16;
  f.pitch_acc = static_cast<std::int64_t>(f.pitch.tenths()) << 16;
  f.roll_acc = static_cast<std::int64_t>(f.roll.tenths()) << 16;
  f.orientation = fx::mat3_from_euler(Angle10(-f.heading.tenths()), Angle10(-f.pitch.tenths()), f.roll);
  f.inverse = fx::transpose(f.orientation);
}

void Aircraft::init(SimContext& ctx) {
  // aircraft_init (0x839a4)
  if (setup_.type != nullptr) {
    params_ = AircraftParams::from(*setup_.type);
  }
  flyby_timer_ = 0;
  relaunch_timer_ = 0;
  ai_.think_time = 0;
  ai_.drops = 0;
  ai_.shadow_time = (ctx.random.random_int(3) + 3) * kMinute;
  ai_.contact_age = -1;
  sighted_ = false;
  crashing_ = false;
  smoke_timer_ = 0;
  ai_.debris = nullptr;
  attacker_ = nullptr;
  ai_.target = nullptr;
  leader_ = nullptr;
  ai_.hit_points = static_cast<std::int32_t>(((ctx.env.options & 0xf) >> 2) + 1) * 3;
  const PlayerSub* player = context_player(ctx);
  flight_.heading = add(player != nullptr ? player->motion().heading : Angle10(0), 1800);
  flight_.pitch = Angle10(0);
  flight_.roll = Angle10(0);
  flight_.wanted_heading = -1;
  flight_.bank_target = 0;
  airborne_ = false;
  set_sinking();
  flight_.ordered_altitude_ft = flight_.position.y / kUnitsPerFoot;
  flight_.throttle = 100;
  reset_flight();
  update_bounds();
}

void Aircraft::take_off(SimContext& ctx, const fx::Vec3& position, std::int32_t range_yd) {
  // The take-off half of air_sorties_relaunch (0x15f58).
  flight_.position = position;
  ai_.state = AircraftState::kMission;
  ai_.sighting_range_yd = range_yd;
  ai_.bombs = setup_.bomb_load;
  init(ctx);
  airborne_ = true;
  clear_sinking();
  --setup_.sorties;
}

void Aircraft::set_escort_leader(const Ship* leader) {
  leader_ = leader;
  leader_dx_ = 0;
  leader_dz_ = 0;
}

void Aircraft::update(SimContext& ctx, Fixed16 dt_s) {
  // aircraft_update (0x83f07)
  if (!airborne_) {
    return;
  }
  const std::int32_t dt = dt_s.raw();
  PlayerSub* player = context_player(ctx);
  Sensors* sensors = context_sensors(ctx);
  const SensorEnvironment no_sensors;
  const SensorEnvironment& env = sensors != nullptr ? sensors->environment() : no_sensors;
  AircraftFlight& f = flight_;
  const fx::Vec3 own = player != nullptr ? player->position() : fx::Vec3{};
  if (player != nullptr) {
    // The eye is the player's depth in feet x 16, as the original passes it.
    const fx::Vec3 eye{own.x, player->depth_ft() * kUnitsPerFoot, own.z};
    ai_.sighting_range_yd = sighting_check(env.sighting(ctx.env), env.grid, eye, f.position).range.range_yd;
  }
  if (ai_.hit_points < 1 && !crashing_) {
    crashing_ = true;
    const auto yards =
        static_cast<std::int32_t>(fx::approx_distance(own.x - f.position.x, own.z - f.position.z) / kUnitsPerYard);
    ctx.sfx(kSfxCrash, distance_volume(yards, 9000));
  }
  flyby_timer_ = std::max(flyby_timer_ - dt, 0);
  const auto before = static_cast<std::int32_t>(fx::approx_distance(own.x - f.position.x, own.z - f.position.z));
  if (sensors != nullptr && player != nullptr) {
    think(ctx, dt);
  }
  const std::int32_t terrain = ctx.floor.height_at(f.position.x, f.position.z);
  if (terrain > 0 && f.position.y - terrain < 800) {
    f.ordered_altitude_ft = terrain / kUnitsPerFoot + 50;
  }
  fly(dt, std::max(terrain, 0));
  if (crashing_ && f.position.y > 0xa0) {
    smoke_timer_ -= dt;
    if (smoke_timer_ < 1) {
      const fx::Vec3 tail = fx::transform(f.orientation, {0, 0, -0x90});
      smoke_timer_ = ctx.random.random_int(0x7ae) + 0x199a;
      ctx.sim->add(std::make_unique<Smoke>(fx::Vec3{f.position.x + tail.x, f.position.y, f.position.z + tail.z}));
    }
  }
  const auto after = static_cast<std::int32_t>(fx::approx_distance(own.x - f.position.x, own.z - f.position.z));
  if (flyby_timer_ == 0 && f.position.y < 32000 && after < 8000 && after < before) {
    // The original plays it at 100 when the station shows the outside
    // (station slot +0x18), else 20; the sensors only know the chart and
    // radar stations.
    ctx.sfx(kSfxFlyby, env.station == SensorStation::kOther ? 100 : 20);
    flyby_timer_ = 6 * kSecond;
  }
  const std::int32_t ground = std::max(terrain, 0);
  if (f.position.y <= ground) {
    airborne_ = false;
    set_sinking();
    relaunch_timer_ = (ctx.random.random_int(10) + 10) * (3 - setup_.crew) * kMinute;
    if (terrain < 0) {
      spawn_splash(ctx, f.position.x, f.position.z, true);
    } else {
      spawn_explosion(ctx, f.position, ExplosionKind::kBomb);
    }
  }
}

void Aircraft::fly(std::int32_t dt, std::int32_t ground) {
  // aircraft_update_flight (0x842de)
  if (sinking()) {
    return;
  }
  AircraftFlight& f = flight_;
  if (setup_.mission == AircraftMissionCode::kEscort && leader_ != nullptr) {
    leader_dx_ += leader_->ai().leader_dx;
    leader_dz_ += leader_->ai().leader_dz;
    const std::int64_t whole_x = leader_dx_ >> 16;
    const std::int64_t whole_z = leader_dz_ >> 16;
    leader_dx_ -= whole_x << 16;
    leader_dz_ -= whole_z << 16;
    setup_.orbit_x += static_cast<std::int32_t>(whole_x);
    setup_.orbit_z += static_cast<std::int32_t>(whole_z);
  }
  if (crashing_) {
    f.throttle = 30;
    f.wanted_heading = f.heading.tenths();
  }
  if (f.wanted_heading >= 0) {
    f.bank_target = bank_toward(f.heading, f.wanted_heading);
  }
  const std::int32_t climb = f.ordered_altitude_ft + (ground - f.position.y) / kUnitsPerFoot;
  const std::int32_t gain = climb < 0 ? 375 : 250;
  std::int32_t pitch_target = -gain * climb / 1000;
  if (crashing_) {
    pitch_target = 450;
  }
  const std::int32_t roll_step = fx::imul(fx::mul(fx::imul(f.bank_target - f.roll.tenths(), 0x10000), dt),
                                          params_.roll_rate) / 10;
  f.roll_acc += roll_step;
  f.roll = Angle10(static_cast<std::int32_t>(f.roll_acc >> 16));
  f.heading_acc -= f.roll_acc >> 4;
  while (f.heading_acc < 0) {
    f.heading_acc += static_cast<std::int64_t>(3600) << 16;
  }
  f.heading = Angle10(static_cast<std::int32_t>(f.heading_acc >> 16));
  f.pitch_acc += fx::mul(fx::imul(pitch_target - f.pitch.tenths(), 0x10000), dt);
  f.pitch = Angle10(static_cast<std::int32_t>(f.pitch_acc >> 16));
  f.orientation = fx::mat3_from_euler(Angle10(-f.heading.tenths()), Angle10(-f.pitch.tenths()), f.roll);
  f.inverse = fx::transpose(f.orientation);
  const std::int32_t knots =
      (1800 - f.pitch.tenths() / 2) * (params_.max_speed_kn * f.throttle / 100) / 1800;
  f.speed = Fixed16::from_raw(fx::imul(knots, 0x10000));
  f.speed_tenths = fx::trunc(fx::imul(knots, 0xa0000));
  const std::int32_t units = fx::div(fx::mul(f.speed.raw(), kUnitsPerNauticalMile), 3600);
  const fx::Vec3 v = fx::transform(f.orientation, {0, 0, units});
  f.pos_x += fx::mul(v.x, dt);
  f.pos_y += fx::mul(v.y, dt);
  f.pos_z += fx::mul(v.z, dt);
  f.position = {static_cast<std::int32_t>(f.pos_x >> 16), static_cast<std::int32_t>(f.pos_y >> 16),
                static_cast<std::int32_t>(f.pos_z >> 16)};
}

std::int32_t Aircraft::range_to(std::int32_t x, std::int32_t z, std::int32_t ox, std::int32_t oz) {
  // ai_aircraft_range_to_point (0x15a23): also turns toward the point.
  const std::int32_t dx = x - (ox + flight_.position.x);
  const std::int32_t dz = z - (flight_.position.z + oz);
  flight_.wanted_heading = fx::heading_from_delta(dx, dz).tenths();
  return static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
}

bool Aircraft::drop_bomb(SimContext& ctx, std::int32_t dt) {
  // ai_aircraft_drop_bomb (0x15a75)
  const std::int32_t range = range_to(ai_.contact.x, ai_.contact.z);
  flight_.ordered_altitude_ft = ctx.random.random_int(100) + 200;
  const std::int32_t release = kReleaseRange[static_cast<std::size_t>(std::clamp(setup_.crew, 0, 2))];
  if (ai_.last_range < range) {
    ++ai_.counter;
    ai_.opening_time += dt;
  } else {
    ai_.counter = 0;
    ai_.opening_time = 0;
  }
  ai_.last_range = range;
  // The original tests the shadow bearing (+0x128) where the opening count
  // is meant.
  if (release / (ai_.drops + 1) + ai_.release_offset <= range &&
      (range > 999 || ai_.opening_time < 0x10001 || ai_.shadow_bearing < 3)) {
    if (range < 3000) {
      ai_.in_release_window = true;
      if (ai_.wingman_bombing) {
        ai_.state = AircraftState::kHoldStart;
      }
      flight_.ordered_altitude_ft = 110;
    }
    return false;
  }
  ai_.in_release_window = false;
  if (!ai_.friendly_near_contact) {
    std::int32_t fuse = -ai_.contact.y / kUnitsPerFoot;
    if (ai_.contact_age > kMinute) {
      const std::int32_t spread = (3 - setup_.crew) * 30;
      fuse += spread / 2 - ctx.random.random_int(spread);
    }
    if (fuse > 0) {
      fuse = 1;
    }
    ctx.sim->add(std::make_unique<Bomb>(flight_.position, flight_.heading, flight_.speed_tenths / 10, fuse, this));
    ai_.bombs = std::max(ai_.bombs - 1, 0);
    ++ai_.drops;
  }
  return true;
}

void Aircraft::think(SimContext& ctx, std::int32_t dt) {
  // ai_aircraft_think (0x14d8b): every frame (no one-second gathering).
  Sensors& sensors = *context_sensors(ctx);
  const SensorEnvironment& env = sensors.environment();
  AircraftFlight& f = flight_;
  ai_.think_time += dt;
  if (ai_.think_time < 0) {
    return;
  }
  if (ctx.clock.effective_compression() == 1 && list_index() % 8 != sensors.frame_counter()) {
    return;
  }
  const std::int32_t elapsed = ai_.think_time;
  ai_.think_time = 0;
  if (sinking()) {
    return;
  }
  std::int32_t best = INT_MAX;
  std::int32_t contact_bearing = -1;
  ai_.wingman_bombing = false;
  ai_.friendly_near_contact = false;
  PlayerSub* player = context_player(ctx);
  const PlayerLookoutState player_state = player_lookout_state(*player, env.crew);
  const SightingConditions conditions = env.sighting(ctx.env);
  const auto& vehicles = ctx.sim->vehicles();
  for (std::size_t index = 0; index < vehicles.size(); ++index) {
    Vehicle& v = *vehicles[index];
    if (&v == this || !v.is_contact() || v.is_aircraft() || v.plane_count() != 0 || v.is_pilot()) {
      continue;
    }
    const fx::Vec3 tpos = v.position();
    const Angle10 theading = v.heading();
    fx::Vec3 eye = f.position;
    AiSightingTarget target{{tpos.x, v.visual_height(), tpos.z}, theading, nullptr, false};
    if (&v == player) {
      // The original raises its eye by the player's awash depth.
      eye.y += player->sub().awash_depth_ft * kUnitsPerFoot;
      target.player = &player_state;
    }
    // The crew term is called with crew 0 whatever the crew.
    const AiSighting seen = ai_aircraft_detect_target(conditions, ctx.env.sea_state, env.grid, eye, target, 0);
    if (!seen.seen()) {
      continue;
    }
    const std::int32_t d = seen.distance_yd;
    const Fixed16 tspeed = v.speed();
    const Angle10 bearing = fx::heading_from_delta(tpos.x - f.position.x, tpos.z - f.position.z);
    if (!v.is_debris()) {
      if (v.side() == side() || v.is_torpedo() || best <= d) {
        // A friendly ship (the escorted group) is its orbit centre. The
        // original's wingman test that follows is unreachable: aircraft are
        // skipped above.
        if (v.side() == side() && d < INT_MAX) {
          setup_.orbit_x = tpos.x;
          setup_.orbit_z = tpos.z;
          if (ai_.contact_age < kMinute) {
            const auto near = static_cast<std::int32_t>(
                fx::approx_distance(ai_.contact.x - tpos.x, ai_.contact.z - tpos.z) / kUnitsPerYard);
            if (near < 300) {
              ai_.friendly_near_contact = true;
            }
          }
        }
        continue;
      }
      ai_.contact = tpos;
      ai_.contact_heading = theading;
      ai_.contact_speed = tspeed;
      contact_bearing = bearing.tenths();
    } else {
      if (&v == ai_.debris) {
        continue;
      }
      ai_.debris = &v;
      if (contact_bearing >= 0) {
        continue;
      }
      ai_.contact.x = tpos.x;
      ai_.contact.y = ctx.random.random_int(300) << 4;
      ai_.contact.z = tpos.z;
      ai_.contact_heading = theading;
      ai_.contact_speed = tspeed;
    }
    ai_.target = &v;
    ai_.contact_age = 0;
    best = d;
    broadcast_contact(ctx, ai_.contact, &v);
  }

  using State = AircraftState;
  const auto in = [&](State lo, State hi) { return ai_.state >= lo && ai_.state <= hi; };
  if (!in(State::kHomeStart, State::kHold)) {
    if (contact_bearing < 0) {
      if (ai_.contact_age == -1 || (setup_.crew + 1) * 0x1e00000 <= ai_.contact_age) {
        if (!in(State::kOrbitStart, State::kOrbitTurn)) {
          ai_.state = State::kOrbitStart;
        }
      } else if (!in(State::kBombApproach, State::kBombAway) && ai_.bombs != 0) {
        ai_.state = State::kBombApproach;
      }
    } else {
      setup_.orbit_radius = kMile;
      setup_.orbit_x = ai_.contact.x;
      setup_.orbit_z = ai_.contact.z;
      if (ai_.bombs != 0) {
        if (!in(State::kBombApproach, State::kBombAway)) {
          ai_.state = State::kBombApproach;
        }
      } else if (!setup_.strafes) {
        if (!in(State::kShadowStart, State::kShadowing)) {
          ai_.state = State::kShadowStart;
        }
      } else if (!in(State::kStrafeApproach, State::kStrafeAway)) {
        ai_.state = State::kStrafeApproach;
      }
    }
  }
  if ((env.light_pct < 50 || env.fog_pct > 20) && ai_.state != State::kHome) {
    ai_.state = State::kHomeStart;
  }

  const auto open_out = [&] {
    range_to(ai_.contact.x, ai_.contact.z);
    f.wanted_heading = add(Angle10(f.wanted_heading), 1800).tenths();
  };
  const auto distance_to = [&](std::int32_t x, std::int32_t z) {
    return static_cast<std::int32_t>(fx::approx_distance(x - f.position.x, z - f.position.z));
  };
  switch (ai_.state) {
    case State::kMission:
      if (setup_.mission == AircraftMissionCode::kEscort || setup_.mission == AircraftMissionCode::kPatrol) {
        ai_.state = State::kOrbitStart;
      } else if (setup_.mission == AircraftMissionCode::kFollowPath && setup_.route.count != 0 &&
                 setup_.route_index < setup_.route.count) {
        // aircraft_steer_to_waypoint (0x8df42)
        const auto i = static_cast<std::size_t>(setup_.route_index);
        const std::int32_t dx = setup_.route.x[i] - f.position.x;
        const std::int32_t dz = setup_.route.z[i] - f.position.z;
        f.wanted_heading = fx::heading_from_delta(dx, dz).tenths();
        if (fx::approx_distance(dx, dz) / kUnitsPerYard < 1000) {
          ++setup_.route_index;
          if (setup_.route_index == setup_.route.count && setup_.route_index < Route::kCapacity &&
              setup_.route.x[static_cast<std::size_t>(setup_.route_index)] == -1) {
            setup_.route_index = 0;
          }
        }
      }
      break;
    case State::kBombApproach:
      if (distance_to(ai_.contact.x, ai_.contact.z) > kMile - 1) {
        const std::int32_t crew = setup_.crew;
        ai_.state = State::kBombRun;
        f.throttle = 50;
        const std::int32_t r = ctx.random.random_int((3 - crew) * 100);
        ai_.opening_time = 0;
        ai_.counter = 0;
        ai_.last_range = 0;
        ai_.release_offset = (3 - crew) * 50 - r;
      } else {
        open_out();
      }
      break;
    case State::kBombRun: {
      if (!drop_bomb(ctx, elapsed)) {
        break;
      }
      ai_.state = State::kBombAway;
      f.ordered_altitude_ft += ctx.random.random_int(50);
      const std::int32_t turn = ctx.random.random_int(600) + 300;
      // rand(1) is always 0: always to the left.
      const bool right = ctx.random.random_int(1) != 0;
      f.wanted_heading = (right ? add(Angle10(f.wanted_heading), turn) : sub(Angle10(f.wanted_heading), turn)).tenths();
      ai_.timer = 30 * kSecond;
      break;
    }
    case State::kBombAway:
    case State::kStrafeAway:
      ai_.timer -= elapsed;
      if (ai_.timer > 0) {
        break;
      }
      ai_.state = State::kMission;
      f.throttle = 80;
      f.ordered_altitude_ft += ctx.random.random_int(500);
      break;
    case State::kStrafeApproach:
      if (distance_to(ai_.contact.x, ai_.contact.z) > kMile - 1) {
        ai_.state = State::kStrafeRun;
        f.throttle = 50;
        ai_.opening_time = 0;
        ai_.counter = 0;
        ai_.release_offset = 1200;
        f.ordered_altitude_ft = 250;
      } else {
        open_out();
      }
      break;
    case State::kStrafeRun: {
      if (contact_bearing >= 0 && !ai_.friendly_near_contact) {
        const std::int32_t range = range_to(ai_.contact.x, ai_.contact.z);
        // Normalised into 0..3599 first (angle_sub(angle_add(heading, 0), 0)).
        const std::int32_t heading = fold(fx::angle_sub(fx::angle_add(f.heading, Angle10(0)), Angle10(0)).tenths());
        const std::int32_t bearing = strafe_bearing(contact_bearing, ctx.env.rules);
        ai_.opening_time = std::max(ai_.opening_time - elapsed, 0);
        if (range < ai_.release_offset && fx::abs(heading - bearing) < 151) {
          f.ordered_altitude_ft = 80;
          if (ai_.opening_time == 0) {
            ctx.sim->add(std::make_unique<Shell>(f.position, f.heading, Angle10(-50 - f.pitch.tenths()), 4000, 0,
                                                 this));
            ++ai_.counter;
            const auto& burst = kGunBurst[static_cast<std::size_t>(std::clamp(setup_.crew, 0, 2))];
            ai_.opening_time = burst[0];
            if (burst[1] <= ai_.counter) {
              ai_.counter = ctx.random.random_int(3);
              ai_.opening_time *= 3;
            }
          }
        }
        if (range > 199) {
          break;
        }
      }
      ai_.state = State::kStrafeAway;
      f.ordered_altitude_ft += ctx.random.random_int(50);
      ai_.timer = 30 * kSecond;
      break;
    }
    case State::kShadowStart:
      ai_.state = State::kShadowLeg;
      ai_.param = ctx.random.random_int(500) + 1500;
      ai_.shadow_side = ctx.random.random_int(2);
      ai_.shadow_bearing = sub(Angle10(contact_bearing), 1800).tenths();
      ai_.shadow_time = (ctx.random.random_int(5) + 4) * kMinute;
      f.throttle = 60;
      f.ordered_altitude_ft = ctx.random.random_int(100) + 200;
      break;
    case State::kShadowLeg: {
      ai_.shadow_bearing = (ai_.shadow_side == 0 ? sub(Angle10(ai_.shadow_bearing), 200)
                                                 : add(Angle10(ai_.shadow_bearing), 200))
                               .tenths();
      const fx::Point2 p = fx::point_rotate_scale({0, ai_.param}, {0, 0}, Angle10(ai_.shadow_bearing));
      ai_.shadow_x = p.x * kUnitsPerYard + ai_.contact.x;
      ai_.shadow_z = p.y * kUnitsPerYard + ai_.contact.z;
      ai_.state = State::kShadowing;
      break;
    }
    case State::kShadowing:
      ai_.shadow_time -= elapsed;
      if (ai_.shadow_time < 1) {
        ai_.shadow_time = 0;
        ai_.state = State::kHomeStart;
      } else if (range_to(ai_.shadow_x, ai_.shadow_z) < 300) {
        ai_.state = State::kShadowLeg;
      }
      break;
    case State::kOrbitStart: {
      ai_.param = ctx.random.random_int(2);
      f.ordered_altitude_ft = 500 / (ctx.env.weather + 1);
      const std::int32_t r = ctx.random.random_int(f.ordered_altitude_ft / 4);
      ai_.state = State::kOrbitReturn;
      f.throttle = 70;
      f.ordered_altitude_ft += r;
      break;
    }
    case State::kOrbitReturn: {
      const std::int32_t oz = ctx.random.random_int(16000);
      const std::int32_t ox = ctx.random.random_int(16000);
      if (range_to(setup_.orbit_x, setup_.orbit_z, ox, oz) < 1000) {
        ai_.state = State::kOrbiting;
      }
      break;
    }
    case State::kOrbiting:
      if (setup_.orbit_radius < distance_to(setup_.orbit_x, setup_.orbit_z)) {
        ai_.state = State::kOrbitTurn;
        f.wanted_heading = (ai_.param == 0 ? sub(Angle10(f.wanted_heading), 900)
                                           : add(Angle10(f.wanted_heading), 900))
                               .tenths();
        ai_.timer = (ctx.random.random_int(60) + 20) * kSecond;
      }
      break;
    case State::kOrbitTurn:
      ai_.timer -= elapsed;
      if (ai_.timer < 1) {
        ai_.state = State::kOrbitReturn;
      }
      break;
    case State::kHomeStart:
      ai_.param = ctx.random.random_int(3600);
      ai_.state = State::kHome;
      f.ordered_altitude_ft = 1000;
      f.throttle = 70;
      break;
    case State::kHome: {
      // ai_aircraft_check_out_of_range (0x15c11)
      const fx::Vec3 own = player->position();
      const auto yards =
          static_cast<std::int32_t>(fx::approx_distance(own.x - f.position.x, own.z - f.position.z) / kUnitsPerYard);
      if (ai_.sighting_range_yd * 3 < yards) {
        airborne_ = false;
        set_sinking();
        relaunch_timer_ = (ctx.random.random_int(10) + 10) * (3 - setup_.crew) * kMinute;
      }
      break;
    }
    case State::kHoldStart:
      ai_.state = State::kHold;
      ai_.param = f.wanted_heading;
      ai_.shadow_side = f.bank_target;
      break;
    case State::kHold:
      f.wanted_heading = -1;
      f.bank_target = 100;
      if (!ai_.wingman_bombing) {
        ai_.state = State::kBombRun;
        ai_.opening_time = 0;
        ai_.counter = 0;
        ai_.last_range = 0;
        f.wanted_heading = ai_.param;
        f.bank_target = ai_.shadow_side;
      }
      break;
  }
  if (ai_.contact_age != -1) {
    ai_.contact_age += elapsed;
  }
}

void Aircraft::update_bounds() {
  // aircraft_update_bbox (0x83ca2)
  if (!airborne_) {
    return;
  }
  const std::int32_t half_z = params_.length_ft * kUnitsPerFoot / 2;
  const std::int32_t half_x = params_.span_ft * kUnitsPerFoot / 2;
  const std::int32_t height = params_.height_ft * kUnitsPerFoot;
  const std::int32_t half_y = height / 2;
  const std::int32_t radius = std::max({height, half_x, half_z});
  const fx::Vec3& p = flight_.position;
  collision().box =
      make_hull_box(p, flight_.orientation, flight_.inverse, half_x, half_z, p.y - half_y, p.y + half_y, radius);
}

bool Aircraft::take_damage(SimContext& /*ctx*/, Vehicle* attacker, DamageKind kind, std::int32_t /*distance*/,
                           std::int32_t amount) {
  // aircraft_take_damage (0x84837)
  if (kind == DamageKind::kGunfire) {
    amount = 1;
  }
  ai_.hit_points = std::max(ai_.hit_points - amount, 0);
  attacker_ = attacker;
  return true;
}

void Aircraft::forget(const Vehicle& removed) {
  // aircraft_clear_dead_refs (0x84bdd)
  if (attacker_ == &removed) {
    attacker_ = nullptr;
  }
  if (ai_.target == &removed) {
    ai_.target = nullptr;
  }
  if (ai_.debris == &removed) {
    ai_.debris = nullptr;
  }
  if (leader_ == &removed) {
    leader_ = nullptr;
  }
}

void Aircraft::receive_contact(SimContext& /*ctx*/, const fx::Vec3& position, Vehicle* target) {
  if (ai_.contact_age == 0) {
    return;
  }
  ai_.contact_heading = Angle10(0);
  ai_.contact_speed = Fixed16();
  ai_.contact_age = 0;
  ai_.contact = position;
  ai_.target = target;
}

std::int32_t Aircraft::radar_cross_section(const fx::Vec3& observer, Angle10 observer_heading) const {
  if (!airborne_) {
    return 0;
  }
  const Angle10 to_player =
      fx::heading_from_delta(-(flight_.position.x - observer.x), -(flight_.position.z - observer.z));
  std::int32_t a = fx::angle_sub(to_player, observer_heading).tenths();
  if (a > 1800) {
    a = fx::angle_sub(Angle10(3600), Angle10(a)).tenths();
  }
  a = fx::abs(900 - a);
  return params_.span_ft * ((900 - a) / 90 + 10) * 10;
}

void Aircraft::mark_sighted(const GameClock& /*clock*/) {
  // aircraft_mark_sighted (0x84bb4)
  if (airborne_) {
    sighted_ = true;
  }
}

void Smoke::init(SimContext& /*ctx*/) { lifetime_ = 0x8000; }

void Smoke::update(SimContext& /*ctx*/, Fixed16 dt) {
  lifetime_ -= dt.raw();
  if (lifetime_ < 1) {
    remove();
  }
}

}  // namespace sh
