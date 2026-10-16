// The player's crew reporting contacts (`player_report_contacts`, 0x5d713):
// every frame the lookouts check each contact (`player_sighting_check`),
// the sighted ones are marked and listed, and the officer of the deck calls
// the new ones out, at most one call at a time and each kind not again for
// a while: #60 torpedo (5 s), #57 aircraft (5 s, repeat 2 min), #59
// submarine and #56 ship (repeat 5 min), #58 downed pilot (2 min), with the
// radar operator's #50 SJ bearing and #49 SD range in between; each call
// resets the time compression. #68 announces a pilot close enough to rescue.
#pragma once

#include <cstdint>
#include <vector>

#include "core/fixed.h"
#include "sim/context.h"
#include "sim/sensor_environment.h"

namespace sh {

class PlayerSub;
class RadarSets;
class Vehicle;

// What a contact is to the lookouts.
enum class ContactKind : std::int32_t {
  kShip = 0,
  kSubmarine = 1,
  kAircraft = 2,   // Aircraft and bomber groups.
  kPilot = 3,
  kTorpedo = 4,
  kOther = 5,      // Sinking ships, friendly or neutral vessels.
};

// A contact the crew sees this frame.
struct PlayerContact {
  Vehicle* vehicle = nullptr;
  ContactKind kind = ContactKind::kOther;
  Angle10 bearing;            // True.
  Angle10 relative_bearing;   // From the bow.
  std::int32_t range_yd = 0;  // As seen (aircraft: a quarter).
  std::int32_t sighting_range_yd = 0;  // The lookouts' range for it.
};

class ContactReports {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Runs the lookouts for `dt` (16.16 s) and posts the crew calls
  // (`player_report_contacts`); when the crew mans the deck gun, its target
  // becomes the nearest hostile ship or submarine seen
  // (`DeckGun::crew_set_target`).
  void report(SimContext& ctx, PlayerSub& player, RadarSets& radar, const SensorEnvironment& env, std::int32_t dt);

  // Drops the pointers to `removed`, about to be deleted.
  void forget(const Vehicle& removed);

  // The contacts seen in the last report, in vehicle order.
  const std::vector<PlayerContact>& contacts() const { return contacts_; }
  // The lookouts' range for the TDC and deck-gun targets (0x129a24,
  // 0x11c1d4), kept until seen again.
  std::int32_t tdc_target_range_yd() const { return tdc_target_range_; }
  std::int32_t gun_target_range_yd() const { return gun_target_range_; }
  // 0x124ee4: the nearest aircraft seen.
  Vehicle* nearest_aircraft() const { return nearest_aircraft_; }
  // 0x124edc: a downed pilot within 200 yd (rescue possible).
  Vehicle* rescue_pilot() const { return rescue_pilot_; }
  // 0x11ac04: enemies are near (blocks the leave-area command).
  bool enemies_near() const { return enemies_near_; }
  // 0x11a8ac: a ship attacked the boat since its last think.
  bool under_attack() const { return under_attack_; }
  // A sighted torpedo will cross the boat within 30 s (the warning sfx 10).
  bool torpedo_warning() const { return torpedo_warning_; }
  // 0x125f4c: with the contact groups, something of interest is around;
  // in a single mission, the scenario's `special_mission` flag (the end
  // text "Mission over." instead of "Contact with enemy broken.").
  bool group_activity() const { return group_activity_; }
  // Sets 0x125f4c (a single mission's `special_mission` flag).
  void set_group_activity(bool on) { group_activity_ = on; }
  // 0x124eec: the nearest photo-recon objective within 25000 yd, or null.
  Vehicle* recon_target() const { return recon_target_; }
  // 0x124ee8: the recon target when it can be photographed (daylight, in
  // sight and within 2 degrees of the view bearing), else null.
  Vehicle* recon_in_view() const { return recon_in_view_; }
  // `player_take_recon_photo` (0x60f44, the Tab key): photographs the
  // recon target in view: crew message #69, the patrol log entry
  // (`MissionEvents::photo_recons`) and the objective removed. Returns
  // whether a photo was taken.
  bool take_recon_photo(SimContext& ctx);
  // Whether any contact was seen, heard or detected (resets the quiet time).
  bool activity() const { return activity_; }

 private:
  // `player_track_recon_target_in_view` (0x60d2d) for objective `v`.
  void track_recon_target(SimContext& ctx, const PlayerSub& player, const SensorEnvironment& env, Vehicle& v,
                          std::int32_t& nearest_yd);

  std::vector<PlayerContact> contacts_;
  std::int32_t tdc_target_range_ = 0;
  std::int32_t gun_target_range_ = 0;
  Vehicle* nearest_aircraft_ = nullptr;
  Vehicle* rescue_pilot_ = nullptr;
  bool enemies_near_ = false;
  bool under_attack_ = false;
  bool torpedo_warning_ = false;
  bool group_activity_ = false;
  bool activity_ = false;
  Vehicle* recon_target_ = nullptr;   // 0x124eec
  Vehicle* recon_in_view_ = nullptr;  // 0x124ee8
  // Report timers, 16.16 s.
  std::int32_t sd_hold_ = 0;        // 0x124e74, delays the SD range call.
  std::int32_t aircraft_call_ = 0;  // 0x124e78, a call in progress.
  std::int32_t spare_timer_ = 0;    // 0x124e7c, counted down, never set.
  std::int32_t torpedo_call_ = 0;   // 0x124e80
  std::int32_t ship_repeat_ = 0;    // 0x124ebc
  std::int32_t aircraft_repeat_ = 0;  // 0x124ec0
  std::int32_t pilot_repeat_ = 0;   // 0x124ec4
  std::int32_t sub_repeat_ = 0;     // 0x124ec8
};

}  // namespace sh
