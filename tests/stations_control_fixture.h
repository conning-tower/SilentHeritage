// A mission screen on a real mission for the control-room station tests:
// the GUI with the default skin, HIST01 (or another scenario) loaded, no
// sound, an injected clock, input helpers and screenshots.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/mission_screen.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "platform/screenshot.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh::test {

struct ControlRig {
  const Install& install;
  ShPaths paths;
  ExeImage exe;
  GameTables tables;
  ui::GuiResources resources;
  ui::DefaultSkin skin{resources};
  Image8 screen_image{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen_image, [this] { return tick; }, skin};
  GameOptions options;
  DetailOptions detail;
  std::unique_ptr<MissionScreen> screen;

  // Loads `scenario` (e.g. "SCEN/HIST01.SCN") with `setup` into a screen.
  explicit ControlRig(const Install& game, std::string_view scenario = "SCEN/HIST01.SCN", MissionSetup setup = {})
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        tables(GameTables::load(game)),
        resources(ui::GuiResources::load(game)) {
    screen = std::make_unique<MissionScreen>(
        MissionScreenSetup{install, paths, exe, tables, gui, nullptr, options, detail, false},
        Mission::load(install, tables, scenario, std::move(setup)));
  }

  Mission& mission() { return screen->mission(); }

  // Runs `n` frames of `ticks` ticks each; returns false once the mission
  // is over.
  bool frames(int n, std::int32_t ticks = 5) {
    for (int i = 0; i < n; ++i) {
      tick += static_cast<std::uint32_t>(ticks);
      if (!screen->frame(ticks)) {
        return false;
      }
    }
    return true;
  }
  // Runs frames until no fade or modal holds the game (at most 200).
  void settle() {
    for (int i = 0; i < 200 && (screen->env().dac.fading()); ++i) {
      frames(1);
    }
  }
  // Posts a BIOS key word, then runs `n` frames: the main loop takes one
  // event per frame, and a hotkey's activation event comes back in the
  // next one (a hotkey acts in the first frame).
  void key(std::uint16_t word, int n = 2) {
    gui.mouse().post_key(word, 0);
    frames(n);
  }
  // Clicks the left button at (x, y), running a frame after each change.
  void click(int x, int y) {
    gui.mouse().update(x, y, 0, 0);
    frames(1);
    gui.mouse().update(x, y, ui::kButtonLeft, 0);
    frames(1);
    gui.mouse().update(x, y, 0, 0);
    frames(1);
  }
  // Saves the screen with the DAC palette as `name`.bmp in the directory
  // of SH_SCREENS, when that variable is set.
  void save(std::string_view name) const {
    const char* dir = std::getenv("SH_SCREENS");
    if (dir == nullptr || *dir == '\0') {
      return;
    }
    save_bmp(std::filesystem::path(dir) / (std::string(name) + ".bmp"), screen->screen(), to_palette(screen->dac()));
  }
};

}  // namespace sh::test
