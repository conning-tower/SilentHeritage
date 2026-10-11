// The base of every simulated object, mirroring the original's 59-slot
// vehicle vtable (docs/structures.md "Vehicles"). Only the slots the simulation core
// drives are declared here; sensors, weapons, damage and save slots are
// added with their subsystems.
#pragma once

#include <cstdint>
#include <optional>

#include "core/fixed.h"
#include "core/fixed_math.h"
#include "sim/collision.h"
#include "sim/context.h"

namespace sh {

// Class ids returned by vtable slot 0 (and saved with the vehicle).
enum class VehicleClass : std::int32_t {
  kPlayer = 0,
  kShip = 1,
  kEnemySub = 2,
  kAircraft = 3,
  kBomberGroup = 4,
  kTorpedo = 5,
  kDepthCharge = 6,
  kShell = 7,
  kBomb = 8,
  kSplash = 9,
  kExplosion = 10,
  kBlastNoise = 11,
  kDebris = 12,
  kDownedPilot = 13,
  kAirReport = 14,
  kSmoke = 15,
};

// The `kind` argument of slot +0x50 (take damage).
enum class DamageKind : std::int32_t {
  kShellfire = 0,    // Gun shells.
  kTorpedo = 1,
  kDepthCharge = 2,
  kRam = 3,
  kGrounding = 4,
  kGunfire = 5,      // Machine-gun and AA rounds (strafing).
  kBomb = 6,
};

class Vehicle {
 public:
  virtual ~Vehicle() = default;

  // Slot +0x00.
  virtual VehicleClass class_id() const = 0;
  // Slot +0x04: called once the spawner has filled the fields.
  virtual void init(SimContext& ctx) = 0;
  // Slot +0x08: the update pass (0..10) the vehicle runs in; passes run in
  // order with sub-steps of 0.02 s (pass 0) or (pass + 1) x 0.1 s.
  virtual std::int32_t update_pass() const = 0;
  // Slot +0x0c: the highest time compression this vehicle allows.
  virtual std::int32_t max_time_compression() const { return kUnrestrictedCompression; }
  // Slot +0x10: advances the vehicle by `dt` (16.16 s, one sub-step).
  virtual void update(SimContext& ctx, Fixed16 dt) = 0;
  // Slot +0x18: the position in world units.
  virtual fx::Vec3 position() const = 0;
  // Slot +0x30: rebuilds the collision box after a move (default: no box,
  // never collides).
  virtual void update_bounds() {}
  // Slot +0x50: damage by `attacker` (may be null) of `kind`, at `distance`
  // (feet, for blasts) and of `amount`; returns whether it was taken (a
  // shell passes through what does not take it). Default: ignored.
  virtual bool take_damage(SimContext& /*ctx*/, Vehicle* /*attacker*/, DamageKind /*kind*/,
                           std::int32_t /*distance*/, std::int32_t /*amount*/) {
    return false;
  }
  // Slot +0xc8: told of a noise or sighting at `position` (an explosion,
  // a gun firing) by `ai_broadcast_contact`; `target` is the vehicle seen,
  // if any. Default: ignored.
  virtual void receive_contact(SimContext& /*ctx*/, const fx::Vec3& /*position*/, Vehicle* /*target*/) {}
  // Slot +0xe8: drops the pointers it holds to `removed`, which is about to
  // be deleted. Default: holds none.
  virtual void forget(const Vehicle& /*removed*/) {}

