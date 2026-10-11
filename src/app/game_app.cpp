#include "app/game_app.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdio>
#include <ctime>
#include <deque>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#include "app/command_icons.h"
#include "app/panel_status.h"
#include "app/sound_system.h"
#include "assets/exe_image.h"
#include "assets/game_exe.h"
#include "assets/smacker.h"
#include "assets/speech.h"
#include "core/edition.h"
#include "core/install.h"
#include "editor/editor.h"
#include "game/frontend.h"
#include "game/frontend_results.h"
#include "game/game_runner.h"
#include "game/lifecycle_log.h"
#include "game/mission.h"
#include "game/mission_session.h"
#include "game/resume.h"
#include "game/resume_keeper.h"
#include "game/shpath.h"
#include "platform/bios_keys.h"
#include "platform/command_panel.h"
#include "platform/lifecycle_watch.h"
#include "platform/screenshot.h"
#include "ui/dialog.h"
#include "ui/skin.h"

namespace sh {

namespace {

// Shuts SDL down (its thread-local and driver state) when the program ends.
struct SdlQuitGuard {
  SdlQuitGuard() = default;
  ~SdlQuitGuard() { SDL_Quit(); }
  SdlQuitGuard(const SdlQuitGuard&) = delete;
  SdlQuitGuard& operator=(const SdlQuitGuard&) = delete;
};

// A video's sound track through an SDL audio stream.
class SdlVideoSound final : public VideoSound {
 public:
  SdlVideoSound() = default;
  ~SdlVideoSound() override { stop(); }
  SdlVideoSound(const SdlVideoSound&) = delete;
  SdlVideoSound& operator=(const SdlVideoSound&) = delete;

  void start(const SmackerAudioFormat& format) override {
    stop();
    format_ = format;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
      std::fprintf(stderr, "%s: no video sound: %s\n", edition::kExecutable, SDL_GetError());
      return;
    }
    initialized_ = true;
    const SDL_AudioSpec spec{.format = format.bits == 16 ? SDL_AUDIO_S16LE : SDL_AUDIO_U8,
                             .channels = format.channels,
                             .freq = static_cast<int>(format.rate)};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (stream_ == nullptr) {
      std::fprintf(stderr, "%s: no video sound: %s\n", edition::kExecutable, SDL_GetError());
      return;
    }
    SDL_ResumeAudioStreamDevice(stream_);
  }

  void queue(std::span<const std::uint8_t> pcm) override {
    if (stream_ != nullptr && !pcm.empty()) {
      SDL_PutAudioStreamData(stream_, pcm.data(), static_cast<int>(pcm.size()));
    }
  }

  void stop() override {
    if (stream_ != nullptr) {
      SDL_DestroyAudioStream(stream_);
      stream_ = nullptr;
    }
    if (initialized_) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      initialized_ = false;
    }
    format_.reset();
  }

  // Stops the track, letting SDL's audio subsystem restart
  // (`AudioDevice::reopen`); `restore` starts it again, without the sound
  // queued so far.
  void release() {
    const std::optional<SmackerAudioFormat> format = format_;
    stop();
    released_ = format;
  }
  void restore() {
    if (released_) {
      start(*released_);
      released_.reset();
    }
  }

 private:
  SDL_AudioStream* stream_ = nullptr;
  bool initialized_ = false;
  std::optional<SmackerAudioFormat> format_;  // While started.
  std::optional<SmackerAudioFormat> released_;
};

// The SDL host of the front end and of the scenario editor; its missions
// run on a `GameRunner`. It turns the system's text input on for the text
// fields being edited, keeps the resume snapshot through the lifecycle
// events (`lifecycle`, a `ResumeKeeper`) and stops the game clock while
// the program is in the background.
class GameApp final : public FrontendHost, public EditorRunner {
 public:
  GameApp(const GameAppOptions& options, const Install& install, const ShPaths& paths, const ExeImage& exe,
          const SaveDir& saves, const SaveDir& custom, bool resume)
      : options_(options),
        install_(install),
        paths_(paths),
        saves_(saves),
        custom_(custom),
        headless_(options.screenshot.has_value()),
        display_(edition::kName, 2, options.scaling),
        sound_(install, paths),
        runner_({.install = install,
                 .paths = paths,
                 .exe = exe,
                 .saves = saves,
                 .custom = custom,
                 .sound = &sound_.layer(),
                 .sound_lock = sound_.lock(),
                 .wait_tick = headless_ ? std::function<void()>() : [] { Display::delay_ms(1); },
                 .features = options.features},
                *this),
        pending_(options.keys.begin(), options.keys.end()) {
    if (resume) {
      log_.emplace(saves.dir());
      keeper_.emplace(saves, [this] { return runner_.snapshot(); }, &*log_);
    }
    if (!headless_) {
      display_.remember_fullscreen(saves.dir() / "DISPLAY.CFG");
    }
    if (options.touch_panel) {
      command_panel_ = std::make_unique<CommandPanel>(saves.dir() / "CMDPANEL.CFG");
      display_.set_command_panel(*command_panel_, build_command_icons(CommandIconArt(install, paths)));
      display_.show_command_panel(false);
    }
  }

