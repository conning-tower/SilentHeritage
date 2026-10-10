// The in-mission stations (chart, conn, bridge, periscope...) behind the
// original's 9-slot station interface (vtables 0x102a90..0x102c18), and the
// table of the 11 station objects the current-station pointer `0x11a860`
// selects among.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "ui/event.h"

namespace sh {

// The stations, in the order of their objects (12 bytes each from
// 0x11a7d8); `station_address` gives the value `0x11a860` holds.
enum class StationId : std::uint8_t {
  kChart = 0,    // F5
  kConn = 1,     // F1, the station a mission starts in
  kBridge = 2,   // F4
  kDeckGun = 3,  // Alt-F4
  kGauges = 4,   // F7
  kScope = 5,    // F2
  kRadar = 6,    // F9
  kStatus = 7,   // F8
  kTbt = 8,      // F3, target bearing transmitter
  kTdc = 9,      // F6
  kCabin = 10,   // F10, Alt-F9, Alt-F10: the captain's quarters
};
inline constexpr std::size_t kStationCount = 11;

// Returns the address of the station's object in SH.EXE.
constexpr std::uint32_t station_address(StationId id) { return 0x11a7d8 + 12 * static_cast<std::uint32_t>(id); }

// A station. The mission loop opens it when it becomes current, passes it
// the input events, processes and updates it every frame (the update
// throttled when `expensive` at high time compression), and closes it when
// another station is selected or the mission ends. The four queries default
// to the original's shared stubs (all false).
class Station {
 public:
  virtual ~Station() = default;

  // Slot 0: loads the station's screen and dialogs.
  virtual void open() = 0;
  // Slot +4: one input event (after the in-game menu has seen it).
  virtual void event(const ui::Event& event) = 0;
  // Slot +8: runs the station's dialogs, once per frame and per event.
  virtual void process() = 0;
  // Slot +0xc: redraws; `first_frame()` is set on the first call after open.
  virtual void update() = 0;
  // Slot +0x10: frees the station's screen and dialogs.
  virtual void close() = 0;
  // Slot +0x14: the view is expensive to draw (TBT, radar, scope, deck gun,
  // bridge), so updates are skipped at compressions above 16.
  virtual bool expensive() const { return false; }
  // Slot +0x18: on deck (TBT, deck gun, bridge): the ambient sea sound
  // plays and the crew speech and aircraft volumes change.
  virtual bool outdoors() const { return false; }
  // Slot +0x1c: shows the outside world (TBT, scope, deck gun, bridge); the
  // weather only changes while the current station does not.
  virtual bool shows_world() const { return false; }
  // Slot +0x20: must be reopened when rig for red toggles (TDC, radar,
  // scope, gauges, conn, some cabin pages).
  virtual bool rig_red_redraw() const { return false; }

  // The first-frame flag (+4): set when opened, cleared by the first update.
  bool first_frame() const { return first_frame_; }

 private:
  friend class MissionLoop;
  bool first_frame_ = false;
};

// The station objects and the selection the loop follows: the current
// station `0x11a860` (the conn at mission start) and the reopen flag
// `0x11a8a0`.
class StationRegistry {
 public:
  // Sets the station object of `id`, replacing any previous one.
  void install(StationId id, std::unique_ptr<Station> station) {
    stations_[static_cast<std::size_t>(id)] = std::move(station);
  }
  // Returns the station object of `id`, or nullptr when none is installed.
  Station* get(StationId id) const { return stations_[static_cast<std::size_t>(id)].get(); }

  StationId current() const { return current_; }
  // Makes `id` current; the loop switches at its next frame.
  void select(StationId id) { current_ = id; }
  // Has the loop close and reopen the current station at its next frame
  // (rig for red, cabin page changes).
  void request_reopen() { reopen_ = true; }
  bool reopen_requested() const { return reopen_; }
  void clear_reopen() { reopen_ = false; }

 private:
  std::array<std::unique_ptr<Station>, kStationCount> stations_;
  StationId current_ = StationId::kConn;
  bool reopen_ = false;
};

}  // namespace sh
