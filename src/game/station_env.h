// What the in-mission screens share: the mission-wide sprites and fonts
// `init_simulation` loads, the sound and DAC palette calls, the UI globals
// of the mission (rig for red, redraw flag, station pages...), and the
// modal dialog runner. `MissionScreen` owns them and hands a `StationEnv`
// to every station.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string_view>

#include "assets/fnt.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "assets/exe_image.h"
#include "audio/mel_layer.h"
#include "core/install.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "ui/dialog.h"

namespace sh {

class ControlBar;
class Mission;
class StationRegistry;
struct GameTables;

// The sprites and fonts of a mission (`init_simulation`, 0x4f078), and the
// cursors (`load_cursors`).
struct StationAssets {
  Shp cursors;   // ART/CURSORS.SHP (0x11a258)
  Shp global;    // ART/GLOBAL.SHP (0x11a270): bar buttons, clock box
  Font bearing;  // ART/BEARING.FNT (0x11a25c)
  Font digits;   // ART/DIGITS.FNT (0x11a260)
  Font digits2;  // ART/DIGITS2.FNT (0x11a264): the bar clock
  Font font1;    // ART/FONT1.FNT (0x11a268): the compression button
  Font msg;      // ART/MSG.FNT (0x11a26c): the crew message line

  // Loads the files from `install`; throws `DataError` if one is missing.
  static StationAssets load(const Install& install);
};

// Holds off the thread that renders a MEL layer (the audio device's) while
// the game thread works on the layer: a BasicLockable.
class SoundLock {
 public:
  virtual ~SoundLock() = default;
  virtual void lock() = 0;
  virtual void unlock() = 0;
};

// The sound calls of the stations and menus: effects through the MEL layer
// and the crew speech, all silent without a layer.
class SoundPort {
 public:
  // `layer` may be null (no sound); `install`, `paths`, `options` (the
  // sound settings) and the layer must outlive the port. Every call on the
  // layer holds `lock` when given.
  SoundPort(MelLayer* layer, const Install& install, const ShPaths& paths, const GameOptions& options,
            SoundLock* lock = nullptr)
      : layer_(layer), install_(&install), paths_(&paths), options_(&options), lock_(lock) {}

  // `sound_sfx_is_playing`.
  bool sfx_playing(std::int32_t sfx) const;
  // `sound_play_sfx(sfx, volume, pan)` (volume 0..100, -1 full).
  void play_sfx(std::int32_t sfx, std::int32_t volume = -1, std::int32_t pan = -1);
  // `sound_stop_sfx`.
  void stop_sfx(std::int32_t sfx);
  // `stop_all_sounds_and_speech` (0x50905): every effect channel and the
  // speech voice.
  void stop_all();
  // `sound_apply_options` (0x508d2): the mute mode and volumes of the
  // game options.
  void apply_options();
  // `play_speech_raw`: plays SPEECH\`file_name` (else ALT\) when speech is
  // on.
  void play_speech(std::string_view file_name);
  // The speech of `crew_message`: "<sound>.RAW" when the speech option is
  // on and `sound` is not empty.
  void play_crew_speech(std::string_view sound);
  // `speech_service`; nothing to do for the software mixer.
  bool speech_playing() const;

 private:
  // The MEL layer's view of the game options.
  SoundOptions sound_options() const;
  // Runs `fn` on the layer under the lock; nothing without a layer.
  template <class Fn>
  void with_layer(Fn&& fn) const;

  MelLayer* layer_;
  const Install* install_;
  const ShPaths* paths_;
  const GameOptions* options_;
  SoundLock* lock_;
};

// The VGA palette as the display shows it (the DAC), with the original's
// palette effects. The fades of the original wait for the vertical retrace
// between steps and block the game; here a fade is queued, and the host
// holds the game while it steps the queued fades one retrace at a time.
class Dac {
 public:
  const VgaPalette& colours() const { return dac_; }
  // Sets every entry at once (`vga_set_palette_256`), dropping the fades.
  void set(const VgaPalette& colours) {
    dac_ = colours;
    fades_.clear();
  }

  // `palette_fade_out(delay, step)`: queues stepping every component down
  // by `step` to black.
  void fade_out(std::int32_t step) { fades_.push_back({Fade::kOut, step, {}}); }
  // `palette_fade_in(target, delay, step)`: queues stepping every
  // component up by `step` to `target` (components above it drop to it
  // at once).
  void fade_in(const VgaPalette& target, std::int32_t step) { fades_.push_back({Fade::kIn, step, target}); }
  // `palette_fade_to_blue` (0x58f28): queues stepping every entry except
  // 1..8 toward (0, 0, 30), 1 per retrace.
  void fade_to_blue() { fades_.push_back({Fade::kBlue, 1, {}}); }
  // Returns whether a fade is queued.
  bool fading() const { return !fades_.empty(); }
  // Runs one step of the first queued fade (one retrace).
  void step();

  // `palette_grey_out(percent, last)` (0x58e29): saves entries 9..`last`
  // and greys them to `percent` of their lightness ((max + min) / 2).
  void grey_out(std::int32_t percent, std::int32_t last);
  // `palette_restore_saved(last)` (0x58ef1).
  void restore_saved(std::int32_t last);

 private:
  enum class Fade : std::uint8_t { kOut, kIn, kBlue };
  struct Op {
    Fade fade;
    std::int32_t step;
    VgaPalette target;
  };
  bool finished(const Op& op) const;

