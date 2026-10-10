// The air strikes and their reports: bomber groups (class 4, vtable
// 0x101df0: `airgroup_*`), the air strike / photo-recon reports (class 14,
// `intel_report_*`) and the downed pilots the strikes leave (class 13,
// `pilot_*`).
//
// A bomber group waits its launch delay at a random open map edge, then
// radios an "Air Strike" report with its ETA, flies (200 kn, 2500 ft) to the
// map location nearest its base, bombs near it or near the ships there once
// the boat is within 20 nm, turns home, and for 780 s after the strike
// spawns downed pilots in deep water within 25000 yd. A photo-recon report
// radios its target (a port or industrial location within 4 nm of the
// base, else the base's harbor) after its delay and lasts its duration.
//
// They need the map's bases (`MAP<n>BASE.DAT`, loaded by the career; the
// original reads whatever table is in memory) and landmarks: `AirTheatre`.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "assets/bases.h"
#include "assets/world.h"
#include "core/fixed.h"
#include "sim/vehicle.h"

namespace sh {

// What the air strikes read of the map.
struct AirTheatre {
  std::span<const BaseRecord> bases;      // 0x116240
  std::span<const Location> locations;    // 0x16e58c
  std::int32_t grid_width = 0;            // 0x16e16c, cells
  std::int32_t grid_height = 0;           // 0x16e170
  std::int32_t scale_shift = 0;           // 0x170f24
  // The zone's air edge flags (0x11b896, `PatrolZone::air_start_edges`,
  // by `MapEdge`): a bomber group starts at an edge whose flag is non-zero;
  // at least one must be.
  std::array<std::int32_t, 4> air_edges{1, 1, 1, 1};
  std::int32_t sea_picture = 0;           // 0x105150, 2: heavy seas halve a pilot's survival.

  std::int32_t cell_size() const { return 0x10000 >> scale_shift; }
  // Returns base `index`; throws DataError when out of range.
  const BaseRecord& base(std::int32_t index) const;
};

// An air strike or photo-recon report (class 14).
class IntelReport : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  enum class Kind : std::int32_t {
    kAirStrike = 0,   // Made by a bomber group, which radios it.
    kPhotoRecon = -1, // A recon target to photograph.
  };

  // A report on base `base` of `theatre` (must outlive it); a photo recon
  // radios after `delay_min` and lasts `duration_min`
  // (`scen_spawn_photo_recon`, 0x5442a).
  IntelReport(const AirTheatre& theatre, Kind kind, std::int32_t base, std::int32_t delay_min = 0,
              std::int32_t duration_min = 0);

  VehicleClass class_id() const override { return VehicleClass::kAirReport; }
  // `intel_report_init` (0x8afff): a recon picks its target (one draw for
  // the harbor-or-location choice, one per pick).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  // `intel_report_update` (0x8b277).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return {target_x_, target_y_, target_z_}; }
  std::int32_t visual_height() const override { return 0; }
  // Slot +0x78 (0x196b8): a photo recon is -1 until radioed, then 1.
  std::int32_t recon_objective() const override {
    if (kind_ == Kind::kAirStrike) {
      return 0;
    }
    return pending_ ? -1 : 1;
  }

  Kind kind() const { return kind_; }
  std::int32_t base() const { return base_; }
  // The target's location type, -1 for the base's harbor.
  std::int32_t location_type() const { return location_type_; }
  // Shown on the chart: the radio could take it (+0xf0).
  bool active() const { return active_; }
  // Its radio message is still to go (+0xf4).
  bool pending() const { return pending_; }
  // The ETA (hour, minute, day, month, year) set when the report went out.
  std::array<std::int32_t, 5> eta() const { return eta_; }
  std::int32_t duration_min() const { return duration_min_; }
  std::int32_t delay_min() const { return delay_min_; }
  // Sets the ETA (the bomber group's report).
  void set_eta(const std::array<std::int32_t, 5>& eta) { eta_ = eta; }

 private:
  const AirTheatre& theatre_;
  Kind kind_;                        // +0xd0
  std::int32_t base_;                // +0x104
  std::int32_t delay_min_;           // +0x124
  std::int32_t duration_min_;        // +0x120
  std::int32_t target_x_ = 0;        // +0xd4
  std::int32_t target_y_ = 0;        // +0xe0
  std::int32_t target_z_ = 0;        // +0xec
  std::int32_t location_type_ = 0;   // +0xf8
  bool active_ = false;              // +0xf0
  bool pending_ = false;             // +0xf4, the message is still to go.
  std::int32_t minute_timer_ = 0;    // +0x100
  std::array<std::int32_t, 5> eta_{};  // +0x10c..+0x11c
};

