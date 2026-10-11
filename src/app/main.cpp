// Silent Hunter reimplementation: entry point. The command line is described
// in app/command_line.h.

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_messagebox.h>

#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "app/android_install.h"
#include "app/command_line.h"
#include "app/game_app.h"
#include "app/install_setup.h"
#include "app/install_source.h"
#include "app/palettes.h"
#include "app/sound_system.h"
#include "app/ui_text.h"
#include "app/viewer.h"
#include "assets/game_exe.h"
#include "core/edition.h"
#include "core/error.h"
#include "core/image.h"
#include "core/install.h"
#include "game/shpath.h"
#include "platform/display.h"
#include "platform/screenshot.h"

namespace {

using Options = sh::CommandLine;

// Shuts SDL down (its thread-local and driver state) when the program ends.
struct SdlQuitGuard {
  SdlQuitGuard() = default;
  ~SdlQuitGuard() { SDL_Quit(); }
  SdlQuitGuard(const SdlQuitGuard&) = delete;
  SdlQuitGuard& operator=(const SdlQuitGuard&) = delete;
};

int run_viewer(const Options& options, const sh::Install& install) {
  const SdlQuitGuard sdl_quit;
  const bool headless = options.screenshot.has_value();
  if (headless) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
  }
  const sh::ShPaths paths = sh::ShPaths::load(install);
  sh::Display display(sh::edition::kName, 2, options.scaling);
  sh::SoundSystem sound(install, paths);
  sh::PaletteBuilder palettes(install, paths);
  const sh::UiText text(install, paths);
  const sh::ViewerServices services{
      .install = install, .paths = paths, .palettes = palettes, .sound = sound, .text = text};
  sh::Viewer viewer(services);
  if (options.open) {
    viewer.open(*options.open);
  }

  std::deque<sh::InputEvent> pending(options.keys.begin(), options.keys.end());
  sh::Image8 frame(sh::kScreenWidth, sh::kScreenHeight);
  sh::Palette palette;
  for (int n = 1;; ++n) {
    if (!pending.empty()) {
      const sh::InputEvent e = pending.front();
      pending.pop_front();
      if (!viewer.handle(e)) {
        return 0;
      }
    }
    while (const auto e = display.poll()) {
      if (!viewer.handle(*e)) {
        return 0;
      }
    }
    if (!headless) {
      // The sound output lost by the system (a video's stream playing
      // meanwhile keeps the subsystem: its device may stay lost).
      const std::uint64_t now_ms = sh::Display::ticks_ms();
      if (const std::optional<sh::AudioFault> fault = sound.audio_fault(now_ms, true)) {
        sound.reopen_audio(*fault, now_ms);
      }
    }
    const std::uint64_t now_us =
        headless ? static_cast<std::uint64_t>(n) * 1'000'000 / 60 : sh::Display::ticks_ms() * 1000;
    viewer.update(now_us);
    viewer.render(frame, palette);
    display.present(frame, palette);
    if (headless && n >= options.frames) {
      sh::save_bmp(*options.screenshot, frame, palette);
      return 0;
    }
  }
}

int run(const Options& options, const sh::Install& install) {
  // The tables and text the original keeps in its executable.
  sh::load_game_exe(install);
  if (options.viewer) {
    return run_viewer(options, install);
  }
  sh::GameAppOptions game{.save_dir = options.save_dir,
                          .play_intro = options.intro,
                          .editor = options.editor,
                          .scaling = options.scaling,
                          .touch_panel = options.touch_panel,
                          .features = options.faithful ? sh::Features::faithful() : sh::Features::for_build(),
                          .extended_rules = sh::edition::kExtended && !options.faithful,
                          .keys = options.keys,
                          .screenshot = {},
                          .frames = options.frames};
  if (options.screenshot) {
    game.screenshot = *options.screenshot;
  }
  return sh::run_game(game, install);
}

// Returns the source remembered by the asset setup in `config`, nothing
// when there is none or it cannot be read (the setup then runs again).
std::optional<sh::InstallSource> remembered_source(const std::filesystem::path& config) {
  try {
    return sh::load_install_config(config);
  } catch (const sh::DataError& e) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", sh::edition::kExecutable, e.what());
    return std::nullopt;
  }
}

}  // namespace

#ifdef SDL_PLATFORM_ANDROID

namespace {

// Returns the install embedded in the APK; without one, the install
// remembered by the asset setup, else the setup's; nothing when the player
// quits the setup.
std::optional<sh::Install> android_install() {
  if (std::optional<sh::Install> apk = sh::apk_install()) {
    return apk;
  }
  const std::filesystem::path config = sh::user_dir() / sh::kInstallConfigName;
  sh::StartupInstall start =
      sh::choose_startup_install(std::nullopt, false, remembered_source(config), sh::open_install_source);
  if (start.install) {
    return std::move(start.install);
  }
  return sh::run_install_setup(start.problems, config, {});
}

}  // namespace

// Android has no command line: the game runs from the install embedded in
// the APK (app/android_install.h) or the one of the asset setup, with the
// floating command button of touch screens. Errors go to the log and a
// message box.
int main(int, char**) {
  SDL_SetAppMetadata(sh::edition::kName, nullptr, sh::edition::kAppId);
  try {
    Options options;
    options.touch_panel = true;
    const std::optional<sh::Install> install = android_install();
    return install ? run(options, *install) : 0;
  } catch (const std::exception& e) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", sh::edition::kExecutable, e.what());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, sh::edition::kName, e.what(), nullptr);
    return 1;
  }
}

#else

namespace {

// Returns the install of `--data`, else the one remembered by the asset
// setup, else the setup's (with `--setup`, always the setup's); nothing
// when the player quits the setup. A scripted headless run shows no
// dialog: its `--data` must be an install.
std::optional<sh::Install> desktop_install(const Options& options) {
  if (options.screenshot && options.data_dir) {
    return sh::Install(*options.data_dir);
  }
  const std::filesystem::path config = sh::user_dir() / sh::kInstallConfigName;
  const std::optional<sh::InstallSource> remembered = remembered_source(config);
  std::optional<std::filesystem::path> data_dir;
  if (options.data_dir) {
    data_dir = *options.data_dir;
  }
  sh::StartupInstall start =
      sh::choose_startup_install(data_dir, options.setup, remembered, sh::open_install_source);
  if (start.install) {
    return std::move(start.install);
  }
  for (const std::string& p : start.problems) {
    std::fprintf(stderr, "%s: %s\n", sh::edition::kExecutable, p.c_str());
  }
  if (options.screenshot) {
    throw std::runtime_error("no game files: give --data <install dir>");
  }
  const bool remembered_folder = remembered && remembered->kind == sh::InstallSource::Kind::kFolder;
  return sh::run_install_setup(start.problems, config, remembered_folder ? remembered->location : std::string());
}

}  // namespace

int main(int argc, char** argv) {
  SDL_SetAppMetadata(sh::edition::kName, nullptr, sh::edition::kAppId);
  try {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    const Options options = sh::parse_command_line(args);
    const std::optional<sh::Install> install = desktop_install(options);
    return install ? run(options, *install) : 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s: %s\n", sh::edition::kExecutable, e.what());
    return 1;
  }
}

#endif
