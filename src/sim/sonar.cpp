#include "sim/sonar.h"

#include <cstddef>

#include "sim/ai_ship.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

// 550.0 in 16.16: thrust over it is the engines' "hp".
constexpr std::int32_t kThrustPerHp = 0x2260000;

// Returns the engine output of a thrust, in the classes' 16.16 power units.
std::int32_t thrust_hp(std::int32_t thrust) { return fx::div(fx::abs(thrust), kThrustPerHp); }

std::int32_t fold(std::int32_t d) {
  if (d > 1800) {
    d -= 3600;
  }
  if (d < -1800) {
    d += 3600;
  }
  return d;
}

}  // namespace

std::int32_t player_noise_level(const PlayerSub& player) {
  // player_noise_level (0x5c1c9)
  const SubClassParams& sub = player.sub();
  std::int32_t noise = fx::imul(thrust_hp(player.motion().power), 100) / sub.motor_hp * sub.noise_factor / 100;
  if (player.silent_running()) {
    noise /= 4;
  }
  return noise;
}

std::int32_t ship_noise_level(const Ship& ship) {
  // ship_noise_level (0x824a0) with veh_noise_percent_tail (0x7fde7)
  if (ship.sinking()) {
    return 30;
  }
  const std::int32_t noise = fx::imul(thrust_hp(ship.motion().power), 100) / ship.params().max_power + 20;
  return noise > 100 ? 100 : noise;
}

void ship_hydrophone_check(Ship& ship, SimContext& ctx) {
  // ship_hydrophone_check (0x6fe2c); the screw sound loop within 1000 yd is
  // left to the audio layer, its quiet-time reset is kept.
  PlayerSub* player = context_player(ctx);
  if (player == nullptr) {
    return;
  }
  ShipAi& ai = ship.ai();
  const bool was_heard = ai.heard.heard;
  const bool pinged = ai.heard.pinging;
  const fx::Vec3 pos = ship.position();
  const fx::Vec3 own = player->position();
  const std::int32_t dx = pos.x - own.x;
  const std::int32_t dz = pos.z - own.z;
  const auto distance = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
  ai.heard.heard = false;
  ai.heard.pinging = false;
  ai.heard.report_timer -= ctx.clock.frame_ticks();
  if (ai.heard.report_timer < 0) {
    ai.heard.report_timer = 0;
  }
  Sensors* sensors = context_sensors(ctx);
  if (player->sub().bridge_height_ft < player->depth_ft()) {
    const std::int32_t relative = fx::angle_sub(fx::heading_from_delta(dx, dz), player->motion().heading).tenths();
    const bool sonar_damaged = player->systems()[static_cast<std::size_t>(PlayerSystem::kSoundHeads)].impaired();
    std::int32_t noise = player_noise_level(*player) * 8000 / 100;
    if (relative > 1700 && relative < 1900) {
      noise *= 5;
    }
    if (ctx.env.thermal_layer_ft <= player->depth_ft()) {
      noise <<= 2;
    }
    const std::int32_t screws =
        fx::imul(thrust_hp(ship.motion().power), 100) / ship.params().max_power * 6000 / 100 - noise;
    std::int32_t pings = 10000 - noise / 8;
    if (screws > 0 || pings > 0) {
      if (!pinged) {
        pings = distance - 1;
      }
      if (distance < screws || distance < pings) {
        if (sensors != nullptr) {
          sensors->reset_quiet_time();
        }
        if (!sonar_damaged && ai.cls.screw != ScrewSound::kNone) {
          ai.heard.bearing = Angle10(relative);
          ai.heard.heard = true;
          ai.heard.screw = ai.cls.screw;
          if (!was_heard && !pinged && ai.heard.report_timer == 0) {
            ctx.crew_message(ai.cls.screw == ScrewSound::kHighSpeed ? 41 : 42, relative / 10);
            ctx.clock.reset_compression();
            ai.heard.report_timer = 18000;
          }
        }
        if (pinged) {
          if (!sonar_damaged) {
            ai.sonar.chart_echo = true;
          }
          const std::int32_t volume = (pings - distance) * 0x7f / pings;
          ctx.sfx(volume < 0x4b ? 0x33 : 0x32, volume);
        }
      }
    }
  } else {
    ai.heard.report_timer = 0;
  }
  if (ai.cls.screw != ScrewSound::kNone && distance < 1000 && fx::abs(ship.motion().power) > 0 &&
      sensors != nullptr) {
    sensors->reset_quiet_time();
  }
}

