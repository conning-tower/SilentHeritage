// The in-mission screen host: one mission in play with its stations, the
// command bar and the main loop (`mission_main_loop`), presented frame by
// frame. It implements the loop's collaborators (sound, input, GAME OVER
// box, frame events), runs the modal menus and the blocking palette fades
// of the original between frames, and keeps the mission's UI state.
//
// Usage (the caller owns the window, the GUI and the sound):
//   MissionScreen screen(setup, Mission::load(...));
//   // The outside and attack stations, on `screen.attack_host()`:
//   screen.install(StationId::kBridge, std::make_unique<BridgeStation>(...));
//   while (screen.frame(ticks)) {
//     // feed input into setup.gui (Mouse::post_key / update), then show
//     // screen.screen() with screen.dac()
//   }
//   int end_code = screen.finish();
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "audio/mel_layer.h"
#include "core/install.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "game/mission_loop.h"
#include "game/radio_room.h"
#include "game/saves_options.h"
#include "game/screen_shake.h"
#include "game/shpath.h"
#include "game/station.h"
#include "game/engine_sounds.h"
#include "game/station_env.h"
#include "game/stations/attack_services.h"
#include "game/stations/gauges.h"
#include "ui/dialog.h"

namespace sh {

class CabinStation;

// What a mission screen is built from; all references must outlive it.
struct MissionScreenSetup {
  const Install& install;
  const ShPaths& paths;
  const ExeImage& exe;
  const GameTables& tables;
  // The GUI the mission draws on and takes input from (default skin).
  ui::Gui& gui;
  // The sound layer, or null for no sound.
  MelLayer* sound = nullptr;
  GameOptions& options;
  DetailOptions& detail;
  // The `/SS305` debug switch (0x11a7b4): debug hotkeys and overlays.
  bool debug = false;
  // Held around every call on `sound` when the audio thread renders it.
  SoundLock* sound_lock = nullptr;
  // The improvements beyond the original the mission runs with.
  Features features{};
  // EXTENDED.CFG is read and written (game/extended_config.h): GAME OPTIONS
  // has its EXTENDED page.
  bool extended_config = false;
};

class MissionScreen : private MissionSound,
                      private MissionInput,
                      private GameOverScreen,
                      private MissionEventSink,
                      private ModalHost {
  friend class StateIo;  // The state files (game/vehicle_state.h).
 public:
  // Takes `mission` (built by `Mission::create`) and installs the
  // control-room stations (conn, chart, gauges, status, captain's
  // quarters); the mission starts at the conn. Throws `DataError` when a
  // station file is missing.
  MissionScreen(MissionScreenSetup setup, std::unique_ptr<Mission> mission);
  ~MissionScreen() override;
  MissionScreen(const MissionScreen&) = delete;
  MissionScreen& operator=(const MissionScreen&) = delete;

  // Installs (or replaces) the object of station `id`. The patrol log
  // entries go to a `CabinStation` installed as the captain's quarters.
  void install(StationId id, std::unique_ptr<Station> station);

  // Runs one frame that lasted `timer_ticks` 100 Hz ticks: a step of a
  // running fade or modal menu, or else a main loop frame. Returns false
  // once the mission is over (end code set, station closed, the GAME OVER
  // presentation finished); `finish` then gives the end code.
  bool frame(std::int32_t timer_ticks);
  // Ends the mission (`Mission::finish`) and returns its end code.
  std::int32_t finish() { return mission_->finish(); }
  // Returns whether the mission is in play with nothing holding it: no
  // fade, modal menu or message box, GAME OVER wait or pause (the pause
  // counter at 0), and its main loop still running. The floating command
  // button of touch screens is shown only then.
  bool in_play() const {
    return loop_running_ && !dac_.fading() && !game_over_wait_ && modals_.empty() && ui_.pause == 0;
  }

  // The picture of the last frame (with the cursor) and the DAC palette
  // to show it with (6-bit values).
  const Image8& screen() const { return setup_.gui.screen(); }
  const VgaPalette& dac() const { return dac_.colours(); }
  // The display start row of the screen shake (game/screen_shake.h): the
  // picture is shown from that row of `screen()` on.
  std::int32_t display_start() const { return shake_.display_start(); }

  Mission& mission() { return *mission_; }
  StationEnv& env() { return env_; }
  ControlBar& bar() { return bar_; }
  // The host of the outside and attack stations (bridge, periscope, TBT,
  // deck gun, TDC, radar), on this screen's command bar and DAC, with the
  // shared nav panel and the recognition manual.
  StationEnvHost& attack_host() { return attack_host_; }
  StationRegistry& stations() { return stations_; }
  MissionUiState& ui_state() { return ui_; }
  // The dive's deepest depth for the bathythermograph (0x11b0e4).
  const DiveRecorder& dive_recorder() const { return dive_recorder_; }
  const MissionLoop& loop() const { return loop_; }

 private:
  struct Modal {
    std::unique_ptr<ui::Dialog> dialog;
    ModalFrame frame = ModalFrame::kMenu;
    std::function<void(int)> done;
    bool started = false;
  };

  // MissionSound
  bool sfx_playing(std::int32_t sfx) const override { return sound_.sfx_playing(sfx); }
  void play_sfx_looped(std::int32_t sfx) override;
  void stop_sfx(std::int32_t sfx) override { sound_.stop_sfx(sfx); }
  void service_speech() override {}
  // MissionInput
  std::optional<ui::Event> get() override { return setup_.gui.events().get(); }
  bool pending() const override { return setup_.gui.events().size() != 0; }
  // GameOverScreen: `show_game_over` (0x10358).
  void show(GameOverReason reason) override;
  // MissionEventSink
  void on_events(const MissionEvents& events) override;
  // ModalHost
  void run_modal(std::unique_ptr<ui::Dialog> dialog, ModalFrame frame, std::function<void(int)> done) override;
  bool modal_active() const override { return !modals_.empty(); }

  // Runs one iteration of the first pending modal.
  void step_modal();
  void begin_modal(Modal& m);
  void end_modal(ModalFrame frame);

  MissionScreenSetup setup_;
  std::unique_ptr<Mission> mission_;
  DiveRecorder dive_recorder_;
  // The radio messages for the captain's radio log.
  RadioRoom radio_;
  StationAssets assets_;
  MissionUiState ui_;
  Dac dac_;
  VgaPalette palette_{};
  StationRegistry stations_;
  SoundPort sound_;
  EngineSounds engine_sounds_{sound_};
  StationEnv env_;
  ControlBar bar_;
  StationEnvHost attack_host_;
  MissionLoop loop_;
  ScreenShake shake_;
  std::deque<Modal> modals_;
  // The captain's quarters, which keep the patrol and radio logs.
  CabinStation* cabin_ = nullptr;
  bool loop_running_ = true;
  // The wait after the fade to blue (`show_game_over`), in ticks.
  std::optional<std::int32_t> game_over_wait_;
};

}  // namespace sh