// A downed pilot on his raft (class 13).
class DownedPilot : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  DownedPilot(const fx::Vec3& position, Angle10 heading, const AirTheatre& theatre);

  VehicleClass class_id() const override { return VehicleClass::kDownedPilot; }
  // `pilot_init` (0x88ad2): radios a position off by up to 1600 yd (in
  // water deeper than the boat's draft) and rolls the survival hours.
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 10; }
  std::int32_t max_time_compression() const override { return sinking() ? 0x800 : 0x200; }
  // `pilot_update` (0x89189): survival and sighting countdowns.
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }
  // `pilot_update_bounds` (0x88f8b): 6 x 10 x 7 ft.
  void update_bounds() override;
  // `pilot_take_damage`: any hit kills.
  bool take_damage(SimContext& ctx, Vehicle* attacker, DamageKind kind, std::int32_t distance,
                   std::int32_t amount) override;
  void forget(const Vehicle& removed) override;

  Angle10 heading() const override { return heading_; }
  std::int32_t comms_range_yd() const override { return sighting_range_yd_; }
  bool is_pilot() const override { return true; }
  bool is_contact() const override { return true; }
  bool sighted() const override { return visible_; }
  void mark_sighted(const GameClock& clock) override;
  std::int32_t add_track_time(std::int32_t dt, bool selected) override;
  void refresh_sighted() override { visible_ = age_ < 0x10000 && last_x_ >= 0; }

  // The radioed position (+0xec/+0xf0).
  std::int32_t reported_x() const { return reported_x_; }
  std::int32_t reported_z() const { return reported_z_; }
  // When the position went out (+0xf4 hour, +0xf8 minute, +0xfc month
  // 0-based, +0x100 day).
  std::int32_t report_hour() const { return report_hour_; }
  std::int32_t report_minute() const { return report_minute_; }
  std::int32_t report_month() const { return report_month_; }
  std::int32_t report_day() const { return report_day_; }
  // Hours he survives (+0x118).
  std::int32_t survival_hours() const { return survival_hours_; }
  // The radio heard of him (+0xe8), and the hours the report stays on
  // the chart (+0x108, from 36).
  bool radioed() const { return radio_; }
  std::int32_t report_hours() const { return report_hours_; }
  // Where he was last seen (+0xd8/+0xdc), x -1 never.
  std::int32_t last_seen_x() const { return last_x_; }
  std::int32_t last_seen_z() const { return last_z_; }

 private:
  const AirTheatre& theatre_;
  fx::Vec3 position_;              // +0x120
  Angle10 heading_;                // +0x198
  fx::Mat3 orientation_;           // +0x150
  fx::Mat3 inverse_;               // +0x174
  Vehicle* attacker_ = nullptr;    // +0xd0
  bool visible_ = false;           // +0xd4
  std::int32_t last_x_ = -1;       // +0xd8
  std::int32_t last_z_ = 0;        // +0xdc
  std::int32_t age_ = 0;           // +0xe0
  std::int32_t track_time_ = 0;    // +0xe4
  bool radio_ = false;             // +0xe8
  std::int32_t reported_x_ = 0;    // +0xec
  std::int32_t reported_z_ = 0;    // +0xf0
  std::int32_t report_hour_ = 0;   // +0xf4
  std::int32_t report_minute_ = 0; // +0xf8
  std::int32_t report_month_ = 0;  // +0xfc
  std::int32_t report_day_ = 0;    // +0x100
  std::int32_t report_timer_ = 0;  // +0x104
  std::int32_t report_hours_ = 36; // +0x108
  std::int32_t accuracy_ = 0;      // +0x10c, rand(100)
  std::int32_t sighting_range_yd_ = 0;  // +0x110
  std::int32_t survival_timer_ = 0;     // +0x114
  std::int32_t survival_hours_ = 0;     // +0x118
};

