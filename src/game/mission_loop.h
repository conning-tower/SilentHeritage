// The per-frame driver of a mission (`mission_main_loop`, 0x1005d): station
// switching, the ambient sea sound, the simulation frame, the event pump
// through the in-game command bar and the station, the throttled station
// update and the game-over check. Its collaborators outside the mission
// (sound, input, the command bar, the GAME OVER box) are injected.
#pragma once

#include <cstdint>
#include <optional>

#include "game/mission.h"
#include "game/station.h"
#include "sim/context.h"
#include "ui/event.h"

namespace sh {

// The ambient sea sound looped while the station is outdoors.
inline constexpr std::int32_t kSfxOutdoorAmbient = 9;

// The sound calls of the loop.
class MissionSound {
 public:
  virtual ~MissionSound() = default;
  // Returns whether effect `sfx` is playing (`sound_sfx_is_playing`).
  virtual bool sfx_playing(std::int32_t sfx) const = 0;
  // Starts effect `sfx` looped (`sound_play_sfx(sfx, 1, -1)`).
  virtual void play_sfx_looped(std::int32_t sfx) = 0;
  // Stops effect `sfx` (`sound_stop_sfx`).
  virtual void stop_sfx(std::int32_t sfx) = 0;
  // Feeds the crew speech stream (`speech_service`), called twice a frame
  // while speech is on.
  virtual void service_speech() = 0;
};

// The input event queue (`event_get` / `event_peek`).
class MissionInput {
 public:
  virtual ~MissionInput() = default;
  // Removes and returns the oldest pending event.
  virtual std::optional<ui::Event> get() = 0;
  // Returns whether an event is pending.
  virtual bool pending() const = 0;
};

// The in-game command bar (dialog 0x11e540), which sees every event before
// the station.
class CommandBar {
 public:
  virtual ~CommandBar() = default;
  virtual void event(const ui::Event& event) = 0;  // ingame_menu_event (0x48d9f)
  virtual void process() = 0;                      // ingame_menu_process (0x48d76)
  virtual void update() = 0;                       // ingame_menu_frame_update (0x48e20)
  virtual void close() = 0;                        // ingame_menu_close (0x48d67), before a station closes
};

// Shows the GAME OVER box with the reason's text and fades to blue
// (`show_game_over`'s presentation).
class GameOverScreen {
 public:
  virtual ~GameOverScreen() = default;
  virtual void show(GameOverReason reason) = 0;
};

// Receives what each simulated frame raised (crew reports, grounding...).
class MissionEventSink {
 public:
  virtual ~MissionEventSink() = default;
  virtual void on_events(const MissionEvents& events) = 0;
};

// The loop's collaborators; all must outlive the loop.
struct MissionLoopHooks {
  MissionSound& sound;
  MissionInput& input;
  CommandBar& command_bar;
  GameOverScreen& game_over;
  MissionEventSink* events = nullptr;
};

// Returns whether the station update runs this frame
// (`mission_station_update_throttled`, 0x102f5): always on its first frame
// or for a cheap view, else above 16x compression only on every
// (compression / 16)th value of the 0..7 frame counter.
bool station_update_due(bool first_frame, bool expensive, std::int32_t frame_counter, std::int32_t compression);

class MissionLoop {
 public:
  // A loop over `mission` showing the stations of `stations` (the current
  // one must be installed whenever a frame runs).
  MissionLoop(Mission& mission, StationRegistry& stations, MissionLoopHooks hooks);

  // Runs one frame that lasted `timer_ticks` 100 Hz ticks. Returns false
  // once the mission has an end code: the station is then closed and the
  // caller ends the mission (`Mission::finish`).
  bool frame(std::int32_t timer_ticks);

  // The station currently open, or nullptr before the first frame.
  Station* open_station() const { return open_; }
  // The 0..7 frame counter `0x11a89c`.
  std::int32_t frame_counter() const { return frame_counter_; }

 private:
  void close_open_station();
  void pump_events();
  void check_game_over();

  Mission& mission_;
  StationRegistry& stations_;
  MissionLoopHooks hooks_;
  Station* open_ = nullptr;
  std::int32_t frame_counter_ = 0;
  // The flags of the last event taken (a local of the original loop, kept
  // across frames).
  std::uint16_t last_event_flags_ = 0;
};

}  // namespace sh
