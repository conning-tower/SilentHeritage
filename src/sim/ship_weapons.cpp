#include "sim/ship_weapons.h"

#include <algorithm>
#include <array>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "sim/ballistic.h"
#include "sim/deck_gun.h"
#include "sim/depth_charge.h"
#include "sim/effects.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/simulation.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

// The rack charges' offsets across the stern (ft, SH.EXE 0x102fd8).
const std::array<std::int32_t, 4>& rack_offsets_ft() {
  static const std::array<std::int32_t, 4> table = exe_i32_array<4>(game_exe(), 0x102fd8);
  return table;
}
// The throwers' angles (deg, SH.EXE 0x102fe8, one per thrower: TYPE C/D
// escorts have 12).
const std::array<std::int32_t, 12>& thrower_angles() {
  static const std::array<std::int32_t, 12> table = [] {
    constexpr std::uint32_t kAddress = 0x102fe8;
    const auto t = exe_i32_array<12>(game_exe(), kAddress);
    check_exe_table(std::ranges::all_of(t, [](std::int32_t a) { return a >= -180 && a <= 180; }), "thrower angle",
                    kAddress);
    return t;
  }();
  return table;
}
constexpr std::int32_t kRackPitch = -100;    // Rolled off: no throw.
constexpr std::int32_t kThrowerPitch = 450;  // Thrown at 45 deg.

// weapons_find_gun (0x51cad) by id.
const Gun* find_gun(const GameTables& tables, std::int32_t id) {
  for (const Gun& g : tables.weapons.guns) {
    if (static_cast<std::int32_t>(g.id) == id) {
      return &g;
    }
  }
  return nullptr;
}

}  // namespace

void ShipWeapons::fire_guns(Ship& ship, SimContext& ctx, std::int32_t dt, std::span<const Angle10> friendly_bearings) {
  // ai_ship_fire_guns (0x12f7a)
  const ShipClass* cls = ship.setup().ship_class;
  if (cls == nullptr) {
    return;
  }
  ShipAi& ai = ship.ai();
  const MotionState& m = ship.motion();
  const std::int32_t crew = ship.setup().crew;
  const bool merchant = cls->role == ShipRole::kMerchant;
  for (std::int32_t mount = 0; mount < 2; ++mount) {
    if (!cls->gun_mounts.at(static_cast<std::size_t>(mount))) {
      continue;
    }
    if (ai.contact.vehicle != nullptr && ai.contact.vehicle->class_id() == VehicleClass::kPlayer) {
      ai.attacked_player = true;
    }
    std::int32_t& reload = mount == 0 ? ai.gun_reload : ai.gun_reload_aft;
    if (reload > 0) {
      reload -= dt;
    }
    if (reload >= 1) {
      continue;
    }
    reload = 0;
    const std::int32_t third = static_cast<std::int32_t>(cls->length_ft) / 3 * kUnitsPerFoot;
    const fx::Vec3 off = fx::transform(m.orientation, {0, 0, mount == 1 ? -third : third});
    const std::int32_t gx = m.position.x + off.x;
    const std::int32_t gy = m.position.y + ai.cls.lookout_ft * 32;
    const std::int32_t gz = m.position.z + off.z;
    const fx::Vec3& target = ai.contact.position;
    const auto yards = static_cast<std::int32_t>(fx::approx_distance(target.x - gx, target.z - gz) / kUnitsPerYard);
    const Gun* gun = find_gun(*tables_, static_cast<std::int32_t>(cls->gun_id));
    if (gun == nullptr) {
      continue;
    }
    const auto max_range = static_cast<std::int32_t>(gun->max_range_yd);
    const auto velocity = static_cast<std::int32_t>(gun->muzzle_velocity_fps);
    const std::int32_t range = max_range - ((merchant ? 2 : 0) + (2 - crew)) * max_range / 6;
    if (yards > range || yards <= 199) {
      continue;
    }
    Angle10 bearing =
        crew < 1 ? fx::heading_from_delta(target.x - gx, target.z - gz)
                 : gun_lead_bearing(velocity, ai.contact.heading, ai.contact.speed, target.x, target.z, gx, gz);
    const std::int32_t scatter = (merchant ? 2 : 1) * (3 - crew) * 10 * ai.gun_accuracy / 1000 + 2;
    if (ctx.random.random_int(2) == 0) {
      bearing = fx::angle_sub(bearing, Angle10(ctx.random.random_int(scatter)));
    } else {
      bearing = fx::angle_add(bearing, Angle10(ctx.random.random_int(scatter)));
    }
    std::int32_t off_side = fx::angle_sub(bearing, m.heading).tenths();
    if (mount > 0) {
      off_side = fx::angle_add(Angle10(off_side), Angle10(1800)).tenths();
    }
    if (off_side > 1800) {
      off_side -= 3600;
    }
    if (off_side <= -1201 || off_side >= 1201) {
      continue;
    }
    bool friendly = false;
    for (const Angle10 f : friendly_bearings) {
      std::int32_t d = fx::angle_sub(f, bearing).tenths();
      if (d > 1800) {
        d -= 3600;
      }
      if (-0x51 < d && d < 0x51) {
        friendly = true;
      }
    }
    if (friendly) {
      continue;
    }
    const std::int32_t base = (2 - crew) * (merchant ? 1500 : 500);
    const std::int32_t falloff = yards * 1000 / range * yards / 1000;
    const std::int32_t spread = yards * ai.gun_accuracy / 1000;
    const std::int32_t error = falloff / 16 + spread / 16 + ctx.random.random_int(base);
    const std::int32_t miss = ctx.random.random_int(error);
    std::int32_t elevation = ballistic_elevation_for_range(velocity, (error / 2 - miss + yards) * kUnitsPerYard);
    ai.gun_elevation = Angle10(elevation);
    if (elevation >= 0x1c3 || elevation < 0) {
      continue;
    }
    const std::int32_t worst = (3 - crew) * (merchant ? 100 : 30);
    ai.gun_accuracy -= (merchant ? 20 : 50) * (crew + 1);
    if (ai.gun_accuracy < worst) {
      ai.gun_accuracy = worst;
    }
    const PlayerCombat* combat = ctx.sim != nullptr ? ctx.sim->player_combat() : nullptr;
    const std::int32_t swell = combat != nullptr ? combat->deck_gun.swell : 30;
    const std::int32_t roll = (swell - 30) / ((crew + 1) * 2);
    elevation = roll < 0 ? fx::angle_add(Angle10(elevation), Angle10(std::abs(roll))).tenths()
                         : fx::angle_sub(Angle10(elevation), Angle10(std::abs(roll))).tenths();
    ai.gun_elevation = Angle10(elevation);
    spawn_explosion(ctx, {gx, gy, gz}, ExplosionKind::kMuzzleFlash);
    ctx.sim->add(std::make_unique<Shell>(fx::Vec3{gx, 1, gz}, bearing, Angle10(elevation), velocity,
                                         static_cast<std::int32_t>(gun->shell_caliber_in), &ship));
    std::int32_t time = (3 - crew) * static_cast<std::int32_t>(gun->reload_time_s);
    if (merchant) {
      time *= 3;
    }
    reload = (ctx.random.random_int(time * 3 / 4) + time) << 16;
  }
}

