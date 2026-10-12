// What the front end needs from the program running it: the display, the
// input, the clock, the sound, and the mission layer. The application
// implements it with SDL (app/game_app.h); the tests with scripted input.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

#include "assets/pal.h"
#include "core/error.h"
#include "core/image.h"
#include "core/side_fill.h"

namespace sh {

namespace ui {
class Gui;
struct Rect;
}

struct SmackerAudioFormat;
struct GameOptions;
struct MissionRequest;
struct MissionOutcome;
struct PatrolRequest;
struct PatrolResult;

// Thrown to leave the game at once: the player closed the window, or
// confirmed EXIT TO DOS? (where the original calls exit(0)).
class QuitRequested : public std::runtime_error {
 public:
  QuitRequested() : std::runtime_error("quit requested") {}
};

// The sound track of a video being played.
class VideoSound {
 public:
  virtual ~VideoSound() = default;
  // Starts a stream in `format`, dropping any previous one.
  virtual void start(const SmackerAudioFormat& format) = 0;
  // Queues PCM bytes of that format.
  virtual void queue(std::span<const std::uint8_t> pcm) = 0;
  // Stops and drops the stream.
  virtual void stop() = 0;
};

class FrontendHost {
 public:
  virtual ~FrontendHost() = default;

  // Returns the 100 Hz GUI tick count (`gui_get_ticks`; also the C
  // library `clock()` the screens wait on).
  virtual std::uint32_t ticks() = 0;
  // Shows `screen` (640x480) with the DAC palette `dac` (6-bit values).
  virtual void present(const Image8& screen, const VgaPalette& dac) = 0;
  // Feeds the input received since the last call into `gui` (keys through
  // `Mouse::post_key`, the pointer through `Mouse::update`). Throws
  // `QuitRequested` when the player closes the window.
  virtual void poll_input(ui::Gui& gui) = 0;
  // Returns the output of the videos' sound, or nullptr without sound.
  virtual VideoSound* video_sound() = 0;
  // `sound_play_sfx` (0xb2bbe): plays effect `sample` at `volume` 0..100
  // (-1 default) and `pan` (-1 centre).
  virtual void play_sfx(std::int32_t sample, std::int32_t volume, std::int32_t pan) = 0;
  // `speech_open` (0xb30a9): plays the speech clip SPEECH\<file_name> (e.g.
  // "GALLEY.RAW") in place of the one playing; returns false when speech
  // is unavailable or the file missing. None by default.
  virtual bool play_speech(std::string_view /*file_name*/) { return false; }
  // `speech_stop` (0xb3125): stops the speech clip.
  virtual void stop_speech() {}
  // `sound_apply_options` (0x508d2) with the game options.
  virtual void apply_sound_options(const GameOptions& options) = 0;
  // Tells the player about a part of the game not reimplemented yet.
  virtual void report(std::string_view message) = 0;
  // Not in the original: whether the touch command panel exists, and opens
  // it above `button` (screen coordinates), for the command bar's ORDERS
  // button (`Features::command_bar`). None by default.
  virtual bool has_orders_panel() const { return false; }
  virtual void open_orders_panel(const ui::Rect& /*button*/) {}
  // Returns the width the wide-screen layouts draw at (480 rows; not in
  // the original): the window's, 640 by default.
  virtual int screen_width() { return kScreenWidth; }
  // Sets what fills a wide screen beside the 640-wide frames presented
  // from now on (not in the original); nothing by default.
  virtual void set_side_fill(const SideFill& /*fill*/) {}
  // Sets the picture shown across the window's bottom rows over the frames
  // presented from now on, from its left edge (the command bar across a
  // wide window; not in the original); null for none, the default. It must
  // stay valid while set.
  virtual void set_bottom_strip(const Image8* /*strip*/) {}
};

// The mission layer, called where the original calls `mission_main_loop`
// (from `run_mission`, 0xa1b10).
class MissionRunner {
 public:
  virtual ~MissionRunner() = default;
  // Plays the mission of `request` and returns its end code (`0x11a890`).
  virtual std::int32_t run(const MissionRequest& request) = 0;
  // Returns what `mission_compute_results` reads of the mission `run`
  // played last (game/frontend_results.h).
  virtual MissionOutcome outcome() const = 0;
  // Plays the career patrol of `request` (game/patrol.h: `run_mission`
  // with the patrol's mission) and returns its end code, with what the
  // career reads of it in `result`. Throws `DataError` in a runner without
  // career patrols.
  virtual std::int32_t run_patrol(const PatrolRequest& /*request*/, PatrolResult& /*result*/) {
    throw DataError("career patrols are not supported by this mission runner");
  }
};

// How the scenario editor ended: SE.EXE's exit codes.
enum class EditorExit : std::int32_t {
  kExit = 0,       // File / Exit: back to the menu
  kRun = 1,        // File / Run: CUSTOM\AEONSCEN.TMP is to be played
  kQuitToDos = 2,  // QUIT TO DOS?: the game ends too
};

// The scenario editor, called where the original spawns SE.EXE
// (`run_scenario_editor`, 0xa22d2). The application implements it with the
// editor library, so that the game does not depend on it.
class EditorRunner {
 public:
  virtual ~EditorRunner() = default;
  // Runs the editor (started by the game: File / Run offered) on the
  // front end's host until it ends.
  virtual EditorExit run_editor() = 0;
};

}  // namespace sh