// A bomber group (class 4).
class AirGroup : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // `planes` bombers from base `base` of `theatre` (must outlive it),
  // launching after `launch_delay_min` and holding `hold_min` before
  // heading out (`scen_spawn_bomber_group`, which rolls the plane count).
  AirGroup(const AirTheatre& theatre, std::int32_t base, std::int32_t planes, std::int32_t launch_delay_min,
           std::int32_t hold_min);

  VehicleClass class_id() const override { return VehicleClass::kBomberGroup; }
  // `airgroup_init` (0x899f8): its target, an open edge to start from
  // (rand(4) until open, then rand of the edge length) and the formation
  // seed (rand()).
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 6; }
  std::int32_t max_time_compression() const override { return hold_min_ != 0 ? 0x800 : 0x200; }
  // `airgroup_update` (0x89efa).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return position_; }
  void forget(const Vehicle& removed) override;

  Angle10 heading() const override { return heading_; }
  Fixed16 speed() const override { return speed_; }
  std::int32_t comms_range_yd() const override { return sighting_range_yd_; }
  bool is_radar_target() const override { return true; }
  // `airgroup_radar_cross_section`: 8000..16000 by the player's aspect.
  std::int32_t radar_cross_section(const fx::Vec3& observer, Angle10 observer_heading) const override;
  std::int32_t noise_level() const override { return 0; }
  bool is_contact() const override { return true; }
  bool sighted() const override { return spotted_; }
  void mark_sighted(const GameClock& /*clock*/) override { spotted_ = true; }
  void refresh_sighted() override { spotted_ = false; }
  std::int32_t visual_height() const override { return position_.y; }
  std::int32_t plane_count() const override { return planes_; }

  std::int32_t target_x() const { return target_x_; }
  std::int32_t target_z() const { return target_z_; }
  std::int32_t start_x() const { return start_x_; }
  std::int32_t start_z() const { return start_z_; }
  bool over_target() const { return over_target_; }
  bool strike_done() const { return strike_done_; }
  std::int32_t launch_delay_min() const { return launch_delay_min_; }
  std::uint32_t formation_seed() const { return seed_; }
  const IntelReport* report() const { return report_; }
  // Whether its "Air Strike" report was created: the original's non-null
  // +0x1a8, which stays set (dangling) after the report is removed.
  bool report_created() const { return report_created_; }

 private:
  void fly(std::int32_t dt);  // airgroup_fly (0x8a613)

  const AirTheatre& theatre_;
  std::int32_t base_;                // +0x1b8
  std::int32_t planes_;              // +0x1bc
  std::int32_t launch_delay_min_;    // +0x1d0
  std::int32_t hold_min_;            // +0x1cc
  std::int32_t start_x_ = 0;         // +0xd0
  std::int32_t start_z_ = 0;         // +0xd4
  bool over_target_ = false;         // +0xd8
  std::int32_t target_x_ = 0;        // +0xdc
  std::int32_t target_z_ = 0;        // +0xe0
  std::int32_t targets_near_ = 0;    // +0xe4: the ship count, or the target x when none.
  std::uint32_t seed_ = 0;           // +0xec
  bool strike_done_ = false;         // +0xf0
  fx::Vec3 position_;                // +0xf4
  std::int64_t pos_x_ = 0;           // +0x100..
  std::int64_t pos_y_ = 0;
  std::int64_t pos_z_ = 0;
  fx::Mat3 orientation_;             // +0x124
  Angle10 heading_;                  // +0x16c
  Angle10 pitch_;                    // +0x170
  Angle10 roll_;                     // +0x174
  std::int64_t heading_acc_ = 0;     // +0x178
  std::int64_t roll_acc_ = 0;        // +0x188
  std::int32_t throttle_ = 100;      // +0x190
  std::int32_t wanted_heading_ = -1; // +0x194
  std::int32_t altitude_ft_ = 2500;  // +0x198
  std::int32_t turn_ = 0;            // +0x19c
  Fixed16 speed_;                    // +0x1a0
  std::int32_t speed_tenths_ = 0;    // +0x1a4
  IntelReport* report_ = nullptr;    // +0x1a8
  bool report_created_ = false;      // +0x1a8 != 0 (the state extension)
  std::int32_t sighting_range_yd_ = 0;  // +0x1b0
  std::array<std::int32_t, 3> timers_{0x3c0000, 0, 0};  // +0x1c0 bombs/minutes, +0x1c4 pilots, +0x1c8 after the strike
  bool spotted_ = false;             // +0x1d4
};

}  // namespace sh