void ShipWeapons::drop_depth_charges(Ship& ship, SimContext& ctx, std::int32_t depth_ft) {
  // ai_ship_drop_depth_charges (0x12dae)
  ShipAi& ai = ship.ai();
  if (ai.depth_charges == 0) {
    return;
  }
  const MotionState& m = ship.motion();
  const std::int32_t stern = ai.cls.length_ft / 2 * -kUnitsPerFoot;
  const std::int32_t y = m.position.y + ai.cls.lookout_ft * kUnitsPerFoot;
  const Angle10 aft = fx::angle_add(m.heading, Angle10(1800));
  for (std::int32_t k = 0; k < ai.cls.rack_charges && ai.depth_charges != 0; ++k) {
    const fx::Vec3 off =
        fx::transform(m.orientation, {rack_offsets_ft().at(static_cast<std::size_t>(k)) * kUnitsPerFoot, 0, stern});
    const std::int32_t depth = ctx.random.random_int(30) + depth_ft - 15;
    spawn_depth_charge(ctx, {m.position.x + off.x, y, m.position.z + off.z}, aft, Angle10(kRackPitch), depth);
    --ai.depth_charges;
    ++ai.charges_dropped;
  }
  if (ai.depth_charges == 0) {
    return;
  }
  const fx::Vec3 off = fx::transform(m.orientation, {0, 0, stern});
  for (std::int32_t k = 0; k < ai.cls.thrower_charges && ai.depth_charges != 0; ++k) {
    const std::int32_t depth = ctx.random.random_int(50) + depth_ft - 25;
    const Angle10 heading = fx::angle_add(aft, Angle10(thrower_angles().at(static_cast<std::size_t>(k)) * 10));
    spawn_depth_charge(ctx, {m.position.x + off.x, y, m.position.z + off.z}, heading, Angle10(kThrowerPitch), depth);
    --ai.depth_charges;
  }
}

}  // namespace sh
