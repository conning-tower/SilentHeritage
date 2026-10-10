#include "sim/player_sub.h"

#include <algorithm>
#include <string>

#include "assets/exe_tables.h"
#include "assets/game_exe.h"
#include "sim/geometry.h"
#include "sim/player_combat.h"
#include "sim/sensors.h"
#include "sim/simulation.h"

namespace sh {

using namespace geom;

namespace {

// Crew message ids (crew message table).
constexpr std::int32_t kMsgRammed = 1;
constexpr std::int32_t kMsgPilotRescued = 67;  // "Pilot has been rescued, sir!"
constexpr std::int32_t kRescueSpeedTenths = 50;  // 0x11aa5c at most 5 kn
constexpr std::int32_t kMsgPeriscopeDepth = 5;
constexpr std::int32_t kMsgSurface = 7;
constexpr std::int32_t kMsgBlowBallast = 12;
constexpr std::int32_t kMsgUnableToBlow = 13;
constexpr std::int32_t kMsgDive = 14;
constexpr std::int32_t kMsgHelmRight = 8;
constexpr std::int32_t kMsgHelmLeft = 9;
constexpr std::int32_t kMsgRudderAmidships = 10;
constexpr std::int32_t kMsgFuelLow = 28;
constexpr std::int32_t kMsgOutOfFuel = 29;
constexpr std::int32_t kMsgAirLow = 33;
constexpr std::int32_t kMsgOutOfAir = 34;
constexpr std::int32_t kMsgOxygenLow = 35;
constexpr std::int32_t kMsgBatteriesLow = 36;
constexpr std::int32_t kMsgBatteriesDead = 37;
constexpr std::int32_t kMsgLastWaypoint = 55;
constexpr std::int32_t kMsgTestDepth = 71;
// Grinding on the bottom while the screen shakes.
constexpr std::int32_t kSfxGrinding = 0x20;

// Rudder in degrees per helm step -7..7 (SH.EXE 0x10cb80).
const std::array<std::int32_t, 15>& helm_degrees() {
  static const std::array<std::int32_t, 15> table = [] {
    constexpr std::uint32_t kAddress = 0x10cb80;
    const auto t = exe_i32_array<15>(game_exe(), kAddress);
    check_exe_table(t[0] == -35 && t[7] == 0 && t[14] == 35 && std::ranges::is_sorted(t), "helm", kAddress);
    return t;
  }();
  return table;
}
// The helmsman's reply by helm step (SH.EXE 0x10cbf4): 35 left ..
// amidships .. 35 right.
const std::array<std::string, 15>& helm_speech() {
  static const std::array<std::string, 15> table = exe_string_array<15>(game_exe(), 0x10cbf4);
  return table;
}
constexpr std::int32_t kHelmRudderCap = 350;           // Tenths of a degree.
constexpr std::int32_t kMapEdgeMargin = 0x76b10;       // World units.
constexpr std::int32_t kDeepCompression = 0x200;
constexpr std::int32_t kChargePerEngine = 0x965;       // Charge rate share of one diesel.
constexpr std::int32_t kOxygenUse = 0x2f6;             // Air units per second submerged.
constexpr std::int32_t kBatteryDrain = 0x258e;         // x (power / motor hp)^2 x 100.
constexpr std::int32_t kFuelLeak = 0x18000;            // x severity % per second.
constexpr std::int32_t kBatteryUnit = 100 << 16;       // Battery fraction per unit.

// Returns `v` sign-extended from its low 16 bits (as the original reads the
// shaft power before dividing it).
std::int32_t low16(std::int32_t v) { return static_cast<std::int16_t>(v); }

}  // namespace

PlayerSub::PlayerSub(const SubClassParams& sub, fx::Vec3 position, Angle10 heading, std::int32_t telegraph,
                     std::int32_t fuel)
    : sub_(sub), telegraph_(telegraph), fuel_(fuel) {
  motion_.position = position;
  motion_.heading = heading;
}

void PlayerSub::set_helm(std::int32_t step) {
  helm_ = step;
  motion_.steering = false;
}

void PlayerSub::set_course(Angle10 course) {
  motion_.ordered_heading = course;
  motion_.steering = true;
}

void PlayerSub::set_ordered_depth(std::int32_t depth_ft) {
  if (depth_ft != -1 && depth_ft < sub_.draft_ft) {
    depth_ft = sub_.draft_ft;
  }
  dive_.ordered_depth_ft = depth_ft;
}

void PlayerSub::order_dive(SimContext& ctx) {
  ctx.clock.reset_compression();
  const std::int32_t depth = dive_.depth_ft < 200 ? 200 : -1;
  if (depth != dive_.ordered_depth_ft) {
    ctx.crew_message(kMsgDive);
  }
  telegraph_ = 5;
  dive_.planes = -80;
  set_ordered_depth(depth);
}

void PlayerSub::order_periscope_depth(SimContext& ctx) {
  if (dive_.ordered_depth_ft != sub_.periscope_depth_ft) {
    ctx.crew_message(kMsgPeriscopeDepth);
  }
  dive_.planes = -60;
  set_ordered_depth(sub_.periscope_depth_ft);
}

void PlayerSub::order_surface(SimContext& ctx) {
  if (sub_.draft_ft < dive_.depth_ft || sub_.displacement_surfaced * 100 < dive_.ballast) {
    ctx.clock.reset_compression();
    dive_.planes = 60;
    set_ordered_depth(-1);
    ctx.crew_message(kMsgSurface);
  }
}

void PlayerSub::order_blow_ballast(SimContext& ctx) {
  if ((sub_.draft_ft < dive_.depth_ft || sub_.displacement_surfaced * 100 < dive_.ballast) && dive_.air_pct != 0) {
    ctx.clock.reset_compression();
    dive_.mode = DiveState::Mode::kBlow;
    dive_.planes = 80;
    telegraph_ = 5;
    set_ordered_depth(-1);
    ctx.crew_message(kMsgBlowBallast);
    return;
  }
  ctx.crew_message(kMsgUnableToBlow);
}

void PlayerSub::follow_route(const Route& route) {
  route_ = route;
  route_index_ = 0;
  follow_route_ = true;
}

std::int32_t PlayerSub::battery_capacity() {
  std::int32_t capacity = kBatteryCapacity;
  for (const PlayerSystem s : {PlayerSystem::kFwdBattery, PlayerSystem::kAftBattery}) {
    if (system(s).impaired()) {
      capacity -= system(s).severity * 300 / 100;
    }
  }
  return capacity;
}

bool PlayerSub::has_energy() const { return diesel_mode_ ? fuel_ != 0 : battery_ != 0; }

void PlayerSub::stop_running_engines() {
  for (EngineRoom& room : engines_) {
    EnginePart& part = room[static_cast<std::size_t>(engine_mode())];
    if ((part.flags & EnginePart::kRunning) != 0) {
      part.flags ^= EnginePart::kRunning;
      part.power_pct = 0;
    }
  }
}

void PlayerSub::init(SimContext& ctx) {
  const std::int32_t surface_y = sub_.draft_ft * -kUnitsPerFoot;
  if (surface_y <= motion_.position.y) {
    motion_.position.y = surface_y;
  }
  for (std::size_t i = 0; i < engines_.size(); ++i) {
    engines_[i][0] = {0, static_cast<std::int32_t>(i) < sub_.motor_count, 0};
    engines_[i][1] = {0, static_cast<std::int32_t>(i) < sub_.diesel_count, 0};
  }
  battery_ = battery_capacity();
  battery_fraction_ = 0;
  if (fuel_ == -1) {
    fuel_ = sub_.fuel_capacity;
  }
  fuel_fraction_ = 0;
  helm_ = 0;
  announced_helm_ = 0;
  refresh_air(ctx);
  sub_dive_state_init(dive_, motion_, sub_);
  applied_telegraph_ = -1;
  applied_mode_ = 2;
  update_engines(true);
  const HullParams hull = sub_hull_params(sub_, dive_);
  const std::int32_t wave_pct = sub_wave_effect_pct(ctx.env.sea_state, dive_.depth_ft, sub_.bridge_height_ft);
  motion_.speed = motion_speed_for_power(motion_.ordered_power, hull, ctx.env.sea_state, wave_pct);
  motion_.power = motion_.ordered_power;
  motion_init(motion_, ctx.floor);
}

void PlayerSub::update(SimContext& ctx, Fixed16 dt) {
  // player_update (0x5bf89): the contact reports, the ramming check (its
  // damage draws come before the radar's), the radars.
  Sensors* sensors = context_sensors(ctx);
  if (sensors != nullptr) {
    sensors->update_player_reports(ctx, *this, dt);
  }
  PlayerCombat* combat = ctx.sim != nullptr ? ctx.sim->player_combat() : nullptr;
  check_ramming(ctx, combat);
  if (sensors != nullptr) {
    sensors->update_player_radar(ctx, *this, dt);
  }
  check_oxygen(ctx, dt);
  if (combat != nullptr) {
    combat->update_damage(ctx, *this, dt);
  }
  update_helm_and_dive(ctx, combat, dt);
  if (combat != nullptr) {
    combat->update_reloads(ctx, *this, dt);
    combat->update_aa_guns(ctx, *this, dt);
  }
  // Then the periscope goes down once under water or damaged, and the
  // swell moves (the deck gun's elevation reads it).
  if (periscope_raised_ && !can_raise_periscope()) {
    toggle_periscope(ctx);
  }
  update_swell(ctx, dt);
  if (combat != nullptr) {
    combat->deck_gun.swell = swell_.bob;
  }
}

bool PlayerSub::can_raise_periscope() const {
  // player_can_raise_periscope (0x60784)
  return sub_.periscope_height_ft * kUnitsPerFoot + motion_.position.y > 0 &&
         !systems_[static_cast<std::size_t>(PlayerSystem::kPeriscope)].impaired();
}

void PlayerSub::toggle_periscope(SimContext& ctx) {
  // player_toggle_periscope (0x6073e): crew messages #3 Up / #4 Down.
  if (!periscope_raised_ && !can_raise_periscope()) {
    return;
  }
  periscope_raised_ = !periscope_raised_;
  if (!systems_[static_cast<std::size_t>(PlayerSystem::kPeriscope)].impaired()) {
    ctx.crew_message(periscope_raised_ ? 3 : 4);
  }
}

void PlayerSub::update_swell(SimContext& ctx, Fixed16 dt) {
  // player_update_swell (0x5e93e): the offset swings between 30 - amplitude
  // and 30 + amplitude; each turn rolls a new rate and amplitude, larger on
  // a heavy sea while the bridge is above water.
  Swell& s = swell_;
  std::int32_t sea = ctx.env.sea_state;
  if (sea == 0) {
    s.bob = 30;
    s.rate = 1000;
    return;
  }
  s.offset = static_cast<std::int32_t>(static_cast<std::uint32_t>(s.offset) +
                                       static_cast<std::uint32_t>(dt.raw()) * static_cast<std::uint32_t>(s.rate) *
                                           static_cast<std::uint32_t>(s.direction));
  if (sea == 2 && sub_.bridge_height_ft * kUnitsPerFoot + motion_.position.y <= 0) {
    sea = 1;
  }
  if (sea == 2) {
    sea = 4;
  }
  const std::int32_t low = (30 - s.amplitude) << 16;
  const std::int32_t high = (s.amplitude + 30) << 16;
  if (s.direction < 0 && s.offset <= low) {
    s.offset = low;
    s.direction = -s.direction;
    s.rate = sea * 5 + ctx.random.random_int(sea * 7);
    s.amplitude = sea + ctx.random.random_int(sea * 5);
  } else if (s.direction > 0 && s.offset >= high) {
    s.offset = high;
    s.direction = -s.direction;
    s.rate = sea * 5 + ctx.random.random_int(sea * 7);
    s.amplitude = sea * 7 + ctx.random.random_int(5);
  }
  s.amplitude = std::min(s.amplitude, 30);
  s.bob = s.offset < 0 ? -(-s.offset >> 16) : s.offset >> 16;
}

void PlayerSub::check_ramming(SimContext& ctx, PlayerCombat* combat) {
  // player_update: touching a ship or submarine rams it (#1, damage kind 3).
  Vehicle* other = collision().partner;
  if (!collision().collided || other == nullptr) {
    return;
  }
  if (other->is_pilot()) {
    // Touching a raft: the pilot comes aboard when the boat can take him.
    pilot_alongside_ = other;
    rescue_pilot(ctx);
    return;
  }
  const VehicleClass c = other->class_id();
  if (c == VehicleClass::kShell || c == VehicleClass::kBomb || c == VehicleClass::kTorpedo ||
      c == VehicleClass::kAircraft) {
    return;
  }
  ctx.crew_message(kMsgRammed);
  if (combat != nullptr) {
    combat->damage.apply(ctx, *this, combat->tubes, DamageKind::kRam, 0, combat->crew);
  }
  ctx.clock.reset_compression();
}

bool PlayerSub::can_rescue_pilot() const {
  return pilot_alongside_ != nullptr && dive_.depth_ft <= sub_.draft_ft;
}

bool PlayerSub::rescue_pilot(SimContext& ctx) {
  // player_pilot_rescued (0x60c9e) with 0.
  if (!can_rescue_pilot() || motion_.speed_tenths > kRescueSpeedTenths || !pilot_alongside_->is_pilot()) {
    return false;
  }
  pilot_alongside_->remove();
  ctx.crew_message(kMsgPilotRescued);
  ++ctx.events.pilots_rescued;
  return true;
}

void PlayerSub::forget(const Vehicle& removed) {
  if (pilot_alongside_ == &removed) {
    pilot_alongside_ = nullptr;
  }
}

void PlayerSub::update_bounds() {
  // player_update_collision_box (0x5c247): from the keel to the bridge, or
  // submerged to the raised periscope or the radar mast.
  std::int32_t top_ft = sub_.bridge_height_ft;
  if (sub_.bridge_height_ft <= dive_.depth_ft) {
    top_ft = periscope_raised_ ? sub_.periscope_height_ft : sub_.radar_depth_ft + 4;
  }
  collision().box = make_hull_box(motion_.position, motion_.orientation, motion_.inverse, sub_.beam_ft * 8,
                                  sub_.length_ft * 8, motion_.position.y, motion_.position.y + top_ft * kUnitsPerFoot);
}

bool PlayerSub::take_damage(SimContext& ctx, Vehicle* /*attacker*/, DamageKind kind, std::int32_t distance,
                            std::int32_t amount) {
  PlayerCombat* combat = ctx.sim != nullptr ? ctx.sim->player_combat() : nullptr;
  return combat != nullptr && combat->take_damage(ctx, *this, kind, distance, amount);
}

void PlayerSub::refresh_air(SimContext& ctx) {
  // player_refresh_air (0x5e810)
  const std::int32_t extra = ctx.random.random_int(333);
  air_fraction_ = 0;
  air_ = 1000 + extra;
  air_max_ = 1000 + extra;
}

void PlayerSub::check_oxygen(SimContext& ctx, Fixed16 dt) {
  // player_check_oxygen (0x5e834): about one unit per 86 s submerged.
  const std::int32_t before = air_;
  if (dive_.depth_ft <= sub_.draft_ft) {
    if (air_ < air_max_) {
      refresh_air(ctx);
    }
    return;
  }
  air_fraction_ -= fx::mul(dt.raw(), kOxygenUse);
  while (air_fraction_ < 0) {
    air_fraction_ += 0x10000;
    --air_;
  }
  if (air_ < 1) {
    air_ = 0;
    ctx.events.game_over = 2;
    return;
  }
  for (std::int32_t level = 166, i = 0; i < 4; ++i, level /= 2) {
    if (level < before && air_ <= level) {
      ctx.crew_message(kMsgOxygenLow);
      ctx.clock.reset_compression();
      return;
    }
  }
}

void PlayerSub::update_engines(bool at_init) {
  // The order logic of player_update_engine_sounds (0x5cb90): a new telegraph
  // step, a change between diesels and motors, or an engine change spreads
  // the step's total % over the available units, up to 95 % each first.
  diesel_mode_ = dive_.depth_ft <= sub_.draft_ft;
  const auto mode = static_cast<std::size_t>(engine_mode());
  for (std::size_t i = 0; i < engines_.size(); ++i) {
    const EnginePart& part = engines_[i][mode];
    const auto key = static_cast<std::uint16_t>(part.flags | (part.fitted ? 0 : 0x400));
    if (key != applied_flags_[i]) {
      applied_telegraph_ = -1;
      applied_flags_[i] = key;
    }
  }
  if (telegraph_ != applied_telegraph_ || engine_mode() != applied_mode_) {
    for (EngineRoom& room : engines_) {
      EnginePart& part = room[mode];
      if (part.fitted) {
        part.flags &= static_cast<std::uint8_t>(~(EnginePart::kRunning | EnginePart::kCharging));
        part.power_pct = 0;
      }
    }
    const auto& table = diesel_mode_ ? sub_.power_surfaced : sub_.power_submerged;
    std::int32_t total = table.at(static_cast<std::size_t>(telegraph_));
    applied_telegraph_ = telegraph_;
    applied_mode_ = engine_mode();
    if (total != 0) {
      const std::int32_t s = fx::sign(total);
      for (std::size_t i = 0; i < engines_.size() && total != 0; ++i) {
        EnginePart& part = engines_[i][mode];
        if ((part.flags & EnginePart::kDamaged) == 0 && part.fitted && has_energy()) {
          part.flags |= EnginePart::kRunning;
          for (; fx::abs(part.power_pct) < 95 && total != 0; total -= s) {
            part.power_pct += s;
          }
        }
      }
      for (std::size_t i = 0; i < engines_.size() && total != 0; ++i) {
        EnginePart& part = engines_[i][mode];
        if ((part.flags & EnginePart::kRunning) != 0 && has_energy()) {
          for (; fx::abs(part.power_pct) < 100 && total != 0; total -= s) {
            part.power_pct += s;
          }
        }
      }
    }
    for (std::size_t i = 0; i < engines_.size(); ++i) {
      const EnginePart& part = engines_[i][mode];
      applied_flags_[i] = static_cast<std::uint16_t>(part.flags | (part.fitted ? 0 : 0x400));
    }
  }
  compute_shaft_power();
  if (at_init) {
    motion_.power = motion_.ordered_power;
  }
}

void PlayerSub::compute_shaft_power() {
  // player_compute_shaft_power (0x5d058)
  const auto mode = static_cast<std::size_t>(engine_mode());
  std::int32_t fitted = 0;
  for (const EngineRoom& room : engines_) {
    fitted += room[mode].fitted ? 1 : 0;
  }
  std::int32_t shaft = 0;
  if (fitted != 0) {
    const std::int32_t unit_hp = (diesel_mode_ ? sub_.diesel_hp : sub_.motor_hp) / fitted;
    for (const EngineRoom& room : engines_) {
      if ((room[mode].flags & EnginePart::kRunning) != 0) {
        shaft += room[mode].power_pct * unit_hp / 100;
      }
    }
  }
  motion_.ordered_power = shaft;
}

void PlayerSub::turn_back_at_map_edge(const SimContext& ctx) {
  if (!ctx.env.map) {
    return;
  }
  const MapExtent& map = *ctx.env.map;
  int edges = 0;
  edges |= motion_.position.x < kMapEdgeMargin ? 1 : 0;
  edges |= map.max_x - kMapEdgeMargin < motion_.position.x ? 2 : 0;
  edges |= motion_.position.z < kMapEdgeMargin ? 4 : 0;
  edges |= map.max_z - kMapEdgeMargin < motion_.position.z ? 8 : 0;
  if (edges == 0) {
    return;
  }
  follow_route_ = false;
  // Away from the edge (or corner); impossible combinations keep the course.
  switch (edges) {
    case 1: motion_.ordered_heading = Angle10(900); break;
    case 2: motion_.ordered_heading = Angle10(2700); break;
    case 4: motion_.ordered_heading = Angle10(0); break;
    case 5: motion_.ordered_heading = Angle10(450); break;
    case 6: motion_.ordered_heading = Angle10(3150); break;
    case 8: motion_.ordered_heading = Angle10(1800); break;
    case 9: motion_.ordered_heading = Angle10(1350); break;
    case 10: motion_.ordered_heading = Angle10(2250); break;
    default: break;
  }
  motion_.steering = true;
}

void PlayerSub::follow_waypoints(SimContext& ctx) {
  // player_waypoint_reached (0x5caa2)
  if (route_.count == 0 || route_.count <= route_index_) {
    follow_route_ = false;
  }
  if (!follow_route_ || motion_.speed_tenths == 0) {
    return;
  }
  motion_steer_to_waypoint(motion_, sub_.turn_factor, route_, route_index_);
  if (route_.count == 0 || route_.count <= route_index_) {
    telegraph_ = kTelegraphStop;
    ctx.crew_message(kMsgLastWaypoint);
  }
}

void PlayerSub::update_helm_and_dive(SimContext& ctx, PlayerCombat* combat, Fixed16 dt) {
  update_engines(false);
  turn_back_at_map_edge(ctx);
  follow_waypoints(ctx);

  const std::int32_t air_before = dive_.air_pct;
  const std::int32_t clearance_before = motion_.keel_clearance;
  const std::int32_t depth_before = dive_.depth_ft;
  std::int32_t rudder_cap = kHelmRudderCap;
  const std::int32_t rudder_max = motion_.rudder_max;
  if (system(PlayerSystem::kSteeringGear).impaired()) {
    rudder_cap = (100 - system(PlayerSystem::kSteeringGear).severity) * 200 / 100;
    motion_.rudder_max = rudder_cap;
  }
  if (!motion_.steering) {
    motion_.rudder = helm_degrees().at(static_cast<std::size_t>(helm_ + 7)) * 10;
    if (fx::abs(motion_.rudder) > rudder_cap) {
      motion_.rudder = rudder_cap * fx::sign(motion_.rudder);
    }
  }
  const std::int32_t rudder_before = motion_.rudder;
  PlayerDiveFactors factors;
  factors.damaged_planes =
      (system(PlayerSystem::kBowPlanes).impaired() ? 1 : 0) + (system(PlayerSystem::kSternPlanes).impaired() ? 1 : 0);
  factors.sailing_model = ctx.env.sailing_model();
  sub_dive_update(dive_, sub_, motion_, dt, &factors, ctx.env.sea_state, ctx.floor);
  motion_.rudder_max = rudder_max;

  // The player's clearance is measured from the keel position itself.
  motion_.keel_clearance -= sub_.draft_ft * kUnitsPerFoot;
  if (dive_.depth_ft <= sub_.draft_ft && dive_.planes > 0 && dive_.ordered_depth_ft == -1) {
    dive_.planes = 0;
  }
  if (sub_.test_depth_ft < dive_.depth_ft) {
    ctx.clock.limit_effective_compression(kDeepCompression);
    if (depth_before <= sub_.test_depth_ft && ctx.env.speech) {
      ctx.crew_message(kMsgTestDepth);
      ctx.clock.reset_compression();
    }
  }
  if (motion_.keel_clearance > 0) {
    if ((ctx.env.options & option::kRunAground) == 0) {
      if (clearance_before < 1) {
        ctx.events.ran_aground = true;
      }
      if (motion_.wake_count != 0 && dive_.depth_ft <= sub_.draft_ft) {
        // Put back, stopped, on the oldest wake point.
        motion_.position = {motion_.wake[0].x, sub_.draft_ft * kUnitsPerFoot, motion_.wake[0].z};
        motion_.power = 0;
        motion_.pos_x = static_cast<std::int64_t>(motion_.position.x) << 16;
        motion_.pos_y = static_cast<std::int64_t>(motion_.position.y) << 16;
        motion_.pos_z = static_cast<std::int64_t>(motion_.position.z) << 16;
        motion_.velocity = {};
        telegraph_ = kTelegraphStop;
        motion_.keel_clearance = ctx.floor.height_at(motion_.position.x, motion_.position.z);
      }
    } else if (clearance_before < 1) {
      if (combat != nullptr) {
        combat->damage.apply(ctx, *this, combat->tubes, DamageKind::kGrounding, 0, combat->crew);
      }
      telegraph_ = kTelegraphStop;
    }
    if (ctx.env.screen_shaking || ctx.events.screen_shake || ctx.events.ran_aground) {
      ctx.sfx(kSfxGrinding);
    }
  }

  // The helm step shown follows the rudder.
  std::int32_t rudder_deg = motion_.rudder / 10;
  if (rudder_deg < 0) {
    rudder_deg -= 4;
  }
  std::int32_t steps = 0;
  while (steps < 15 && helm_degrees()[static_cast<std::size_t>(steps)] < rudder_deg) {
    ++steps;
  }
  helm_ = steps - 7;
  if (!motion_.steering && helm_ != announced_helm_) {
    announced_helm_ = helm_;
    if (ctx.env.speech) {
      ctx.events.speech.push_back(helm_speech().at(static_cast<std::size_t>(helm_ + 7)) + ".RAW");
      if (helm_ < 0) {
        ctx.crew_message(kMsgHelmLeft, helm_ * -5);
      } else if (helm_ == 0) {
        ctx.crew_message(kMsgRudderAmidships);
      } else {
        ctx.crew_message(kMsgHelmRight, helm_ * 5);
      }
    }
  } else if (motion_.steering && !follow_route_ && rudder_before / 5 != 0 && motion_.rudder / 5 == 0 &&
             ctx.env.speech) {
    // Steering a course: the rudder just came back amidships.
    ctx.events.speech.push_back("ONCOURSE.RAW");
  }

  for (std::int32_t level = 25, i = 0; i < 2; ++i, level /= 2) {
    if (level < air_before && dive_.air_pct <= level) {
      ctx.crew_message(kMsgAirLow);
      ctx.clock.reset_compression();
      break;
    }
  }
  if (air_before > 0 && dive_.air_pct < 1) {
    ctx.crew_message(kMsgOutOfAir);
  }
  check_fuel(ctx, dt);
  check_batteries(ctx, dt);
  charge_batteries(ctx, dt);
}

void PlayerSub::check_fuel(SimContext& ctx, Fixed16 dt) {
  // player_check_diesel_fuel (0x5d0d6)
  if ((ctx.env.options & option::kUnlimitedFuel) != 0) {
    return;
  }
  const std::int32_t before = fuel_;
  const auto warn = [&] {
    for (std::int32_t level = sub_.fuel_capacity / 10, i = 0; i < 4; ++i, level /= 2) {
      if (level < before && fuel_ <= level) {
        ctx.crew_message(kMsgFuelLow);
        ctx.clock.reset_compression();
        return true;
      }
    }
    return false;
  };
  const auto burn = [&](std::int32_t amount) {
    for (fuel_fraction_ += amount; fuel_fraction_ > 0xffff; fuel_fraction_ -= 0x10000) {
      --fuel_;
    }
  };
  const SystemState& tanks = system(PlayerSystem::kFuelTanks);
  if (tanks.impaired()) {
    burn(fx::mul(tanks.severity * kFuelLeak / 100, dt.raw()));
    if (fuel_ < 1) {
      fuel_ = 0;
      ctx.crew_message(kMsgOutOfFuel);
    } else {
      warn();
    }
  }
  if (!diesel_mode_ || motion_.ordered_power == 0) {
    return;
  }
  if (fuel_ == 0) {
    fuel_fraction_ = 0;
    stop_running_engines();
    return;
  }
  const std::int32_t load = fx::div(fx::imul(low16(fx::abs(motion_.ordered_power)), 0x10000),
                                    fx::imul(sub_.diesel_hp, 0x10000));
  burn(fx::mul(fx::mul(load, sub_.fuel_use.raw()), dt.raw()));
  if (fuel_ < 1) {
    fuel_ = 0;
    ctx.crew_message(kMsgOutOfFuel);
    return;
  }
  warn();
}

void PlayerSub::check_batteries(SimContext& ctx, Fixed16 dt) {
  // player_check_batteries (0x5d338): drain (power / motor hp)^2 x 0.1467
  // units per second.
  if ((ctx.env.options & option::kUnlimitedBattery) != 0 || diesel_mode_ || motion_.ordered_power == 0) {
    return;
  }
  const std::int32_t before = battery_;
  if (battery_ == 0) {
    stop_running_engines();
    return;
  }
  const std::int32_t load =
      fx::div(fx::imul(low16(motion_.ordered_power), 0x10000), fx::imul(sub_.motor_hp, 0x10000));
  battery_fraction_ += fx::mul(fx::mul(fx::mul(fx::imul(load, 100), load), kBatteryDrain), dt.raw());
  while (battery_fraction_ > kBatteryUnit - 1) {
    battery_fraction_ -= kBatteryUnit;
    --battery_;
  }
  if (battery_ < 1) {
    battery_ = 0;
    ctx.crew_message(kMsgBatteriesDead);
    return;
  }
  for (std::int32_t level = 100, i = 0; i < 4; ++i, level /= 2) {
    if (level < before && battery_ <= level) {
      ctx.crew_message(kMsgBatteriesLow);
      ctx.clock.reset_compression();
      return;
    }
  }
}

void PlayerSub::charge_batteries(const SimContext& ctx, Fixed16 dt) {
  // player_charge_batteries (0x5d4b7): idle diesels charge, at a rate that
  // grows with the deficit (at least a tenth of the capacity).
  if ((ctx.env.options & option::kUnlimitedBattery) != 0) {
    return;
  }
  const std::int32_t capacity = battery_capacity();
  if (capacity < battery_) {
    battery_fraction_ = 0;
    battery_ = capacity;
  }
  if (!diesel_mode_ || capacity <= battery_) {
    return;
  }
  std::int32_t deficit = capacity - battery_;
  if (deficit < capacity / 10) {
    deficit = capacity / 10;
  }
  const std::int32_t rate = fx::div(fx::mul(fx::imul(deficit, 0x10000), 0x199a), fx::imul(capacity, 0x10000));
  std::int32_t remaining = rate;
  for (EngineRoom& room : engines_) {
    EnginePart& part = room[1];
    if ((part.flags & EnginePart::kRunning) == 0) {
      if ((part.flags & EnginePart::kDamaged) == 0 && part.fitted) {
        part.flags &= static_cast<std::uint8_t>(~EnginePart::kCharging);
        part.power_pct = 0;
        const std::int32_t share = remaining > kChargePerEngine ? kChargePerEngine : remaining;
        remaining -= share;
        if (share > 0) {
          part.power_pct = share * 100 / kChargePerEngine;
          if (part.power_pct > 0) {
            part.flags |= EnginePart::kCharging;
          }
        }
      }
    } else if (sub_.diesel_count < 2 && 100 - part.power_pct > 0) {
      // A single propelling diesel charges with its spare power.
      part.flags |= EnginePart::kCharging;
      remaining -= (100 - part.power_pct) * kChargePerEngine / 100;
    }
  }
  battery_fraction_ -= fx::mul(fx::imul(rate - remaining, 100), dt.raw());
  while (battery_fraction_ < 0) {
    battery_fraction_ += kBatteryUnit;
    ++battery_;
  }
  if (capacity <= battery_) {
    battery_ = capacity;
    for (EngineRoom& room : engines_) {
      room[1].flags &= static_cast<std::uint8_t>(~EnginePart::kCharging);
    }
  }
}

}  // namespace sh