  VgaPalette dac_{};
  VgaPalette saved_{};
  std::deque<Op> fades_;
};

// The mission's UI globals.
struct MissionUiState {
  bool rig_for_red = false;     // 0x11a8c0: the stations use their red-light .PAL
  bool redraw = false;          // 0x11a898: full redraw this frame (cleared after the update)
  std::int32_t pause = 0;       // 0x129c00: the simulation runs while 0
  std::int32_t status_page = 0;  // 0x11bcd4: 0 damage, 1 torpedoes
  std::int32_t gauges_page = 0;  // 0x11bce0: 0 engine lamps, 1 bathythermograph
  std::int32_t cabin_page = 0;   // 0x11bd00: 0 cabin, 1 ID book, 2 calendar, 3 log, 4 radio
  // 0x1094dc: the recognition manual's page (a SHIPS.DAT index), -1 closed
  // on its cover; shared by the scope, the TBT and the cabin.
  std::int32_t id_book_page = -1;
  std::int32_t selected_compartment = 7;  // 0x11bcdc
  // The rig-for-red automation memory of `cmd_toggle_rig_for_red`
  // (0x124e84: 1 rigged, 2 secured by the light) and its override 0x12506c.
  std::int32_t auto_red_state = 0;
  bool red_override = false;
  // 0x124ed8: the dive and blow commands ring their own alarm, so the next
  // telegraph order is not spoken (`EngineSounds`).
  bool quiet_order = false;
  // The debug overlays (`/SS305` only): 0x11a7bc bounding boxes, 0x11a7c0
  // group labels, 0x11a7c4 routes, 0x11a7c8 bases.
  bool debug = false;  // 0x11a7b4
  bool debug_bboxes = false;
  bool debug_labels = false;
  bool debug_routes = false;
  bool debug_bases = false;
  // The 16 animation tick counters 0x129bc0..0x129bfc, advanced by the
  // 100 Hz game timer while the simulation runs (`game_timer_tick`).
  std::array<std::int32_t, 16> timers{};
};

// The tick counters of `MissionUiState::timers` the screens use.
enum class GameTimer : std::uint8_t {
  kConnDoor = 5,         // 0x129bd4: cabin door frames
  kConnPeriscope = 6,    // 0x129bd8: periscope shaft frames
  kScreenShake = 9,      // 0x129be4: the screen shake's steps
  kConnDoorOpen = 10,    // 0x129be8: the door's open time
  kCrewMessage = 15,     // 0x129bfc: the crew message display time
};

// How a modal dialog is framed.
enum class ModalFrame : std::uint8_t {
  // The in-game menus: simulation paused, sounds stopped, chart panels
  // suspended, the palette greyed to 70 % (and restored unless the
  // mission ended), sound options applied and a full redraw afterwards.
  kMenu,
  // The message boxes: paused and greyed only.
  kGrey,
  // Paused only (the GAME OVER popup).
  kPlain,
};

// Runs modal dialogs (`gui_dialog_run`) on behalf of the stations and the
// command bar: the host holds the mission while one runs.
class ModalHost {
 public:
  virtual ~ModalHost() = default;
  // Runs `dialog` from the next frame on; `done(result)` is called once it
  // returns (it may start another modal, which then runs in the same
  // frame).
  virtual void run_modal(std::unique_ptr<ui::Dialog> dialog, ModalFrame frame, std::function<void(int)> done) = 0;
  // Returns whether a modal dialog is running or pending.
  virtual bool modal_active() const = 0;
};

// The chart station's panels, closed while a menu runs
// (`chart_suspend_panels` / `chart_resume_panels`).
class PanelSuspender {
 public:
  virtual ~PanelSuspender() = default;
  virtual void suspend_panels() = 0;
  virtual void resume_panels() = 0;
};

// Everything a station works with; all references outlive the stations.
struct StationEnv {
  const Install& install;
  const ShPaths& paths;
  const ExeImage& exe;
  const GameTables& tables;
  Mission& mission;
  ui::Gui& gui;            // its screen is the screen bitmap 0x11a574
  VgaPalette& palette;     // the screen palette 0x11a274 (6-bit)
  Dac& dac;
  StationRegistry& stations;
  ControlBar& bar;
  SoundPort& sound;
  ModalHost& modals;
  MissionUiState& ui;
  GameOptions& options;    // GOPTION.SAV values (chart follow, auto red lights, speech...)
  DetailOptions& detail;   // GDETAIL.SAV values
  const StationAssets& assets;
  // The chart's panels while the chart is open (null otherwise).
  PanelSuspender* panels = nullptr;

  Image8& screen() { return gui.screen(); }
  // `mouse_set_cursor(CURSORS, frame)`.
  void set_cursor(std::size_t frame) { gui.mouse().set_cursor(assets.cursors, frame); }
  // `mouse_set_cursor(CURSORS, 0x11dca8)`: the default cursor.
  void default_cursor() { gui.mouse().set_cursor(assets.cursors, ui::CursorId::kPointer); }
  // Returns tick counter `t` (the screens reset it by assignment).
  std::int32_t& timer(GameTimer t) { return ui.timers[static_cast<std::size_t>(t)]; }
};

// `load_pcx(name, screen, palette)` (0x589cc): draws the PCX `art_file`
// of the ART directory onto the screen at (0, 0) and makes its palette the
// screen palette. Throws `DataError` when the file is missing.
void load_screen_pcx(StationEnv& env, std::string_view art_file);
// `load_pcx(name, 0, palette)`: only takes the palette of `art_file` (the
// rig-for-red `.PAL` files).
void load_screen_palette(StationEnv& env, std::string_view art_file);

}  // namespace sh
