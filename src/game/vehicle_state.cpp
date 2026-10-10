#include "game/vehicle_state.h"

#include <array>
#include <map>

#include "assets/dat_tables.h"
#include "core/error.h"
#include "game/class_params.h"
#include "sim/aircraft.h"
#include "sim/aircraft_strike.h"
#include "sim/depth_charge.h"
#include "sim/enemy_sub.h"
#include "sim/shell.h"
#include "sim/ship.h"
#include "sim/state_blocks.h"
#include "sim/torpedo.h"

namespace sh {

namespace {

// The ranges of `veh_write_state` (0x7f026): the box +0x04..+0xaf, the
// collision flag and partner, sinking, removed, side, id, then the
// contact-group index (written apart, kept at `size`).
std::vector<StateRange> common_ranges(std::uint32_t size, std::initializer_list<StateRange> own) {
  std::vector<StateRange> r = {{0x04, 0xac}, {0xb0, 4}, {0xb4, 4}, {0xb8, 4}, {0xbc, 4},
                               {0xc0, 4},    {0xc4, 4}, {size, 4}};
  r.insert(r.end(), own);
  return r;
}

// The class layouts of the `*_write_state` functions (contiguous writes
// merged).
std::map<VehicleClass, VehicleStateLayout> make_layouts() {
  std::map<VehicleClass, VehicleStateLayout> m;
  const auto add = [&m](VehicleClass c, std::uint32_t size, std::initializer_list<StateRange> own) {
    m[c] = VehicleStateLayout{c, size, common_ranges(size, own)};
  };
  // ship_write_state (0x82ffe); +0x47c is written twice.
  add(VehicleClass::kShip, 0x4e0,
      {{0xd0, 4}, {0x1fc, 4}, {0xd8, 0x74}, {0x200, 4}, {0x150, 0x14}, {0x164, 0x38}, {0x19c, 0x48},
       {0x204, 4}, {0x1e8, 0x14}, {0x228, 0x144}, {0x36c, 0x38}, {0x3a4, 0xf4}, {0x47c, 4}, {0x208, 4},
       {0x49c, 0x24}, {0x4c0, 0x19}, {0x4dc, 4}});
  // enemy_sub_write_state (0x806a7)
  add(VehicleClass::kEnemySub, 0xb40,
      {{0xd0, 4}, {0x1cc, 4}, {0xd8, 0x6c}, {0x1d0, 4}, {0x148, 0x7c}, {0x1c8, 4}, {0x1e4, 0x87d}, {0xa64, 0xdc}});
  // aircraft_write_state (0x8511c)
  add(VehicleClass::kAircraft, 0x368,
      {{0xd0, 4}, {0x144, 4}, {0xd8, 0x28}, {0x148, 4}, {0x104, 0x40}, {0x158, 0xb4}, {0x210, 0x19},
       {0x22c, 0xfc}, {0x14c, 4}, {0x32c, 0x28}, {0x150, 4}, {0x358, 0x10}});
  // airgroup_write_state (0x8add8)
  add(VehicleClass::kBomberGroup, 0x1d8,
      {{0xd0, 0x20}, {0x1c0, 0x14}, {0xf0, 0xb8}, {0x1ac, 4}, {0x1b0, 0x10}, {0x1d4, 4}});
  // torpedo_write_state (0x85f9e): the weapon's short name last.
  add(VehicleClass::kTorpedo, 0x250, {{0xd0, 8}, {0xec, 0x144}, {0x230, 0x1c}, {0xd8, 4}, {0x250 + 4, 10}});
  // dc_write_state (0x86a8c)
  add(VehicleClass::kDepthCharge, 0x11c, {{0xd0, 0x4c}});
  // shell_write_state (0x873cc)
  add(VehicleClass::kShell, 0x114, {{0xd0, 4}, {0xd8, 0x38}, {0xd4, 4}});
  // bomb_write_state (0x87f67)
  add(VehicleClass::kBomb, 0x130, {{0xd4, 0x58}, {0xd0, 4}});
  // splash / explosion / uw blast / debris / smoke (0x88396 .. 0x8b914)
  add(VehicleClass::kSplash, 0xf0, {{0xd0, 0x20}});
  add(VehicleClass::kExplosion, 0xe8, {{0xd0, 0x18}});
  add(VehicleClass::kBlastNoise, 0xe0, {{0xd0, 0x10}});
  add(VehicleClass::kDebris, 0xe0, {{0xd0, 0x10}});
  // pilot_write_state (0x89759): the attacker id after the motion block.
  add(VehicleClass::kDownedPilot, 0x270, {{0x120, 0x144}, {0x270 + 4, 4}, {0x264, 0xc}, {0xd4, 0x48}});
  // intel_report_write_state (0x8b678)
  add(VehicleClass::kAirReport, 0x128, {{0xd0, 0x58}});
  add(VehicleClass::kSmoke, 0xe0, {{0xd0, 0x10}});
  return m;
}

const std::map<VehicleClass, VehicleStateLayout>& layouts() {
  static const std::map<VehicleClass, VehicleStateLayout> m = make_layouts();
  return m;
}

// The player crew's sighting record of a ship or enemy submarine.
template <class Io, class S>
void bind_sighting(Io& io, std::uint32_t base, S& s) {
  io.flag(base + 0x00, s.sighted);
  io.i32(base + 0x04, s.last_x);
  io.i32(base + 0x08, s.last_z);
  io.i32(base + 0x0c, s.age);
  io.i32(base + 0x10, s.track_time);
  io.i32(base + 0x14, s.hour);
  io.i32(base + 0x18, s.minute);
  io.i32(base + 0x1c, s.month);
  io.i32(base + 0x20, s.day);
}

template <class Io, class C>
void bind_contact(Io& io, std::uint32_t base, std::uint32_t vehicle_off, C& c) {
  io.i32(base + 0x00, c.age);
  io.vec3(base + 0x04, c.position);
  io.angle(base + 0x10, c.heading);
  io.ref(vehicle_off, c.vehicle);
  io.fixed(base + 0x18, c.speed);
}

// A SUBS.DAT-like record at `base` (the enemy sub's dive block +0x34c).
template <class Io, class P>
void bind_sub_params(Io& io, std::uint32_t base, P& p) {
  io.i32(base + 148, p.length_ft);
  io.i32(base + 152, p.beam_ft);
  io.i32(base + 156, p.draft_ft);
  io.i32(base + 160, p.test_depth_ft);
  io.i32(base + 164, p.periscope_depth_ft);
  io.i32(base + 168, p.radar_depth_ft);
  io.i32(base + 172, p.periscope_height_ft);
  io.i32(base + 176, p.bridge_height_ft);
  io.i32(base + 180, p.awash_depth_ft);
  io.i32(base + 184, p.noise_factor);
  io.i32(base + 208, p.displacement_surfaced);
  io.i32(base + 212, p.ballast_neutral);
  io.i32(base + 216, p.ballast_max);
  io.i32(base + 220, p.diesel_hp);
  io.i32(base + 224, p.motor_hp);
  io.fixed(base + 228, p.drag_cubic_surfaced);
  io.fixed(base + 232, p.drag_cubic_submerged);
  io.fixed(base + 236, p.drag_linear_surfaced);
  io.fixed(base + 240, p.drag_linear_submerged);
  io.fixed(base + 244, p.lateral_drag);
  io.fixed(base + 248, p.astern_drag_factor);
  io.fixed(base + 252, p.turn_factor);
  io.fixed(base + 256, p.pitch_factor);
  io.i32(base + 260, p.diesel_count);
  io.i32(base + 264, p.motor_count);
  io.i32(base + 268, p.fuel_capacity);
  io.fixed(base + 272, p.fuel_use);
  for (std::uint32_t i = 0; i < 11; ++i) {
    io.i32(base + 280 + i * 4, p.power_surfaced[i]);
    io.i32(base + 324 + i * 4, p.power_submerged[i]);
  }
}

// A flight block (+0x00 position, 64-bit position, orientation, inverse,
// heading, pitch, roll and their accumulators, throttle, wanted heading,
// altitude, bank, speed): the aircraft's +0x158, the bomber group's +0xf4.
template <class Io, class F>
void bind_flight(Io& io, std::uint32_t base, F& f) {
  io.vec3(base + 0x00, f.position);
  io.i64(base + 0x0c, f.pos_x);
  io.i64(base + 0x14, f.pos_y);
  io.i64(base + 0x1c, f.pos_z);
  io.mat3(base + 0x30, f.orientation);
  io.mat3(base + 0x54, f.inverse);
  io.angle(base + 0x78, f.heading);
  io.angle(base + 0x7c, f.pitch);
  io.angle(base + 0x80, f.roll);
  io.i64(base + 0x84, f.heading_acc);
  io.i64(base + 0x8c, f.pitch_acc);
  io.i64(base + 0x94, f.roll_acc);
  io.i32(base + 0x9c, f.throttle);
  io.i32(base + 0xa0, f.wanted_heading);
  io.i32(base + 0xa4, f.ordered_altitude_ft);
  io.i32(base + 0xa8, f.bank_target);
  io.fixed(base + 0xac, f.speed);
  io.i32(base + 0xb0, f.speed_tenths);
}

// A name the original saves in a char[25] (SHIPS.DAT, AIRCRAFT.DAT class).
std::string saved_name(const StateImage& image, std::uint32_t offset) { return image.string(offset, 0x19); }

template <class T>
T* require(T* found, std::string_view what, const std::string& name) {
  if (found == nullptr) {
    throw DataError(std::string(what) + ": no class " + name + " (saved vehicle)");
  }
  return found;
}

}  // namespace

const VehicleStateLayout& vehicle_state_layout(VehicleClass cls) {
  const auto it = layouts().find(cls);
  if (it == layouts().end()) {
    throw DataError("state file: no vehicle class " + std::to_string(static_cast<std::int32_t>(cls)));
  }
  return it->second;
}

template <class Io>
void StateIo::bind_common(Io& io, Vehicle& v) {
  // veh_write_state (0x7f026) / veh_read_state (0x7ef31)
  CollisionState& c = v.collision();
  bind_box(io, c.box);
  io.flag(0xb0, c.collided);
  io.ref(0xb4, c.partner);
  bool sinking = v.sinking();
  bool removed = v.removed();
  std::int32_t side = v.side();
  io.flag(0xb8, sinking);
  io.flag(0xbc, removed);
  io.i32(0xc0, side);
  if constexpr (!Io::kReading) {
    // sim_write_state sets every vehicle's +0xc4 to its index first.
    v.set_list_index(io.ids().id(&v));
  }
  std::int32_t index = v.list_index();
  io.i32(0xc4, index);
  if constexpr (Io::kReading) {
    if (sinking) {
      v.set_sinking();
    } else {
      v.clear_sinking();
    }
    if (removed) {
      v.remove();
    }
    v.set_side(side);
    v.set_list_index(index);
  }
}

template <class Io>
void StateIo::bind(Io& io, Ship& v) {
  // ship_write_state (0x82ffe) / ship_read_state (0x82783)
  ShipAi& ai = v.ai_;
  ShipSetup& s = v.setup_;
  bind_common(io, v);
  io.flag(0xd0, v.combat_.hit);
  io.ref(0x1fc, v.combat_.attacker);
  io.flag(0xd8, ai.heard.heard);
  io.angle(0xdc, ai.heard.bearing);
  io.enumeration(0xe0, ai.heard.screw);
  io.flag(0xe4, ai.heard.pinging);
  io.i32(0xf0, ai.heard.report_timer);
  bind_sighting(io, 0xf4, ai.sighting);
  io.i32(0x130, ai.depth_charges);
  io.i32(0x134, ai.charges_dropped);
  bind_contact(io, 0x138, 0x200, ai.contact);
  io.flag(0x158, ai.sonar.active);
  io.i32(0x15c, ai.sonar.beam);
  io.flag(0x160, ai.sonar.echo);
  io.i32(0x164, ai.sonar.chart_beam);
  io.flag(0x168, ai.sonar.chart_echo);
  io.i32(0x16c, ai.last_run_range);
  io.i32(0x170, ai.opening_time);
  io.flag(0x174, ai.turn_left);
  io.i32(0x178, ai.aground_power);
  io.flag(0x17c, ai.circle_for_collision);
  io.enumeration(0x180, ai.resume_mode);
  io.enumeration(0x184, ai.aground_mode);
  io.flag(0x188, ai.aground_steering);
  io.i32(0x18c, ai.aground_rudder);
  io.i32(0x190, ai.evade_power);
  io.i32(0x194, ai.gun_reload);
  io.i32(0x198, ai.gun_reload_aft);
  io.angle(0x19c, ai.gun_elevation);
  io.i32(0x1a0, ai.gun_accuracy);
  io.flag(0x1a4, ai.saved_steering);
  io.i32(0x1a8, ai.saved_rudder);
  io.i32(0x1ac, ai.saved_power);
  io.i32(0x1b0, ai.circle_phase);
  io.enumeration(0x1b4, ai.gun_circle_mode);
  io.i32(0x1b8, ai.timer);
  io.i32(0x1bc, ai.sonar.ping_timer);
  io.i32(0x1c0, ai.timer2);
  io.i32(0x1c4, ai.water_power);
  io.flag(0x1c8, ai.friendly_near_contact);
  io.flag(0x1cc, ai.dropping);
  io.i32(0x1d0, v.alert_.level);
  io.i32(0x1d4, v.alert_.previous);
  io.fixed(0x1d8, v.alert_.timer);
  io.fixed(0x1dc, v.reaction_rolls_.preserved_timer_1dc);
  io.flag(0x1e0, v.reaction_rolls_.fooled_by_debris);
  io.ref(0x204, ai.debris);
  io.i32(0x1e8, v.reaction_rolls_.flee_angle);
  io.i32(0x1ec, ai.think_time);
  io.flag(0x1f0, ai.attacked_player);
  io.i32(0x1f4, ai.sighting_range_yd);
  io.i32(0x1f8, ai.previous_clearance);
  bind_motion(io, 0x228, v.motion_);
  bind_effects(io, 0x36c, v.combat_.effects);
  io.i32(0x3a4, s.hit_points);
  io.enumeration(0x3a8, v.orders_);
  io.enumeration(0x3ac, ai.mode);
  io.i32(0x3b0, s.crew);
  bind_route(io, 0x3b4, 0x418, 0x480, s.route);
  bind_route_index(io, 0x47c, s.route_index);
  io.i32(0x484, s.convoy_group);
  io.i32(0x488, s.zigzag);
  io.i32(0x48c, ai.player_range_yd);
  io.i32(0x494, ai.player_distance_yd);
  if constexpr (Io::kReading) {
    io.ref(0x208, ai.leader);
  } else {
    // The leader only when in a convoy group.
    io.i32(0x208, s.convoy_group != 0 ? io.ids().id(ai.leader) : -1);
  }
  io.i64(0x49c, ai.leader_dx);
  io.i64(0x4a4, ai.leader_dz);
  io.i32(0x4ac, v.reaction_rolls_.breakaways_left);
  io.i32(0x4b0, ai.zigzag.timer);
  io.i32(0x4b4, ai.zigzag.leg);
  io.i32(0x4b8, ai.zigzag.random);
  io.angle(0x4bc, ai.zigzag.base_course);
  if constexpr (!Io::kReading) {
    io.string(0x4c0, 0x19, s.ship_class != nullptr ? s.ship_class->index_name : std::string());
  }
  io.i32(0x4dc, s.damage);
}

template <class Io>
void StateIo::bind(Io& io, EnemySub& v) {
  // enemy_sub_write_state (0x806a7) / enemy_sub_read_state (0x7ffd1)
  EnemySubAi& ai = v.ai_;
  EnemySubSetup& s = v.setup_;
  bind_common(io, v);
  io.flag(0xd0, v.combat_.hit);
  io.ref(0x1cc, v.combat_.attacker);
  io.flag(0xd8, v.heard_);
  io.angle(0xdc, v.heard_bearing_);
  io.i32(0xf0, v.report_timer_);
  bind_sighting(io, 0xf4, v.sighting_);
  bind_contact(io, 0x130, 0x1d0, ai.contact);
  io.i32(0x14c, ai.alert.level);
  io.i32(0x150, ai.alert.previous);
  io.fixed(0x154, ai.alert.timer);
  io.i32(0x158, ai.think_time);
  io.flag(0x15c, ai.attacked_player);
  io.flag(0x160, ai.friendly_near_contact);
  io.i32(0x168, ai.sighting_range_yd);
  io.i32(0x16c, ai.previous_clearance);
  io.i32(0x170, ai.fire_range_yd);
  io.i32(0x174, ai.salvo);
  io.i32(0x178, ai.aground_power);
  io.i32(0x17c, ai.closing_count);
  io.enumeration(0x180, ai.water_mode);
  io.enumeration(0x184, ai.aground_mode);
  io.flag(0x188, ai.aground_steering);
  io.i32(0x18c, ai.aground_rudder);
  io.i32(0x190, ai.evade_power);
  io.i32(0x194, ai.last_range);
  io.i32(0x198, ai.min_contact_y);
  io.flag(0x19c, ai.saved_steering);
  io.i32(0x1a0, ai.saved_rudder);
  io.i32(0x1a4, ai.cruise_power);
  io.i32(0x1a8, ai.timer);
  io.i32(0x1ac, ai.opening_time);
  io.i32(0x1b0, ai.timer2);
  io.i32(0x1b4, ai.periscope_time);
  io.i32(0x1b8, ai.reload_timer);
  io.i32(0x1bc, ai.cooldown);
  io.i32(0x1c0, ai.torpedoes);
  io.i32(0x1c8, ai.speed_setting);
  bind_motion(io, 0x1e4, v.motion_);
  bind_dive(io, 0x328, v.dive_);
  bind_sub_params(io, 0x34c, v.params_);
  bind_effects(io, 0x9f4, v.combat_.effects);
  io.i32(0xa2c, s.hit_points);
  io.enumeration(0xa30, v.orders_);
  io.enumeration(0xa34, ai.mode);
  io.i32(0xa38, s.crew);
  io.i32(0xa3c, v.player_range_yd_);
  io.i32(0xa44, v.player_distance_yd_);
  if constexpr (!Io::kReading) {
    io.string(0xa48, 0x19, s.ship_class != nullptr ? s.ship_class->index_name : std::string());
  }
  bind_route(io, 0xa64, 0xac8, 0xb30, s.route);
  bind_route_index(io, 0xb2c, s.route_index);
  io.i32(0xb34, s.target_x);
  io.i32(0xb38, s.target_z);
  io.i32(0xb3c, s.damage);
}

template <class Io>
void StateIo::bind(Io& io, Aircraft& v) {
  // aircraft_write_state (0x8511c) / aircraft_read_state (0x84c46)
  AircraftAi& ai = v.ai_;
  AircraftSetup& s = v.setup_;
  bind_common(io, v);
  io.ref(0x144, v.attacker_);
  io.i32(0xd8, v.flyby_timer_);
  io.vec3(0xdc, ai.contact);
  io.angle(0xe8, ai.contact_heading);
  io.fixed(0xec, ai.contact_speed);
  io.flag(0xf4, ai.friendly_near_contact);
  io.flag(0xf8, ai.wingman_bombing);
  io.i32(0xfc, ai.last_range);
  io.ref(0x148, ai.debris);
  io.i32(0x104, ai.think_time);
  io.i32(0x108, ai.timer);
  io.i32(0x10c, ai.shadow_time);
  io.i32(0x118, ai.opening_time);
  io.i32(0x11c, ai.drops);
  io.i32(0x120, ai.param);
  io.i32(0x124, ai.shadow_side);
  io.i32(0x128, ai.shadow_bearing);
  io.i32(0x130, ai.counter);
  io.i32(0x134, ai.release_offset);
  io.i32(0x138, v.smoke_timer_);
  io.i32(0x13c, ai.shadow_x);
  io.i32(0x140, ai.shadow_z);
  bind_flight(io, 0x158, v.flight_);
  if constexpr (!Io::kReading) {
    io.string(0x210, 0x19, s.type != nullptr ? s.type->index_name : std::string());
  }
  io.i32(0x22c, s.crew);
  io.i32(0x230, s.bomb_load);
  io.i32(0x234, ai.bombs);
  io.flag(0x238, s.strafes);
  io.i32(0x23c, s.orbit_x);
  io.i32(0x240, s.orbit_z);
  io.i32(0x244, s.orbit_radius);
  io.i32(0x248, ai.sighting_range_yd);
  io.enumeration(0x24c, ai.state);
  io.i32(0x250, ai.hit_points);
  bind_route(io, 0x254, 0x2b8, 0x320, s.route);
  bind_route_index(io, 0x31c, s.route_index);
  io.i32(0x324, s.escort_group);
  if constexpr (Io::kReading) {
    io.ref(0x14c, v.leader_);
  } else {
    io.i32(0x14c, s.escort_group != 0 ? io.ids().id(v.leader_) : -1);
  }
  io.i64(0x32c, v.leader_dx_);
  io.i64(0x334, v.leader_dz_);
  io.flag(0x33c, v.sighted_);
  io.flag(0x340, v.airborne_);
  io.enumeration(0x344, s.mission);
  io.flag(0x348, v.crashing_);
  io.i32(0x350, v.relaunch_timer_);
  io.ref(0x150, ai.target);
  io.i32(0x358, ai.contact_age);
  io.flag(0x35c, ai.in_release_window);
  io.i32(0x364, s.sorties);
}

template <class Io>
void StateIo::bind(Io& io, Smoke& v) {
  bind_common(io, v);
  io.vec3(0xd0, v.position_);
  io.i32(0xdc, v.lifetime_);
}

template <class Io>
void StateIo::bind(Io& io, AirGroup& v) {
  // airgroup_write_state (0x8add8): the flight block has no pitch
  // accumulator here.
  bind_common(io, v);
  io.i32(0xd0, v.start_x_);
  io.i32(0xd4, v.start_z_);
  io.flag(0xd8, v.over_target_);
  io.i32(0xdc, v.target_x_);
  io.i32(0xe0, v.target_z_);
  io.i32(0xe4, v.targets_near_);
  io.u32(0xec, v.seed_);
  for (std::uint32_t i = 0; i < 3; ++i) {
    io.i32(0x1c0 + i * 4, v.timers_[i]);
  }
  io.i32(0x1cc, v.hold_min_);
  io.i32(0x1d0, v.launch_delay_min_);
  io.flag(0xf0, v.strike_done_);
  io.vec3(0xf4, v.position_);
  io.i64(0x100, v.pos_x_);
  io.i64(0x108, v.pos_y_);
  io.i64(0x110, v.pos_z_);
  io.mat3(0x124, v.orientation_);
  io.angle(0x16c, v.heading_);
  io.angle(0x170, v.pitch_);
  io.angle(0x174, v.roll_);
  io.i64(0x178, v.heading_acc_);
  io.i64(0x188, v.roll_acc_);
  io.i32(0x190, v.throttle_);
  io.i32(0x194, v.wanted_heading_);
  io.i32(0x198, v.altitude_ft_);
  io.i32(0x19c, v.turn_);
  io.fixed(0x1a0, v.speed_);
  io.i32(0x1a4, v.speed_tenths_);
  io.ref(0x1ac, v.report_);
  io.i32(0x1b0, v.sighting_range_yd_);
  io.i32(0x1b8, v.base_);
  io.i32(0x1bc, v.planes_);
  io.flag(0x1d4, v.spotted_);
}

template <class Io>
void StateIo::bind(Io& io, IntelReport& v) {
  // intel_report_write_state (0x8b678)
  bind_common(io, v);
  io.enumeration(0xd0, v.kind_);
  io.i32(0xd4, v.target_x_);
  io.i32(0xe0, v.target_y_);
  io.i32(0xec, v.target_z_);
  io.flag(0xf0, v.active_);
  io.flag(0xf4, v.pending_);
  io.i32(0xf8, v.location_type_);
  io.i32(0x100, v.minute_timer_);
  io.i32(0x104, v.base_);
  for (std::uint32_t i = 0; i < 5; ++i) {
    io.i32(0x10c + i * 4, v.eta_[i]);
  }
  io.i32(0x120, v.duration_min_);
  io.i32(0x124, v.delay_min_);
}

template <class Io>
void StateIo::bind(Io& io, DownedPilot& v) {
  // pilot_write_state (0x89759): a motion block at +0x120.
  bind_common(io, v);
  io.vec3(0x120, v.position_);
  io.mat3(0x150, v.orientation_);
  io.mat3(0x174, v.inverse_);
  io.angle(0x198, v.heading_);
  io.ref(0x270 + 4, v.attacker_);
  io.flag(0xd4, v.visible_);
  io.i32(0xd8, v.last_x_);
  io.i32(0xdc, v.last_z_);
  io.i32(0xe0, v.age_);
  io.i32(0xe4, v.track_time_);
  io.flag(0xe8, v.radio_);
  io.i32(0xec, v.reported_x_);
  io.i32(0xf0, v.reported_z_);
  io.i32(0xf4, v.report_hour_);
  io.i32(0xf8, v.report_minute_);
  io.i32(0xfc, v.report_month_);
  io.i32(0x100, v.report_day_);
  io.i32(0x104, v.report_timer_);
  io.i32(0x108, v.report_hours_);
  io.i32(0x10c, v.accuracy_);
  io.i32(0x110, v.sighting_range_yd_);
  io.i32(0x114, v.survival_timer_);
  io.i32(0x118, v.survival_hours_);
}

template <class Io>
void StateIo::bind(Io& io, TorpedoVehicle& v) {
  // torpedo_write_state (0x85f9e): the run block +0x230, the launcher id
  // at +0xd8, then the weapon's short name.
  bind_common(io, v);
  io.i32(0xd0, v.premature_timer_);
  io.flag(0xd4, v.sighted_);
  bind_motion(io, 0xec, v.motion_);
  io.i32(0x234, v.depth_ft_);
  io.i32(0x238, v.setting_);
  io.angle(0x23c, v.course_);
  io.i32(0x240, v.set_depth_ft_);
  io.i32(0x244, v.run_left_);
  io.i32(0x248, v.run_fraction_);
  io.ref(0xd8, v.launcher_);
  if constexpr (!Io::kReading) {
    io.string(0x250 + 4, 10, v.weapon_.short_name);
  }
}

template <class Io>
void StateIo::bind(Io& io, DepthCharge& v) {
  // dc_write_state (0x86a8c)
  bind_common(io, v);
  bind_ballistic(io, 0xd0, v.flight_);
  io.angle(0xf8, v.heading_);
  io.angle(0xfc, v.pitch_);
  io.i64(0x100, v.y16_);
  io.i32(0x108, v.set_depth_ft_);
  io.i32(0x10c, v.depth_ft_);
  io.i32(0x110, v.bottom_ft_);
  io.i32(0x114, v.splash_frame_);
  io.i32(0x118, v.splash_timer_);
}

template <class Io>
void StateIo::bind(Io& io, Shell& v) {
  // shell_write_state (0x873cc): the owner as its id at +0xd4.
  bind_common(io, v);
  io.i32(0xd0, v.lifetime_);
  bind_ballistic(io, 0xd8, v.flight_);
  io.angle(0x100, v.heading_);
  io.angle(0x104, v.elevation_);
  io.i32(0x108, v.speed_fps_);
  io.i32(0x10c, v.caliber_);
  io.ref(0xd4, v.owner_);
}

template <class Io>
void StateIo::bind(Io& io, Bomb& v) {
  // bomb_write_state (0x87f67): the owner as its id at +0xd0.
  bind_common(io, v);
  bind_ballistic(io, 0xd4, v.flight_);
  io.i64(0x104, v.y16_);
  io.i32(0x10c, v.fuse_depth_ft_);
  io.i32(0x110, v.depth_ft_);
  io.i32(0x114, v.bottom_ft_);
  io.i32(0x118, v.splash_frame_);
  io.i32(0x11c, v.splash_timer_);
  io.angle(0x120, v.heading_);
  io.i32(0x124, v.speed_fps_);
  io.ref(0xd0, v.owner_);
}

template <class Io>
void StateIo::bind(Io& io, Splash& v) {
  bind_common(io, v);
  io.i32(0xd0, v.tick_timer_);
  io.i32(0xd4, v.ticks_);
  io.vec3(0xd8, v.position_);
  io.i32(0xe4, v.frame_);
  io.i32(0xe8, v.frame_timer_);
  io.i32(0xec, v.size_);
}

template <class Io>
void StateIo::bind(Io& io, ExplosionFx& v) {
  bind_common(io, v);
  io.vec3(0xd0, v.position_);
  io.i32(0xdc, v.frame_);
  io.i32(0xe0, v.frame_timer_);
  io.enumeration(0xe4, v.kind_);
}

template <class Io>
void StateIo::bind(Io& io, UnderwaterBlast& v) {
  bind_common(io, v);
  io.vec3(0xd0, v.position_);
  io.i32(0xdc, v.lifetime_);
}

template <class Io>
void StateIo::bind(Io& io, Debris& v) {
  // debris_write_state (0x88a4c) / debris_read_state (0x889da)
  bind_common(io, v);
  io.vec3(0xd0, v.position_);
  io.i32(0xdc, v.lifetime_);
}

void StateIo::capture_vehicle(Vehicle& v, StateOut& out) {
  switch (v.class_id()) {
    case VehicleClass::kShip:
      bind(out, static_cast<Ship&>(v));
      return;
    case VehicleClass::kEnemySub:
      bind(out, static_cast<EnemySub&>(v));
      return;
    case VehicleClass::kAircraft:
      bind(out, static_cast<Aircraft&>(v));
      return;
    case VehicleClass::kBomberGroup:
      bind(out, static_cast<AirGroup&>(v));
      return;
    case VehicleClass::kTorpedo:
      bind(out, static_cast<TorpedoVehicle&>(v));
      return;
    case VehicleClass::kDepthCharge:
      bind(out, static_cast<DepthCharge&>(v));
      return;
    case VehicleClass::kShell:
      bind(out, static_cast<Shell&>(v));
      return;
    case VehicleClass::kBomb:
      bind(out, static_cast<Bomb&>(v));
      return;
    case VehicleClass::kSplash:
      bind(out, static_cast<Splash&>(v));
      return;
    case VehicleClass::kExplosion:
      bind(out, static_cast<ExplosionFx&>(v));
      return;
    case VehicleClass::kBlastNoise:
      bind(out, static_cast<UnderwaterBlast&>(v));
      return;
    case VehicleClass::kDebris:
      bind(out, static_cast<Debris&>(v));
      return;
    case VehicleClass::kDownedPilot:
      bind(out, static_cast<DownedPilot&>(v));
      return;
    case VehicleClass::kAirReport:
      bind(out, static_cast<IntelReport&>(v));
      return;
    case VehicleClass::kSmoke:
      bind(out, static_cast<Smoke&>(v));
      return;
    case VehicleClass::kPlayer:
      break;
  }
  throw DataError("state file: vehicle class " + std::to_string(static_cast<std::int32_t>(v.class_id())) +
                  " cannot be saved");
}

std::unique_ptr<Vehicle> StateIo::restore_vehicle(VehicleClass cls, StateIn& in, const VehicleRestoreContext& ctx) {
  // sim_read_state's construct-and-read switch (the class read slots load
  // their classes: load_ships_dat, load_aircraft_dat, weapons_find).
  const StateImage& image = in.image();
  const fx::Vec3 origin;
  switch (cls) {
    case VehicleClass::kShip: {
      const std::string name = saved_name(image, 0x4c0);
      const ShipClass* c = require(ctx.tables.find_ship(name), "SHIPS.DAT", name);
      auto v = std::make_unique<Ship>(ship_motion_params(*c), origin, Angle10(), Fixed16(), ShipOrder::kNone);
      v->setup().ship_class = c;
      v->ai().cls = ShipAiClass::from(*c);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kEnemySub: {
      const std::string name = saved_name(image, 0xa48);
      const ShipClass* c = require(ctx.tables.find_ship(name), "SHIPS.DAT", name);
      auto v = std::make_unique<EnemySub>(origin, Angle10(), Fixed16(), ShipOrder::kNone, 0);
      v->setup_.ship_class = c;
      v->setup_.torpedo = ctx.tables.find_torpedo("TYPE95-1");
      v->hull_ = ship_hull_params(*c);
      v->max_power_ = c->max_power.raw();
      v->submerged_power_ = c->submerged_power.raw();
      bind(in, *v);
      return v;
    }
    case VehicleClass::kAircraft: {
      const std::string name = saved_name(image, 0x210);
      const AircraftType* t = require(ctx.tables.find_aircraft(name), "AIRCRAFT.DAT", name);
      auto v = std::make_unique<Aircraft>();
      v->setup_.type = t;
      v->params_ = AircraftParams::from(*t);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kBomberGroup: {
      auto v = std::make_unique<AirGroup>(ctx.theatre, 0, 0, 0, 0);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kTorpedo: {
      const std::string name = image.string(0x250 + 4, 10);
      const Torpedo* w = require(ctx.tables.find_torpedo(name), "WEAPONS.DAT", name);
      auto v = std::make_unique<TorpedoVehicle>(*w, nullptr, origin, Angle10(), Angle10(), Fixed16(), 0, Angle10(), 0);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kDepthCharge: {
      auto v = std::make_unique<DepthCharge>(origin, Angle10(), Angle10(), 0);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kShell: {
      auto v = std::make_unique<Shell>(origin, Angle10(), Angle10(), 0, 0, nullptr);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kBomb: {
      auto v = std::make_unique<Bomb>(origin, Angle10(), 0, 0, nullptr);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kSplash: {
      auto v = std::make_unique<Splash>(0, 0, false, ctx.frames.spout);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kExplosion: {
      const auto kind = static_cast<ExplosionKind>(image.i32(0xe4));
      const std::int32_t frames = kind == ExplosionKind::kMuzzleFlash ? ctx.frames.gunfire
                                  : kind == ExplosionKind::kShellHit  ? ctx.frames.gunexpl
                                                                      : ctx.frames.expl;
      auto v = std::make_unique<ExplosionFx>(origin, kind, frames);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kBlastNoise: {
      auto v = std::make_unique<UnderwaterBlast>(origin);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kDownedPilot: {
      auto v = std::make_unique<DownedPilot>(origin, Angle10(), ctx.theatre);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kAirReport: {
      auto v = std::make_unique<IntelReport>(ctx.theatre, IntelReport::Kind::kAirStrike, 0);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kSmoke: {
      auto v = std::make_unique<Smoke>(origin);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kDebris: {
      auto v = std::make_unique<Debris>(origin);
      bind(in, *v);
      return v;
    }
    case VehicleClass::kPlayer:
      break;
  }
  throw DataError("state file: vehicle class " + std::to_string(static_cast<std::int32_t>(cls)) +
                  " cannot be read");
}

}  // namespace sh