  MissionRunner& runner() { return runner_; }
  // The front end whose GUI the missions use; must be set before a run.
  void set_front_end(FrontEnd& front_end) { runner_.set_front_end(front_end); }

  // Handles a lifecycle event at an event poll (from a `LifecycleWatch`,
  // or the event queue for the window's focus and the quit).
  void lifecycle(LifecycleEvent e) {
    switch (e) {
      case LifecycleEvent::kWillEnterBackground:
      case LifecycleEvent::kDidEnterBackground:
        if (!suspended_) {
          suspended_ = true;
          suspended_at_ms_ = Display::ticks_ms();
        }
        break;
      case LifecycleEvent::kDidEnterForeground:
        if (suspended_) {
          suspended_ = false;
          background_ms_ += Display::ticks_ms() - suspended_at_ms_;
        }
        break;
      default:
        break;
    }
    if (keeper_) {
      keeper_->handle(e);
    }
  }
  // Logs the launch (with the resume snapshot) and, from then on, the
  // sound output's state.
  void launch() {
    if (keeper_) {
      keeper_->launch(edition::kName);
    }
    if (!headless_) {
      sound_.set_log([this](std::string_view line) {
        SDL_Log("%s: %.*s", edition::kExecutable, static_cast<int>(line.size()), line.data());
        if (log_) {
          log_->write(line);
        }
      });
    }
  }
  // Ends the program: keeps the resume snapshot when it ends on a quit,
  // else deletes it (left through the menus).
  void exit() const {
    if (keeper_) {
      keeper_->exit();
    }
  }

  // FrontendHost

  std::uint32_t ticks() override {
    if (headless_) {
      return static_cast<std::uint32_t>(static_cast<std::uint64_t>(frames_) * 100 / 60);
    }
    // The time spent in the background does not count.
    const std::uint64_t now = suspended_ ? suspended_at_ms_ : Display::ticks_ms();
    return static_cast<std::uint32_t>((now - background_ms_) / 10);
  }

  void present(const Image8& screen, const VgaPalette& dac) override {
    const Palette palette = to_palette(dac);
    display_.present(screen, palette);
    ++frames_;
    if (headless_ && frames_ >= options_.frames) {
      save_bmp(*options_.screenshot, screen, palette);
      throw QuitRequested();
    }
  }

  void poll_input(ui::Gui& gui) override {
    // Every GUI the host runs (the front end's, the editor's) gets the
    // soft keyboard.
    if (!gui.has_text_input()) {
      gui.set_text_input({.begin =
                              [this](const ui::Rect& r, ui::TextFieldKind kind) {
                                display_.begin_text_input({r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1},
                                                          kind == ui::TextFieldKind::kDigits ? TextInputKind::kDigits
                                                                                             : TextInputKind::kText);
                              },
                          .end = [this] { display_.end_text_input(); }});
    }
    if (command_panel_) {
      // Only over a mission in play, never over the menus.
      display_.show_command_panel(runner_.mission_in_play());
      if (options_.features.panel_status) {
        // The live state, read from the mission (not in the original).
        MissionSession* session = runner_.mission_in_play() ? runner_.session() : nullptr;
        std::optional<PanelStatus> status;
        if (session != nullptr) {
          const Mission& mission = session->screen().mission();
          status = command_panel_status(mission.player(), mission.player_combat().tubes);
        }
        command_panel_->set_status(std::move(status));
      }
    }
    if (!pending_.empty()) {
      const InputEvent e = pending_.front();
      pending_.pop_front();
      feed(gui, e);
    }
    while (const std::optional<InputEvent> e = display_.poll()) {
      feed(gui, *e);
    }
    if (!headless_) {
      keep_sound_output();
    }
  }

