#include "sim/player_combat.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "sim/geometry.h"
#include "sim/player_sub.h"
#include "sim/sensors.h"
#include "sim/shell.h"
#include "sim/simulation.h"
#include "sim/torpedo.h"
#include "sim/weapon_math.h"

namespace sh {

namespace {

// The firing orders by tube (SH.EXE 0x10f4cc).
const std::array<std::string, 10>& fire_speech() {
  static const std::array<std::string, 10> table = exe_string_array<10>(game_exe(), 0x10f4cc);
  return table;
}
// The "tube not loaded" report.
constexpr const char* kNotLoadedSpeech = "WO0022.RAW";

}  // namespace

PlayerCombat::PlayerCombat(const GameTables& tables, const SubClass& boat)
      : tubes(boat), damage(boat), deck_gun(boat), tables_(&tables), boat_(&boat) {}

OwnBoat PlayerCombat::own_boat(const PlayerSub& sub, const SimContext& ctx) const {
  return {sub.motion(), static_cast<std::int32_t>(boat_->length_ft), ctx.env.options, crew, sub.side()};
}

TorpedoVehicle* PlayerCombat::fire_tube(SimContext& ctx, PlayerSub& sub, std::size_t tube) {
  // fire_tube (0x7cd70)
  if (tube >= static_cast<std::size_t>(tubes.count())) {
    return nullptr;
  }
  const auto number = static_cast<std::int32_t>(tube) + 1;
  const std::int32_t content = tubes.content(tube);
  if (content < 1) {
    if (content < 0) {
      const std::int32_t minutes = std::max(fx::trunc(tubes.timer(tube)) / 60, 1);
      ctx.events.crew_messages.push_back({kMsgStillLoading, number, {}, minutes});
      if (ctx.env.speech) {
        ctx.events.speech.emplace_back(kNotLoadedSpeech);
      }
    } else {
      ctx.crew_message(kMsgNotLoaded);
    }
    return nullptr;
  }
  if (tubes.damage(tube).flags != 0) {
    ctx.crew_message(kMsgTubeDamaged, number);
    return nullptr;
  }
  const OwnBoat own = own_boat(sub, ctx);
  if (tdc.mode() == Tdc::Mode::kAuto &&
      static_cast<std::uint32_t>(content) != torpedo_types[static_cast<std::size_t>(tdc.type_index())].weapon_id) {
    // The TDC switches to the tube's torpedo type.
    std::int32_t index = 0;
    while (index < 3 && torpedo_types[static_cast<std::size_t>(index)].weapon_id != static_cast<std::uint32_t>(content)) {
      ++index;
    }
    if (index < 3) {
      tdc.set_type_index(index);
      if (tdc.target() != nullptr) {
        tdc.update_tube_solutions(own, *tables_, torpedo_types);
      }
    }
  }
  const Torpedo* weapon = tables_->find_torpedo_by_id(static_cast<std::uint32_t>(content));
  if (weapon == nullptr) {
    return nullptr;
  }
  const bool stern = tubes.is_stern(tube);
  const MotionState& m = sub.motion();
  std::int32_t offset = (own.length_ft / 2 + static_cast<std::int32_t>(weapon->length_ft)) * kUnitsPerFoot;
  Angle10 heading = m.heading;
  if (stern) {
    offset = -offset;
    heading = fx::angle_sub(m.heading, Angle10(1800));
  }
  const Angle10 gyro = tdc.gyro(stern ? Tdc::kStern : Tdc::kBow);
  const std::int32_t off_axis = gyro.tenths() > 1800 ? 3600 - gyro.tenths() : gyro.tenths();
  if (off_axis > kGyroLimit) {
    ctx.crew_message(kMsgGyroLimit);
    return nullptr;
  }
  std::int32_t setting = tdc.speed_setting();
  if (static_cast<std::int32_t>(weapon->speed_settings) <= setting) {
    setting = 0;
  }
  // It leaves the tube turned at most 10 deg, then turns to the gyro course.
  std::int32_t turn = gyro.tenths();
  if (1800 < turn && turn < 3500) {
    turn = 3500;
  }
  if (turn < 1800 && 100 < turn) {
    turn = 100;
  }
  const Angle10 launch_heading = fx::angle_add(heading, Angle10(turn));
  const fx::Vec3 launch = fx::transform(m.orientation, {0, 0, offset});
  const fx::Vec3 position{m.position.x + launch.x, m.position.y + 0x30, m.position.z + launch.z};
  const std::int32_t speed = static_cast<std::int32_t>(setting == 0 ? weapon->speed_low_kts : weapon->speed_high_kts);
  const Angle10 course =
      fx::angle_add(fx::angle_add(launch_heading, fx::angle_sub(gyro, Angle10(turn))), tdc.spread_angle());
  auto torpedo = std::make_unique<TorpedoVehicle>(*weapon, &sub, position, launch_heading, m.pitch,
                                                  Fixed16::from_int(speed), setting, course, tdc.depth_ft());
  tdc.track_torpedo(stern ? Tdc::kStern : Tdc::kBow, torpedo.get());
  TorpedoVehicle& fired = ctx.sim->add(std::move(torpedo));
  const fx::Vec3 muzzle = fx::transform(m.orientation, {0, 0, fx::sign(offset) * 0x398});
  fired.add_wake_point({m.position.x + muzzle.x, muzzle.y, m.position.z + muzzle.z});
  ctx.crew_message(kMsgFire, number);
  ctx.sfx(kSfxFire);
  if (ctx.env.speech) {
    ctx.events.speech.push_back(fire_speech().at(tube) + ".RAW");
  }
  tubes.clear(tube);
  tubes.start_reload(ctx, tube, crew);
  return &fired;
}

void PlayerCombat::update_damage(SimContext& ctx, PlayerSub& sub, Fixed16 dt) {
  damage.update(ctx, sub, tubes, dt, crew);
}

void PlayerCombat::update_reloads(SimContext& ctx, PlayerSub& sub, Fixed16 dt) {
  tubes.update(ctx, sub.silent_running(), dt, crew);
}

void PlayerCombat::update_aa_guns(SimContext& ctx, PlayerSub& sub, Fixed16 dt) {
  // player_update_aa_guns (0x5ea5d)
  static constexpr std::array<std::int32_t, 3> kFireInterval = {0x18000, 0x10000, 0x8000};  // 0x10cbbc by crew
  constexpr std::int32_t kMsgAaSecured = 63;
  constexpr std::uint32_t kAaRange = 0x9600;  // 800 yd
  const fx::Vec3 own = sub.position();
  const std::int32_t gun_y = sub.sub().bridge_height_ft * kUnitsPerFoot + own.y;
  if (gun_y <= 0 && aa_guns.manned) {
    // cmd_man_aa_guns(0, -1)
    aa_guns.manned = false;
    ctx.crew_message(kMsgAaSecured);
  }
  if (!aa_guns.manned) {
    aa_guns.fire_timer = 0;
    return;
  }
  const Sensors* sensors = context_sensors(ctx);
  Vehicle* target = sensors != nullptr ? sensors->reports().nearest_aircraft() : nullptr;
  if (target == nullptr) {
    return;
  }
  aa_guns.fire_timer -= dt.raw();
  if (aa_guns.fire_timer >= 1) {
    return;
  }
  const std::int32_t interval = kFireInterval.at(static_cast<std::size_t>(crew));
  aa_guns.fire_timer = interval + ctx.random.random_int(interval);
  const fx::Vec3 t = target->position();
  const Angle10 bearing = fx::heading_from_delta(t.x - own.x, t.z - own.z);
  if (fx::approx_distance(t.x - own.x, t.z - own.z) > kAaRange) {
    return;
  }
  const Angle10 elevation = aa_gun_elevation(t.x - own.x, t.y - gun_y, t.z - own.z, ctx.env.rules);
  const std::int32_t error = ((ctx.env.options & 0xf) >> 2) + 1;
  const std::int32_t e_down = ctx.random.random_int(error);
  const std::int32_t e_up = ctx.random.random_int(error);
  const Angle10 aim_elevation = fx::angle_sub(fx::angle_add(elevation, Angle10(e_up)), Angle10(e_down));
  const std::int32_t h_left = ctx.random.random_int(error);
  const std::int32_t h_right = ctx.random.random_int(error);
  const Angle10 aim_heading = fx::angle_sub(fx::angle_add(bearing, Angle10(h_right)), Angle10(h_left));
  if (aa_guns_fire_at(aim_elevation, ctx.env.rules) && ctx.sim != nullptr) {
    ctx.sim->add(std::make_unique<Shell>(fx::Vec3{own.x, gun_y, own.z}, aim_heading, aim_elevation, 4000, 0, &sub));
  }
}

Angle10 aa_gun_elevation(std::int32_t dx, std::int32_t dy, std::int32_t dz, const MissionRules& rules) {
  // The original's elevation runs over |dx| alone (|dz| when dx is 0); over
  // the horizontal range with the rules' corrections (not in the original).
  const std::int32_t across = rules.fix_obvious_errors ? static_cast<std::int32_t>(fx::approx_distance(dx, dz))
                                                       : fx::abs(dx == 0 ? dz : dx);
  return fx::heading_from_delta(dy, across);
}

bool aa_guns_fire_at(Angle10 aim, const MissionRules& rules) {
  // Unsigned like the original's: a target below the gun wraps to a high
  // elevation and is fired at too; signed with the rules' corrections.
  const std::int32_t tenths = aim.tenths();
  return (rules.fix_obvious_errors && tenths > 1800 ? tenths - 3600 : tenths) > 19;
}

bool PlayerCombat::take_damage(SimContext& ctx, PlayerSub& sub, DamageKind kind, std::int32_t distance,
                               std::int32_t amount) {
  return damage.take_damage(ctx, sub, tubes, kind, distance, amount, crew);
}

void PlayerCombat::update_frame(SimContext& ctx, PlayerSub& sub) {
  const OwnBoat own = own_boat(sub, ctx);
  const std::int32_t follow = tdc.update(ctx.clock.frame_ticks(), ctx.clock.effective_compression(), own, *tables_,
                                         torpedo_types, sensing);
  if (follow >= 0) {
    view_bearing = Angle10(follow);
  }
  deck_gun.update(ctx, sub, own, ctx.clock.frame_ticks(), sensing);
}

void PlayerCombat::forget(const Vehicle& removed) {
  tdc.forget(removed);
  deck_gun.forget(removed);
}

}  // namespace sh