  // Sensor and AI slots (sim/sensors.h, sim/ai_ship.h); the defaults are the
  // original's `veh_default_*` stubs.
  // Slot +0x1c: the heading.
  virtual Angle10 heading() const { return Angle10(0); }
  // Slot +0x20: the speed (16.16 kn).
  virtual Fixed16 speed() const { return Fixed16(); }
  // Slot +0x34: the range (yd) within which the player's detection marks
  // the vehicle's contact group (ship: its sighting range +0x1f4).
  virtual std::int32_t comms_range_yd() const { return 0; }
  // Slot +0x2c: a hull hostile to the player of side `player_side`
  // (`0x11a9b0`), listed by the mission results while afloat.
  virtual bool is_hostile(std::int32_t /*player_side*/) const { return false; }
  // Slot +0x58: the player's hydrophones listening to it, once per frame.
  virtual void hydrophone_check(SimContext& /*ctx*/) {}
  // Slot +0x64: whether the SJ/SD radars can see it.
  virtual bool is_radar_target() const { return false; }
  // Slot +0x68: the radar cross-section (16.16 raw) seen from the player at
  // `observer` heading `observer_heading`.
  virtual std::int32_t radar_cross_section(const fx::Vec3& /*observer*/, Angle10 /*observer_heading*/) const {
    return 0;
  }
  // Slot +0x6c: the noise level in % for the sonars, -1 when silent.
  virtual std::int32_t noise_level() const { return -1; }
  // Slot +0x78: a photo-recon objective (only class 14 has one): 0 none,
  // -1 not radioed yet, 1 to be photographed. Non-zero keeps the mission's
  // contact time running; positive ones are tracked for the camera.
  virtual std::int32_t recon_objective() const { return 0; }
  // Slot +0x74: a downed pilot.
  virtual bool is_pilot() const { return false; }
  // Slot +0x7c: shown as a contact (lookouts, AI observers).
  virtual bool is_contact() const { return false; }
  // Slot +0x80 (query): whether the player's crew sees it.
  virtual bool sighted() const { return false; }
  // Slot +0x80 (set): the crew sees it now (records time and position).
  virtual void mark_sighted(const GameClock& /*clock*/) {}
  // Slot +0x84: adds `dt` (16.16 s, >= 0) of tracking when `selected` is
  // set (the TDC or deck-gun target); returns the tracking time (a negative
  // `dt` only queries it).
  virtual std::int32_t add_track_time(std::int32_t /*dt*/, bool /*selected*/) { return 0; }
  // Slot +0x84 queried (a negative `dt`): the tracking time (16.16 s).
  virtual std::int32_t track_time() const { return 0; }
  // Slot +0x88: the per-frame refresh of the sighted flag.
  virtual void refresh_sighted() {}
  // Slot +0x8c: the world y of its top, what lookouts look for (default:
  // the collision box's top, corner 2).
  virtual std::int32_t visual_height() const { return collision_.box.corners[2].y; }
  // Slot +0x54: whether the TDC and the deck gun may target it (ships and
  // enemy submarines whose top is above the water).
  virtual bool is_surface_target() const { return false; }
  // Slot +0x90: the divisor of the player's sighting range (1 surfaced,
  // 2 below the waterline, 5 deep).
  virtual std::int32_t exposure() const { return 1; }
  // Slot +0x9c.
  virtual bool is_torpedo() const { return false; }
  // Slot +0xa0.
  virtual bool is_submarine() const { return false; }
  // Slot +0xa4: an escort class (SHIPS.DAT role 1).
  virtual bool is_escort() const { return false; }
  // Slot +0xa8: dropping depth charges on its current run (+0x1cc).
  virtual bool dropping_charges() const { return false; }
  // Slot +0xac: its convoy group id when it is the leader (`members` false)
  // or a member (`members` true) of one, else 0.
  virtual std::int32_t convoy_group_by_role(bool /*members*/) const { return 0; }
  // Slot +0xb0: the convoy group it escorts, else 0.
  virtual std::int32_t escorted_group() const { return 0; }
  // Slot +0xb8: a small craft (the radar range is quartered).
  virtual bool is_small_craft() const { return false; }
  // Slot +0xc0: it has attacked the player since its last AI think.
  virtual bool has_attacked_player() const { return false; }
  // Slot +0xc4: jettisoned debris (class 12).
  virtual bool is_debris() const { return false; }
  // Slot +0xcc.
  virtual bool is_aircraft() const { return false; }
  // Slot +0xd0: the planes of a bomber group, else 0.
  virtual std::int32_t plane_count() const { return 0; }
  // Slot +0xd4: its noise masks the escorts' sonar.
  virtual bool masks_sonar() const { return false; }
  // A torpedo's launcher (+0x24c), and the point it was fired from when
  // known (+0x1ac/+0x1b0); null and empty for other classes.
  virtual const Vehicle* torpedo_launcher() const { return nullptr; }
  virtual std::optional<fx::Vec3> torpedo_launch_point() const { return std::nullopt; }

  // +0xc0: the side; AI observers ignore their own side and side 2.
  std::int32_t side() const { return side_; }
  void set_side(std::int32_t side) { side_ = side; }

  // +0x04..+0xb4: the collision box, flag and partner.
  CollisionState& collision() { return collision_; }
  const CollisionState& collision() const { return collision_; }

  // +0xb8: sinking (or destroyed).
  bool sinking() const { return sinking_; }
  void set_sinking() { sinking_ = true; }
  // Clears it: an aircraft taking off again (`air_sorties_relaunch`).
  void clear_sinking() { sinking_ = false; }
  // +0xbc: to be deleted by the simulation at the end of the frame.
  bool removed() const { return removed_; }
  void remove() { removed_ = true; }
  // +0xc4: its index in the simulation's list, set before every update
  // (the AI staggers its thinking by it at 1x).
  std::int32_t list_index() const { return list_index_; }
  void set_list_index(std::int32_t index) { list_index_ = index; }

 private:
  std::int32_t side_ = 0;
  std::int32_t list_index_ = 0;
  CollisionState collision_;
  bool sinking_ = false;
  bool removed_ = false;
};

}  // namespace sh