  VideoSound* video_sound() override { return sound_.has_device() ? &video_sound_ : nullptr; }

  void play_sfx(std::int32_t sample, std::int32_t volume, std::int32_t pan) override {
    sound_.play_sfx(sample, volume, pan);
  }

  bool play_speech(std::string_view file_name) override {
    return sound_.start_speech(read_speech_raw(install_, file_name, paths_.dir(GameDir::kSpeech)));
  }

  void stop_speech() override {
    sound_.with_layer([](MelLayer& layer) { layer.speech_stop(); });
  }

  void apply_sound_options(const GameOptions& o) override {
    const SoundOptions sound{.sound_effects = o.sound_effects != 0,
                             .engine_sounds = o.engine_sounds != 0,
                             .speech = o.speech != 0,
                             .effects_volume = o.effects_volume,
                             .speech_volume = o.speech_volume};
    sound_.with_layer([&](MelLayer& layer) { sh::apply_sound_options(layer, sound); });
  }

  void report(std::string_view message) override {
    SDL_Log("%s: %.*s", edition::kExecutable, static_cast<int>(message.size()), message.data());
    if (log_) {
      log_->write(std::format("report: {}", message));
    }
  }

  // EditorRunner

  EditorExit run_editor() override { return run_scenario_editor(true); }

  // Runs the scenario editor on this host; `started_by_game` false for
  // `--editor`.
  EditorExit run_scenario_editor(bool started_by_game) {
    editor::Editor editor({.install = install_, .paths = paths_, .saves = saves_, .custom = custom_}, *this,
                          started_by_game, [] { return static_cast<std::int32_t>(std::time(nullptr)); });
    return editor.run();
  }

 private:
  // Reopens the sound output when the system lost it (not in the
  // original).
  void keep_sound_output() {
    const std::uint64_t now = Display::ticks_ms();
    if (const std::optional<AudioFault> fault = sound_.audio_fault(now, !suspended_)) {
      // A video's stream would keep the audio subsystem, and the lost
      // device with it.
      video_sound_.release();
      sound_.reopen_audio(*fault, now);
      video_sound_.restore();
    }
  }

  // Feeds one input event to the GUI the way the INT 9 / INT 33h handlers
  // feed the original's event queue.
  void feed(ui::Gui& gui, const InputEvent& e) {
    switch (e.type) {
      case EventType::kQuit:
        // The window closed or the system ends the program: back here on
        // the next launch. The event watch has normally seen it first.
        if (keeper_ && !keeper_->quitting()) {
          lifecycle(LifecycleEvent::kQuit);
        }
        throw QuitRequested();
      case EventType::kFocusLost:
      case EventType::kFocusGained:
#ifdef SDL_PLATFORM_ANDROID
        // SDLActivity reports the background only once the activity is
        // stopped (onStop, on multi-window systems), but the system may
        // kill the app from the recent apps screen while it is only
        // paused: its window then has just lost the focus.
        lifecycle(e.type == EventType::kFocusLost ? LifecycleEvent::kFocusLost : LifecycleEvent::kFocusGained);
#endif
        break;
      case EventType::kKeyDown:
      case EventType::kText:
        if (is_back_key(e)) {
          // Back activates the back control of the front end's dialogs
          // (their way back one menu, EXIT TO DOS in the main menu) and
          // is Esc everywhere else: a mission's dialogs and the scenario
          // editor's have none.
          gui.mouse().post_back(bios_shift_state(e.mods));
        } else if (const std::optional<std::uint16_t> word = bios_key_word(e)) {
          gui.mouse().post_key(*word, bios_shift_state(e.type == EventType::kText ? mods_ : e.mods));
        }
        if (e.type == EventType::kKeyDown) {
          mods_ = e.mods;
        }
        break;
      case EventType::kKeyUp:
        mods_ = e.mods;
        break;
      case EventType::kMouseMove:
      case EventType::kMouseDown:
      case EventType::kMouseUp: {
        const std::uint8_t bit = e.button == MouseButton::kLeft    ? ui::kButtonLeft
                                 : e.button == MouseButton::kRight ? ui::kButtonRight
                                 : e.button == MouseButton::kMiddle ? ui::kButtonMiddle
                                                                    : 0;
        if (e.type == EventType::kMouseDown) {
          buttons_ |= bit;
        } else if (e.type == EventType::kMouseUp) {
          buttons_ &= static_cast<std::uint8_t>(~bit);
        }
        gui.mouse().update(e.x, e.y, buttons_, bios_shift_state(mods_));
        break;
      }
      case EventType::kMouseWheel:
        // The original had no wheel; the precise dials take it.
        if (options_.features.precise_dials && e.wheel != 0) {
          gui.mouse().post_wheel(e.wheel, bios_shift_state(mods_));
        }
        break;
    }
  }

