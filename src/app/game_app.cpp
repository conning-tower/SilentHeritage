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
#include "assets/pcx.h"
#include "assets/smacker.h"
#include "assets/speech.h"
#include "core/display_config.h"
#include "core/edition.h"
#include "core/install.h"
#include "core/platform.h"
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

// Returns the RGB picture of `pcx`'s columns `x0`..`x1` (not in the original).
RgbaImage pcx_columns(const PcxImage& pcx, int x0, int x1) {
  const Palette palette = to_palette(pcx.palette);
  RgbaImage out(x1 - x0 + 1, pcx.image.height());
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      const Rgb c = palette.colors[pcx.image.at(x0 + x, y)];
      out.at(x, y) = 0xff000000u | (std::uint32_t{c.r} << 16) | (std::uint32_t{c.g} << 8) | c.b;
    }
  }
  return out;
}

// Returns the side fill of the wood: the plain desk right of WOOD.PCX's
// medal frames, followed by its mirror image. The columns are where the
// grain runs level, so that the mirror axes hardly show (not in the
// original).
RgbaImage side_fill_wood(const Install& install) {
  constexpr int kX0 = 486;
  constexpr int kX1 = 620;
  const RgbaImage half = pcx_columns(load_pcx(install, "ART/WOOD.PCX"), kX0, kX1);
  RgbaImage out(2 * half.width, half.height);
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < half.width; ++x) {
      out.at(x, y) = half.at(x, y);
      out.at(out.width - 1 - x, y) = half.at(x, y);
    }
  }
  return out;
}

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
        // A headless run neither reads nor writes DISPLAY.CFG, and is never
        // fullscreen.
        display_config_(headless_ ? DisplayConfig{} : read_display_config(saves.dir() / kDisplayConfigName)),
        display_settings_(initial_display_settings(display_config_, options.extended_config, kAndroid)),
        display_(edition::kName, 2, scaling(), options.features.wide_screen, options.window_width,
                 options.window_height, !headless_ && display_settings_.fullscreen),
        sound_(install, paths),
        runner_({.install = install,
                 .paths = paths,
                 .exe = exe,
                 .saves = saves,
                 .custom = custom,
                 .sound = &sound_.layer(),
                 .sound_lock = sound_.lock(),
                 .wait_tick = headless_ ? std::function<void()>() : [] { Display::delay_ms(1); },
                 .features = options.features,
                 .extended_config = options.extended_config},
                *this),
        pending_(options.keys.begin(), options.keys.end()) {
    if (resume) {
      log_.emplace(saves.dir());
      keeper_.emplace(saves, [this] { return runner_.snapshot(); }, &*log_);
    }
    if (!headless_) {
      display_.on_fullscreen_toggled([this](bool) { save_display_config(); });
    }
    if (options.features.wide_screen) {
      display_.set_side_texture(SideFill::Kind::kBrass,
                                pcx_columns(load_pcx(install, "ART/MENUBGND.PCX"), 0, kScreenWidth - 1));
      display_.set_side_texture(SideFill::Kind::kWood, side_fill_wood(install));
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
    if (headless_ && options_.features.wide_screen && frames_ + 1 >= options_.frames) {
      // The window as shown as well: the side fill is not in the frame.
      std::filesystem::path window = *options_.screenshot;
      window.replace_extension(".window.bmp");
      display_.capture_next(window);
    }
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
    // ... and shows its cursor where the frame cannot: beside it, over the
    // side fill and the black bars (not in the original).
    if (!gui.mouse().has_cursor_beyond()) {
      gui.mouse().set_cursor_beyond([this](const SpriteFrame* frame, int x, int y) {
        if (frame == nullptr) {
          display_.set_cursor_beyond(std::nullopt);
        } else {
          display_.set_cursor_beyond(frame->decode(), x + frame->x0, y + frame->y0);
        }
      });
    }
    if (command_panel_) {
      // Only over a mission in play, never over the menus.
      display_.show_command_panel(runner_.mission_in_play());
      MissionSession* session = runner_.mission_in_play() ? runner_.session() : nullptr;
      // The mission's improvements: the options can change them meanwhile.
      const Features& features = session != nullptr ? session->screen().ui_state().features : options_.features;
      // The floating button, unless hidden or in the command bar (not in
      // the original).
      command_panel_->set_button_shown(options_.orders_button && features.floating_orders());
      if (features.panel_status) {
        // The live state, read from the mission (not in the original).
        std::optional<PanelStatus> status;
        if (session != nullptr) {
          const Mission& mission = session->screen().mission();
          status = command_panel_status(mission.player(), mission.player_combat().tubes);
          if (options_.features.torpedo_types) {
            // The loaded tubes' types on their label strips (not in the
            // original).
            const std::array<TubeMark, kTubeIconCount> marks = tube_marks(mission.player_combat());
            for (std::size_t t = 0; t < marks.size() && t < status->tubes.size(); ++t) {
              status->tubes[t].label = marks[t].label;
            }
          }
        }
        command_panel_->set_status(std::move(status));
      } else {
        command_panel_->set_status(std::nullopt);
      }
    }
    // The game moved the pointer since the last poll (the charts' edge
    // scroll and zoom box): the system pointer follows, as the original's
    // mouse driver did, so that the pointer drawn is where the next motion
    // starts. The pointer itself, not the game's position: one only held
    // at the screen's edge (beside it, or the screen narrowed under it)
    // stays where it is. Where the system does not move its pointer (some
    // Wayland compositors), the game's goes back to it instead, so that the
    // cursor drawn stays on the real one: a chart then keeps scrolling while
    // the pointer is on its edge.
    const ui::Mouse& mouse = gui.mouse();
    if (!headless_ && pointer_.gui == &gui &&
        (mouse.pointer_x() != pointer_.x || mouse.pointer_y() != pointer_.y)) {
      if (display_.pointer_warp_refused()) {
        feed(gui, InputEvent{.type = EventType::kMouseMove, .x = pointer_.x, .y = pointer_.y});
      } else {
        display_.warp_pointer(mouse.pointer_x(), mouse.pointer_y());
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
    pointer_ = {.gui = &gui, .x = mouse.pointer_x(), .y = mouse.pointer_y()};
    // The charts' edge scrolling: only the very edges when the pointer is
    // held in the fullscreen picture, none on touch screens (not in the
    // original).
#ifdef SDL_PLATFORM_ANDROID
    gui.mouse().set_edges(ui::PointerEdges::kNone);
#else
    gui.mouse().set_edges(display_.fullscreen() ? ui::PointerEdges::kExact : ui::PointerEdges::kBand);
#endif
    if (!headless_) {
      keep_sound_output();
    }
  }

  int screen_width() override { return display_.screen_width(); }

  void set_wide_screen(bool on) override { display_.set_wide_aspect(on); }

  void set_side_fill(const SideFill& fill) override { display_.set_side_fill(fill); }
  void set_bottom_strip(const Image8* strip) override { display_.set_bottom_strip(strip); }

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

  std::optional<DisplayOptions> display_options() const override {
    if (!options_.extended_config) {
      return std::nullopt;
    }
    return DisplayOptions{.settings = {.fullscreen = display_.fullscreen(), .smoothing = display_settings_.smoothing},
                          .fullscreen_fixed = kAndroid,
                          .smoothing_fixed = options_.scaling == Scaling::kSharp};
  }

  void set_display_settings(const DisplaySettings& settings) override {
    if (!headless_) {
      display_.set_fullscreen(settings.fullscreen);
    }
    display_settings_.smoothing = settings.smoothing;
    display_.set_scaling(scaling());
    save_display_config();
  }

  bool has_orders_panel() const override { return command_panel_ != nullptr; }
  void open_orders_panel(const ui::Rect& button) override {
    display_.open_command_panel_above({button.x0, button.y0, button.x1 - button.x0 + 1, button.y1 - button.y0 + 1});
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
    editor::Editor editor({.install = install_,
                           .paths = paths_,
                           .saves = saves_,
                           .custom = custom_,
                           .features = options_.features,
                           .extended_torpedoes = options_.extended_config},
                          *this,
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

  // Returns how the picture is scaled: `--sharp` forces sharp pixels.
  Scaling scaling() const {
    return options_.scaling == Scaling::kSharp || !display_settings_.smoothing ? Scaling::kSharp : Scaling::kSmooth;
  }

  // Writes the window's settings to DISPLAY.CFG (not for a headless run).
  // Without the extended settings, the smoothing saved there is kept as
  // it is.
  void save_display_config() {
    if (headless_) {
      return;
    }
    display_config_.fullscreen = display_.fullscreen();
    if (options_.extended_config) {
      display_config_.smoothing = display_settings_.smoothing;
    }
    if (!write_display_config(saves_.dir() / kDisplayConfigName, display_config_)) {
      SDL_Log("%s: cannot save the display settings", edition::kExecutable);
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
          // is Esc everywhere else (a mission's dialogs and the scenario
          // editor's have none), but for the command bar, where it goes
          // back to the conn.
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
      case EventType::kFingerDown:
      case EventType::kFingerMove:
        // The charts' finger scrolling and the extended chart's pinch.
        gui.mouse().touch(e.finger, e.x, e.y);
        break;
      case EventType::kFingerUp:
        gui.mouse().lift(e.finger);
        break;
      case EventType::kMouseWheel:
        // The original had no wheel; the precise dials and the extended
        // chart take it.
        if ((options_.features.precise_dials || options_.features.chart_tools) && e.wheel != 0) {
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
  // DISPLAY.CFG as read and last written, and the window's settings.
  DisplayConfig display_config_;
  DisplaySettings display_settings_;
  Display display_;
  SoundSystem sound_;
  GameRunner runner_;
  SdlVideoSound video_sound_;
  std::deque<InputEvent> pending_;
  int frames_ = 0;
  std::uint8_t buttons_ = 0;
  std::uint16_t mods_ = 0;
  // The pointer (`Mouse::pointer_x`) after the last poll, and the GUI it
  // was in.
  struct PolledPointer {
    const ui::Gui* gui = nullptr;
    int x = 0;
    int y = 0;
  };
  PolledPointer pointer_;
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
  // Android: the system's ordinary audio path instead of the low-latency
  // one, whose stream the system drops more often (calls, screen off,
  // folding) and which the game's 22 kHz mix does not need.
  SDL_SetHint(SDL_HINT_ANDROID_LOW_LATENCY_AUDIO, "0");
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
                      .extended_config = options.extended_config},
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
