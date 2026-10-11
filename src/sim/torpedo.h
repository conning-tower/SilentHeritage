// A running torpedo (class 5, vtable 0x1018b4): the motion model with the
// WEAPONS.DAT run parameters (`torpedo_motion_init/update`), the run depth
// and the Mark 14 faults of the Dud Torpedoes option, and the arming check
// that turns a collision into a hit, a dud or a miss
// (`torpedo_check_arming`).
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "assets/dat_tables.h"
#include "core/fixed.h"
#include "sim/clock.h"
#include "sim/motion.h"
#include "sim/vehicle.h"

namespace sh {

// Bits of the options word used by the weapons.
namespace combat_option {
inline constexpr std::uint32_t kLimitedVisibility = 0x80;
inline constexpr std::uint32_t kUnlimitedAmmo = 0x800;
inline constexpr std::uint32_t kRealisticReloads = 0x2000;
inline constexpr std::uint32_t kDudTorpedoes = 0x4000;
inline constexpr std::uint32_t kInvulnerable = 0x8000;  // "Vulnerable Boat" off.
}  // namespace combat_option

// The WEAPONS.DAT id of the Mark 14, the torpedo with the historical faults.
inline constexpr std::uint32_t kMark14Id = 2;

// A date of the Mark 14 fix table (`0x10e3c0..0x10e408`): month (0-based),
// day, year since 1930. The faults last while the clock is before the
// month *preceding* `month` (the original compares with month - 1).
struct FaultDate {
  std::int32_t month = 0;
  std::int32_t day = 0;
  std::int32_t year = 0;
};
// The Mark 14 fix dates, read from SH.EXE (`game_exe`) on first use.
struct TorpedoFixDates {
  // Deep running (Sep 1942: faults through Jul 1942).
  FaultDate deep_running;
  // Magnetic exploder deactivated, per command (0 ComSubsAsiatic, 1
  // ComSubPac, 2 ComSubSoWesPac, 3 Task Force 42; `0x11b872`).
  std::array<FaultDate, 4> magnetic_exploder_off;
  // Contact exploder fixed (Sep 1943: duds through Jul 1943).
  FaultDate contact_exploder;
};
const TorpedoFixDates& torpedo_fix_dates();

// Returns whether `clock` is still before the fix `date`: before the month
// preceding it, as the original; before its day with
// `rules.fix_obvious_errors` (not in the original).
bool before_fix(const GameClock& clock, const FaultDate& date, const MissionRules& rules = {});

class TorpedoVehicle : public Vehicle {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // A torpedo of type `weapon` (which must outlive it) fired by `launcher`
  // (null when unknown) from `position`, pointing `heading` with the
  // launcher's `pitch` at `speed` (16.16 kn), set to speed `setting`, to
  // turn to `course` and run at `depth_ft`, as fire_tube fills it.
  TorpedoVehicle(const Torpedo& weapon, Vehicle* launcher, const fx::Vec3& position, Angle10 heading, Angle10 pitch,
                 Fixed16 speed, std::int32_t setting, Angle10 course, std::int32_t depth_ft);

  VehicleClass class_id() const override { return VehicleClass::kTorpedo; }
  // `torpedo_init` (0x8571d): the motion block, then with Dud Torpedoes a
  // Mark 14 before its fix dates runs up to 9 ft deeper (rand(10)) and has
  // a 20 % chance of a magnetic premature after rand(60) s past 300 yd.
  void init(SimContext& ctx) override;
  std::int32_t update_pass() const override { return 1; }
  std::int32_t max_time_compression() const override { return 0x20; }
  // Arming check, then the run (`torpedo_update`, 0x8583f).
  void update(SimContext& ctx, Fixed16 dt) override;
  fx::Vec3 position() const override { return motion_.position; }
  Angle10 heading() const override { return motion_.heading; }
  Fixed16 speed() const override { return motion_.speed; }
  bool is_torpedo() const override { return true; }
  const Vehicle* torpedo_launcher() const override { return launcher_; }
  // The first wake point (+0x1ac count, +0x1b0 point): the tube's muzzle.
  std::optional<fx::Vec3> torpedo_launch_point() const override;
  bool is_contact() const override { return true; }
  // Slot +0x80 (`torpedo_mark_sighted`, 0x85e97) and slot +0x88
  // (`torpedo_clear_sighted`, 0x85eac): the crew sees its wake this frame
  // (+0xd4), which shows it on the chart with realistic charts.
  bool sighted() const override { return sighted_; }
  void mark_sighted(const GameClock& /*clock*/) override { sighted_ = true; }
  void refresh_sighted() override { sighted_ = false; }
  // 16 for a torpedo with a wake, 0 for an electric one (`torpedo_visual_height`).
  std::int32_t visual_height() const override { return has_wake() ? 16 : 0; }
  // `torpedo_update_bbox` (0x85c58): no box for the first 20 yards.
  void update_bounds() override;
  // Drops a removed launcher (`torpedo_clear_dead_refs`).
  void forget(const Vehicle& removed) override;

  // Adds a wake point (fire_tube marks the tube's muzzle).
  void add_wake_point(const fx::Vec3& p);

  const Torpedo& weapon() const { return weapon_; }
  Vehicle* launcher() const { return launcher_; }
  const MotionState& motion() const { return motion_; }
  // The run block +0x230.
  std::int32_t depth_ft() const { return depth_ft_; }
  std::int32_t set_depth_ft() const { return set_depth_ft_; }
  std::int32_t speed_setting() const { return setting_; }
  Angle10 course() const { return course_; }
  // Seconds of run left (the range countdown).
  std::int32_t run_left() const { return run_left_; }
  // The magnetic-exploder timer (16.16 s, -1: none).
  std::int32_t premature_timer() const { return premature_timer_; }
  // Yards run so far.
  std::int32_t run_yards() const;
  // Leaves a wake (steam and oxygen torpedoes; not electric ones).
  bool has_wake() const { return weapon_.propulsion != TorpedoPropulsion::kElectric; }

 private:
  // torpedo_check_arming (0x8587b): returns true once the torpedo is spent.
  bool check_arming(SimContext& ctx, std::int32_t dt);
  // torpedo_motion_update (0x8c292)
  void move(SimContext& ctx, std::int32_t dt);
  bool fired_by_player() const;

  const Torpedo& weapon_;           // +0xe8
  Vehicle* launcher_;               // +0x24c
  MotionState motion_;              // +0xec
  std::int32_t depth_ft_ = 0;       // +0x234
  std::int32_t setting_ = 0;        // +0x238
  Angle10 course_;                  // +0x23c
  std::int32_t set_depth_ft_ = 0;   // +0x240
  std::int32_t run_left_ = 0;       // +0x244
  std::int32_t run_fraction_ = 0;   // +0x248
  std::int32_t premature_timer_ = -1;  // +0xd0
  bool sighted_ = false;            // +0xd4
};

}  // namespace sh