void ai_ship_sonar_search(Ship& ship, SimContext& ctx, std::int32_t dt) {
  // ai_ship_sonar_search (0x705f4)
  ShipAi& ai = ship.ai();
  if (!ai.cls.has_sonar) {
    ai.sonar.active = false;
    return;
  }
  const MotionState& m = ship.motion();
  bool ping = false;
  bool echo_heard = false;
  if (!ai.sonar.active) {
    ai.sonar.echo = false;
    ai.sonar.beam = 0;
  } else {
    ai.sonar.ping_timer -= dt;
    if (ai.sonar.ping_timer < 0) {
      // Every 4 s and up to one more.
      ai.sonar.ping_timer += ctx.random.random_int(0xffff) + 0x40000;
      ping = true;
      if (!ai.sonar.echo) {
        ai.sonar.beam = fx::angle_sub(Angle10(ai.sonar.beam), Angle10(120)).tenths();
        if (ai.sonar.beam > 1200 && ai.sonar.beam < 2500) {
          ai.sonar.beam = 1200;
        }
      } else {
        const Angle10 to_contact =
            fx::heading_from_delta(ai.contact.position.x - m.position.x, ai.contact.position.z - m.position.z);
        ai.sonar.beam = fx::angle_sub(to_contact, m.heading).tenths();
      }
      // Never into the arc astern.
      if (ai.sonar.beam > 1200 && ai.sonar.beam < 2500) {
        ai.sonar.beam = ai.sonar.beam < 1800 ? 1200 : 2500;
      }
      ai.heard.pinging = true;
      ai.sonar.chart_beam = ai.sonar.beam;
    }
  }
  const std::int32_t crew = ship.setup().crew;
  const std::int32_t sea = ctx.env.sea_state;
  std::int32_t active_yd = (crew + 1) * 2000 / 5 + 2000;
  active_yd -= active_yd * sea / 8;
  std::int32_t passive_yd = (crew + 1) * 5000 / 5 + 5000;
  passive_yd -= passive_yd * sea / 8;
  std::int32_t strength = 0;
  if (m.speed_tenths > 50) {
    passive_yd -= (m.speed_tenths - 50) * passive_yd / 100;
    if (passive_yd < 0) {
      passive_yd = 0;
    }
  }
  if (m.speed_tenths > 120) {
    active_yd -= (m.speed_tenths - 120) * active_yd / 100;
    if (active_yd < 0) {
      active_yd = 0;
    }
  }
  if (ctx.sim == nullptr) {
    return;
  }
  const auto& vehicles = ctx.sim->vehicles();
  // A loud noise near the ship deafens the whole search (only explosions
  // mask; see docs/structures.md "Bugs in the original").
  std::size_t searched = vehicles.size();
  for (const auto& v : vehicles) {
    const std::int32_t noise = v->masks_sonar() ? v->noise_level() : 0;
    if (noise > 0) {
      const fx::Vec3 p = v->position();
      const auto d =
          static_cast<std::int32_t>(fx::approx_distance(p.x - m.position.x, p.z - m.position.z) / kUnitsPerYard);
      if (d <= noise * 1200 / 100) {
        searched = 0;
        break;
      }
    }
  }
  ShipAiContact estimate;
  for (std::size_t i = 0; i < searched; ++i) {
    Vehicle& v = *vehicles[i];
    const std::int32_t noise = v.is_submarine() ? v.noise_level() : -1;
    if (noise < 0) {
      continue;
    }
    const fx::Vec3 p = v.position();
    const Fixed16 speed = v.speed();
    const Angle10 heading = v.heading();
    const std::int32_t dx = p.x - m.position.x;
    const std::int32_t dz = p.z - m.position.z;
    const auto d = static_cast<std::int32_t>(fx::approx_distance(dx, dz) / kUnitsPerYard);
    const std::int32_t depth_ft = -p.y / kUnitsPerFoot;
    const Angle10 bearing = fx::heading_from_delta(dx, dz);
    const std::int32_t relative = fx::angle_sub(bearing, m.heading).tenths();
    if (depth_ft >= ctx.env.thermal_layer_ft || (relative >= 1701 && relative <= 1899)) {
      continue;
    }
    if (d < noise * passive_yd / 100 && d > 200) {
      strength = (passive_yd - d) * 100 / passive_yd;
    }
    if (ai.sonar.active && ping && d < active_yd && d > 400) {
      const std::int32_t off = fold(relative - ai.sonar.beam);
      if (off > -76 && off < 76) {
        std::int32_t aspect = fx::angle_sub(fx::angle_add(bearing, Angle10(1800)), heading).tenths();
        if (aspect > 1800) {
          aspect = fx::angle_sub(Angle10(3600), Angle10(aspect)).tenths();
        }
        aspect = fx::abs(900 - aspect);
        strength = (active_yd - d) * ((900 - aspect) / 10 + 10) / active_yd;
        if (strength < 15) {
          strength = 0;
        }
        if (strength != 0) {
          echo_heard = true;
        }
      }
    }
    if (strength > 0) {
      // The position error grows as the signal and the crew drop.
      const std::int32_t error = (100 - strength) * ((2 - crew) * 200 + 200) / 100;
      const Angle10 skew(error / 20);
      estimate.heading = ctx.random.random_int(1) == 0 ? fx::angle_sub(heading, skew) : fx::angle_add(heading, skew);
      estimate.position = {p.x + error * kUnitsPerFoot, p.y + error / 2, p.z + error * kUnitsPerFoot};
      estimate.vehicle = &v;
      estimate.speed = speed + Fixed16::from_raw((error / 100) << 16);
      const std::int32_t floor_y = ai.cls.draft_ft * kUnitsPerFoot + m.keel_clearance + 16;
      if (estimate.position.y > -800) {
        estimate.position.y = -800;
      }
      if (estimate.position.y < floor_y) {
        estimate.position.y = floor_y;
      }
      if (estimate.speed.raw() < 0) {
        estimate.speed = Fixed16();
      }
      estimate.age = 0;
    }
  }
  if (strength > 0) {
    // Another noise near the estimate cancels it: within noise x 8 yd, or in
    // the beam no farther than the estimate's range from the map origin (the
    // original's slips, kept: that range and the bearing taken x - x, x - z).
    const auto origin_range =
        static_cast<std::int32_t>(fx::approx_distance(estimate.position.x, estimate.position.z) / kUnitsPerYard);
    for (const auto& v : vehicles) {
      if (v.get() == &ship || v.get() == estimate.vehicle) {
        continue;
      }
      const std::int32_t noise = v->noise_level();
      if (noise <= 0) {
        continue;
      }
      const fx::Vec3 p = v->position();
      const auto d = static_cast<std::int32_t>(
          fx::approx_distance(p.x - estimate.position.x, p.z - estimate.position.z) / kUnitsPerYard);
      if (d > noise * 800 / 100) {
        const Angle10 b = fx::heading_from_delta(p.x - m.position.x, p.x - m.position.z);
        const std::int32_t off = fold(fx::angle_sub(b, m.heading).tenths() - ai.sonar.beam);
        if (off < -75 || off > 75 || origin_range < d) {
          continue;
        }
      }
      strength = 0;
      break;
    }
  }
  if (strength > 0) {
    ai.contact = estimate;
    broadcast_contact(ctx, estimate.position, estimate.vehicle);
  }
  if (crew > 0 && ai.contact.age > 0) {
    // Dead reckoning on the estimated course and speed.
    const fx::SinCos sc = fx::angle_sincos(ai.contact.heading);
    const std::int32_t v = fx::mul(ai.contact.speed.raw(), kUnitsPerSecondPerKnot);
    ai.contact.position.x += fx::trunc(fx::mul(fx::mul(v, sc.sin), dt));
    ai.contact.position.z += fx::trunc(fx::mul(fx::mul(v, sc.cos), dt));
  }
  if (ai.sonar.active && ping) {
    ai.sonar.echo = strength > 0;
    if (strength != 0 && echo_heard) {
      ctx.sfx(0x34, strength * 0x7f / 100);
    }
  }
}

}  // namespace sh
