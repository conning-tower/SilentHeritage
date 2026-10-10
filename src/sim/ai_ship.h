// The surface ships' AI (`ai_ship_think` 0x11000 and its helpers): what the
// lookouts see, the escorts' sonar search and attack runs (depth charges,
// guns), alerts, convoy formations and zigzags, collision and grounding
// avoidance; and the ship's sensor slots (sighting by the player, noise,
// hydrophones).
//
// The state lives in `ShipAi`, embedded in each `Ship` (offsets are the
// original object's); the scenario fields (crew, route, convoy group,
// zigzag), the alert and the init rolls are the ship's own (ship.h).
//
// Convoys: the leader (orders 3) stores its per-step position delta and
// the members (orders 4) move rigidly with it, copying its orientation and
// speed; the escorts (orders 1) shift their waypoints by it
// (`ship_update_motion`). `convoy_regroup_on_leader` links a group's
// members and escorts to its leader: the mission calls it once every ship
// is spawned (`init_regroup_convoys`), the AI when a leader hands over.
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "core/fixed_math.h"
#include "sim/context.h"
#include "sim/motion.h"

namespace sh {

class Ship;
class Vehicle;

// The SHIPS.DAT fields of the AI and sensors (defaults: a merchant without
// guns, sonar or charges).
struct ShipAiClass {
  ShipType type = ShipType::kSampan;     // +25
  ShipRole role = ShipRole::kMerchant;   // +26
  ScrewSound screw = ScrewSound::kLowSpeed;  // +100
  bool has_sonar = false;                // +132
  std::int32_t depth_charges = 0;        // +120, the load
  std::int32_t rack_charges = 0;         // +124, stern-rack charges per pattern
  std::int32_t thrower_charges = 0;      // +128, side-thrower charges per pattern
  std::int32_t gun_count = 0;            // +136
  std::int32_t length_ft = 0;            // +176
  std::int32_t draft_ft = 0;             // +184
  std::int32_t visual_size = 0;          // +188, the top above the waterline (ft)
  std::int32_t lookout_ft = 0;           // +192, the lookouts' (and gun mounts') height

