// The boat's radio messages: the reports the air strikes, downed pilots and
// photo recons radio (`RadioReport`, queued by `radio_queue_push` 0x5e715 on
// the list 0x11b118) wait until the radio can take them, then go to the
// captain's radio log (the list 0x11b11c) stamped with the time, with the
// crew's "Incoming radio message, sir." (`player_incoming_radio`, 0x5e75e).
//
// Usage: build a `RadioRoom` for the mission, register it with
// `Mission::add_frame_task`, and after each frame move `take_delivered()`
// into the cabin's `CabinLogs::radio`.
#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/patrol_zones.h"
#include "game/mission.h"
#include "game/stations/cabin.h"
#include "sim/context.h"

namespace sh {

class ExeImage;
struct World;

// The crew message of a delivery: "Incoming radio message, sir."
inline constexpr std::int32_t kMsgIncomingRadio = 73;

// Returns the queued message of `report` (the 0xa0-byte record its sender
// builds): RMSG.DAT template = the report kind, its arguments, to the boat
// `boat_name`, from the command `from`, and the latitude and longitude of
// the report's world position on `world` (which must have bounds). The
// date fields are left for the delivery.
RadioMessage radio_message(const RadioReport& report, const World& world, const std::string& boat_name,
                           const std::string& from);

// Returns the name of `command` (char*[4] at 0x10eb38: "ComSubsAsiatic",
// "ComSubPac", "ComSubSoWesPac", "Task Force 42").
std::string command_name(const ExeImage& exe, Command command);

// Returns the command whose patrol zone uses the map `world_file` (PZ.DAT
// has one zone per `.WLD`; the original reads the zone record in memory,
// 0x11b872), ComSubsAsiatic when none does.
Command command_of_map(std::span<const PatrolZone> zones, std::string_view world_file);

class RadioRoom : public FrameTask {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // The radio of `mission` (must outlive it), its messages from `from`.
  RadioRoom(Mission& mission, std::string from);

  // After the frame's vehicles: when the radio is available (at radar
  // depth or above, radio not damaged) the waiting messages are delivered
  // with the clock's date and time, the crew reports #73 and the time
  // compression drops to 1x; then the frame's new reports wait (they reach
  // the log in a later frame, as the original's queue fills after the
  // boat's update).
  void tick() override;

  // The messages waiting for the radio (0x11b118).
  const std::vector<RadioMessage>& waiting() const { return waiting_; }
  // Returns the messages delivered since the last call, oldest first.
  std::vector<RadioMessage> take_delivered();

 private:
  Mission& mission_;
  std::string from_;
  std::vector<RadioMessage> waiting_;
  std::vector<RadioMessage> delivered_;
};

}  // namespace sh