  const GameAppOptions& options_;
  const Install& install_;
  const ShPaths& paths_;
  const SaveDir& saves_;
  const SaveDir& custom_;
  bool headless_;
  // The lifecycle log and the keeping of the resume snapshot (not for a
  // headless run or the editor alone).
  std::optional<LifecycleLog> log_;
  std::optional<ResumeKeeper> keeper_;
  // The floating command button (outlives the display that shows it).
  std::unique_ptr<CommandPanel> command_panel_;
  Display display_;
  SoundSystem sound_;
  GameRunner runner_;
  SdlVideoSound video_sound_;
  std::deque<InputEvent> pending_;
  int frames_ = 0;
  std::uint8_t buttons_ = 0;
  std::uint16_t mods_ = 0;
  // The background: since when, and the time spent there before.
  bool suspended_ = false;
  std::uint64_t suspended_at_ms_ = 0;
  std::uint64_t background_ms_ = 0;
};

}  // namespace

std::filesystem::path user_dir() {
  char* pref = SDL_GetPrefPath(edition::kPrefOrg, edition::kPrefApp);
  if (pref == nullptr) {
    throw std::runtime_error(std::format("no save directory: {}", SDL_GetError()));
  }
  std::filesystem::path dir(pref);
  SDL_free(pref);
  return dir;
}

int run_game(const GameAppOptions& options, const Install& install) {
  const SdlQuitGuard sdl_quit;
  if (options.screenshot) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  }
  // Android: the event poll that reports the app paused (to the lifecycle
  // watch: the resume snapshot written) blocks until it is resumed, with
  // the audio paused; the process may be killed meanwhile.
  SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE, "1");
  // load_shpath_ini (0x50985), then init_video / init_sound
  // (`sound_apply_options` runs in the front end once GOPTION.SAV is
  // read), load_cursors / load_gui_fonts (in the front end) and
  // seed_random (time()).
  const ShPaths paths = ShPaths::load(install);
  const ExeImage& exe = game_exe();
  // The player's SAVE and CUSTOM directories over the install's: the install
  // is never written.
  const std::filesystem::path user = options.save_dir.empty() ? user_dir() : std::filesystem::path();
  const SaveDir saves(options.save_dir.empty() ? user / "SAVE" : options.save_dir, &install,
                      paths.dir(GameDir::kSave));
  const SaveDir custom(options.save_dir.empty() ? user / "CUSTOM" : options.save_dir / "CUSTOM", &install,
                       paths.dir(GameDir::kCustom));
  SDL_Log("%s: saves in %s, custom scenarios in %s", edition::kExecutable, saves.dir().string().c_str(),
          custom.dir().string().c_str());
  // The resume snapshot: not for a scripted headless run or the editor
  // alone.
  const bool resume = !options.screenshot && !options.editor;
  GameApp app(options, install, paths, exe, saves, custom, resume);
  FrontEnd front_end({.install = install, .paths = paths, .exe = exe, .saves = saves, .custom = &custom,
                      .features = options.features,
                      .extended_rules = options.extended_rules},
                     app, app.runner(), static_cast<std::uint32_t>(std::time(nullptr)));
  app.set_front_end(front_end);
  front_end.set_play_intro(options.play_intro);
  front_end.set_editor(&app);
  front_end.set_resume(resume);
  app.launch();
  try {
    // The lifecycle events from now on (the front end ready), until the
    // front end is gone.
    const LifecycleWatch watch([&app](LifecycleEvent e) { app.lifecycle(e); });
    if (options.editor) {
      // SE.EXE started from DOS: its exit code ends the program.
      app.run_scenario_editor(false);
    } else {
      front_end.run();
    }
  } catch (const QuitRequested&) {
    // The window was closed, EXIT TO DOS? confirmed, or the screenshot
    // taken: the original leaves through exit().
  }
  app.exit();
  return 0;
}

}  // namespace sh