  // Returns the fields of `c`.
  static ShipAiClass from(const ShipClass& c);
};

// The states of `ai_ship_think` (+0x3ac).
enum class ShipAiMode : std::int32_t {
  kCruise = 0,              // Orders; escorts react to alerts.
  kCloseOnContact = 1,      // Steer to the contact, then hunt.
  kEvadeStart = 2,          // A collision course: hard rudder, stop.
  kEvading = 3,             // Until 30 s clear, then resume.
  kHuntShort = 4,           // Hunt for 2-4 min.
  kHunt = 5,                // Sonar search at 5 kn for (crew + 1) x 20..30 min.
  kSearch = 6,              // Random legs around the datum.
  kAttackStart = 7,         // Sonar contact: speed up (12 kn or contact + 2).
  kAttackRun = 8,           // Run over the contact, drop a pattern.
  kOpening = 9,             // Open out 60-120 s after a pattern.
  kReturn = 10,             // Back to the datum at 5 kn.
  kGunAttack = 11,          // Guns on a surfaced contact.
  kGunApproach = 12,        // Close a surfaced contact at full power.
  kInvestigate = 13,        // To the reported datum at 17 kn.
  kInvestigating = 14,
  kCircleSearch = 15,       // Circle at 5 kn for (crew + 1) x 2..3 min.
  kGunCircleFriendly = 16,  // A friendly ship near the contact: circle it with guns.
  kGunCircleCollision = 17,
  kGunCircle = 18,          // Circle 1000 yd off the contact.
  kAgroundStart = 19,       // Touching bottom: full astern.
  kBackingOff = 20,
  kFindWater = 21,          // Head for the nearest water cell at 1/8 power.
  kToWater = 22,
};

// What a ship's lookouts or sonar hold (+0x138..+0x150).
struct ShipAiContact {
  std::int32_t age = -1;          // +0x138, 16.16 s since the contact; -1 none.
  fx::Vec3 position;              // +0x13c, estimated.
  Angle10 heading;                // +0x148
  Vehicle* vehicle = nullptr;     // +0x14c
  Fixed16 speed;                  // +0x150 (kn)
};

// The player crew's sighting record of the ship (+0xf4..+0x114).
struct ShipSightingRecord {
  bool sighted = false;           // +0xf4
  std::int32_t last_x = -1;       // +0xf8, -1: never seen.
  std::int32_t last_z = 0;        // +0xfc
  std::int32_t age = 0;           // +0x100, 16.16 s since; forgotten after 480 s.
  std::int32_t track_time = 0;    // +0x104, 16.16 s as the selected target.
  std::int32_t hour = 0;          // +0x108..+0x114, when.
  std::int32_t minute = 0;
  std::int32_t month = 0;
  std::int32_t day = 0;
};

// The player's hydrophones on the ship (+0xd8..+0xf0).
struct ShipHeard {
  bool heard = false;             // +0xd8
  Angle10 bearing;                // +0xdc, relative to the boat.
  ScrewSound screw = ScrewSound::kHighSpeed;  // +0xe0
  bool pinging = false;           // +0xe4, its sonar pinged this frame.
  std::int32_t report_timer = 0;  // +0xf0, hundredths of a second.
};

// The escort sonar (+0x158..+0x168, +0x1bc).
struct ShipSonar {
  bool active = false;            // +0x158, pinging.
  std::int32_t beam = 0;          // +0x15c, relative, tenths.
  bool echo = false;              // +0x160
  std::int32_t chart_beam = -1;   // +0x164, drawn on the chart (reset off it).
  bool chart_echo = false;        // +0x168
  std::int32_t ping_timer = 0;    // +0x1bc, 16.16 s.
};

// The leader's zigzag plan (+0x4b0..+0x4bc).
struct ShipZigzag {
  std::int32_t timer = 0;         // +0x4b0, 16.16 s left on the leg.
  std::int32_t leg = 0;           // +0x4b4, 0..3; -1 near a waypoint; -2 after one.
  std::int32_t random = 0;        // +0x4b8, 0..19 rolled per plan.
  Angle10 base_course;            // +0x4bc
};

// The AI state of a ship.
struct ShipAi {
  ShipAiClass cls;
  ShipAiMode mode = ShipAiMode::kCruise;   // +0x3ac
  ShipAiContact contact;
  ShipSightingRecord sighting;
  ShipHeard heard;
  ShipSonar sonar;
  ShipZigzag zigzag;
  std::int32_t depth_charges = 0;          // +0x130, left.
  std::int32_t charges_dropped = 0;        // +0x134
  std::int32_t last_run_range = 0;         // +0x16c, yd on the attack run.
  std::int32_t opening_time = 0;           // +0x170, 16.16 s of opening range.
  bool turn_left = false;                  // +0x174, circle search direction.
  bool circle_for_collision = false;       // +0x17c
  ShipAiMode resume_mode = ShipAiMode::kCruise;   // +0x180
  ShipAiMode aground_mode = ShipAiMode::kCruise;  // +0x184
  ShipAiMode gun_circle_mode = ShipAiMode::kCruise;  // +0x1b4
  std::int32_t aground_power = 0;          // +0x178
  bool aground_steering = false;           // +0x188
  std::int32_t aground_rudder = 0;         // +0x18c
  std::int32_t evade_power = 0;            // +0x190
  std::int32_t gun_reload = 0;             // +0x194, the guns' (weapons code).
  std::int32_t gun_reload_aft = 0;         // +0x198, the aft mount's (weapons code).
  Angle10 gun_elevation;                   // +0x19c, the last shot's (weapons code).
  std::int32_t gun_accuracy = 1000;        // +0x1a0, the guns' spread base (weapons code).
  bool saved_steering = false;             // +0x1a4
  std::int32_t saved_rudder = 0;           // +0x1a8, or the ordered heading (find water).
  std::int32_t saved_power = 0;            // +0x1ac
  std::int32_t circle_phase = 0;           // +0x1b0, -1 .. 2.
  std::int32_t timer = 0;                  // +0x1b8, 16.16 s.
  std::int32_t timer2 = 0;                 // +0x1c0, 16.16 s.
  std::int32_t water_power = 0;            // +0x1c4
  bool friendly_near_contact = false;      // +0x1c8, a non-escort within 300 yd of the contact.
  bool dropping = false;                   // +0x1cc
  Vehicle* debris = nullptr;               // +0x1e4, the last debris seen.
  std::int32_t think_time = 0;             // +0x1ec, 16.16 s gathered.
  bool attacked_player = false;            // +0x1f0
  std::int32_t sighting_range_yd = 0;      // +0x1f4, its range from the player's periscope height.
  std::int32_t previous_clearance = 0;     // +0x1f8, the keel clearance at the last think.
  Ship* leader = nullptr;                  // +0x498
  std::int64_t leader_dx = 0;              // +0x49c, the leader's position delta (48.16).
  std::int64_t leader_dz = 0;              // +0x4a4
  std::int32_t player_range_yd = 0;        // +0x48c, the player's lookout range to it.
  std::int32_t player_distance_yd = -1;    // +0x494, -1 unseen.
};

// The calls the ship AI makes into the weapons; the defaults do nothing.
class ShipWeaponsHook {
 public:
  virtual ~ShipWeaponsHook() = default;
  // Fires the ship's guns at its AI contact (`ai_ship_fire_guns`, 0x12f7a),
  // `dt` the think interval (16.16 s), avoiding the bearings of friendly
  // ships near the contact.
  virtual void fire_guns(Ship& /*ship*/, SimContext& /*ctx*/, std::int32_t /*dt*/,
                         std::span<const Angle10> /*friendly_bearings*/) {}
  // Drops a pattern set to `depth_ft` (`ai_ship_drop_depth_charges`,
  // 0x12dae): decrements `ai().depth_charges` per charge and counts the
  // rack charges in `ai().charges_dropped`.
  virtual void drop_depth_charges(Ship& /*ship*/, SimContext& /*ctx*/, std::int32_t /*depth_ft*/) {}
};

// The calls into the strategic contact groups; the defaults do nothing.
class ContactGroupHook {
 public:
  virtual ~ContactGroupHook() = default;
  // A ship saw the player (`contact_group_mark_engaged`, 0x2e1ee).
  virtual void engaged(Vehicle& /*ship*/) {}
  // The player detected the vehicle (`contact_group_mark_detected`, 0x2e1df).
  virtual void detected(Vehicle& /*vehicle*/) {}
  // `removed` is about to be deleted (the simulation's forget, through
  // `Sensors::forget`).
  virtual void forget(const Vehicle& /*removed*/) {}
};

// The ship part of `ship_init` after its rolls: the AI fields and class
// data (from `setup().ship_class` when set, else `ai().cls` as given), its
// id, then the route pruning: while a point is in water shallower than 1.5
// drafts or its leg to the next crosses land, the *next* point is dropped
// and the check moves on past it, as the original's loop does.
void ai_ship_init(Ship& ship, SimContext& ctx);

// The ship update after the damage state (`ship_update` 0x81170 from
// `ai_ship_think`): the AI, the motion with the convoy formation, the
// effects, the player's sighting record and lookout check.
void ai_ship_update(Ship& ship, SimContext& ctx, Fixed16 dt);

// Runs `ai_ship_think` (0x11000) for `dt` (16.16 s): the ship thinks once a
// second of game time has gathered (only on its frame of the eight at 1x).
void ai_ship_think(Ship& ship, SimContext& ctx, std::int32_t dt);

// Follows the orders (`ai_ship_navigate_orders`, 0x12816): waypoints for
// escorts, patrols and paths; the convoy leader's zigzag legs (table
// 0x10304c); fishing boats' random power every 3-8 minutes.
void ai_ship_navigate_orders(Ship& ship, SimContext& ctx, std::int32_t dt);

// The escort sonar (`ai_ship_sonar_search`, 0x705f4), see docs/structures.md.
void ai_ship_sonar_search(Ship& ship, SimContext& ctx, std::int32_t dt);

// Steers the motion block of `ship` toward (`x`, `z`) seen from the point
// offset by (`offset_x`, `offset_z`) when off its waypoint radius
// (`ai_steer_to_point`, 0x149cf); returns the range in yards.
std::int32_t ai_steer_to_point(Ship& ship, std::int32_t x, std::int32_t z, std::int32_t offset_x = 0,
                               std::int32_t offset_z = 0);
// The same for any motion block with the hull's `turn_factor` (the enemy
// submarines').
std::int32_t ai_steer_to_point(MotionState& m, Fixed16 turn_factor, std::int32_t x, std::int32_t z,
                               std::int32_t offset_x = 0, std::int32_t offset_z = 0);

// Returns the heading toward the first water cell among the four around
// (`x`, `z`) closest to `heading`, else the reciprocal of `heading`
// (`ai_find_water_heading`, 0x14c60). Without a grid: the reciprocal.
Angle10 ai_find_water_heading(SimContext& ctx, std::int32_t x, std::int32_t z, Angle10 heading);

// Links the members and escorts of convoy `group` to its leader, zeroes the
// deltas and the leader's zigzag, and sets the escorts' power for twice the
// leader's speed (`convoy_regroup_on_leader`, 0x8e215).
void convoy_regroup_on_leader(SimContext& ctx, std::int32_t group);

// Returns the leader of convoy `group`, or null (`convoy_find_leader`).
Ship* convoy_find_leader(const std::vector<std::unique_ptr<Vehicle>>& vehicles, std::int32_t group);

// Gives the lead of the ship's convoy to its first member, or to its first
// non-aircraft escort, with the route and zigzag (`ship_hand_over_convoy_lead`,
// 0x825f3); called when the leader sinks.
void ship_hand_over_convoy_lead(Ship& ship, SimContext& ctx);

// Returns the noise level of a ship: 20 + its power as % of the class
// maximum, at most 100; 30 when sinking (`ship_noise_level`, 0x824a0).
std::int32_t ship_noise_level(const Ship& ship);

}  // namespace sh
