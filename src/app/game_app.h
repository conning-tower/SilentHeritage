// The game program (`game_main`, 0x10010, from the data directories on):
// SHPATH.INI, the sound with the options of GOPTION.SAV, the GUI fonts and
// cursors, the random seed, then the front end, whose missions it hands to
// the mission layer. The CD and memory checks are left out.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/features.h"
#include "core/install.h"
#include "platform/display.h"

namespace sh {

struct GameAppOptions {
  // Where the SAVE files are written (the install's SAVE directory is only
  // read), the scenario editor's CUSTOM files going to its CUSTOM
  // subdirectory; empty for the SAVE and CUSTOM subdirectories of the
  // user's SDL preference directory.
  std::filesystem::path save_dir;
  bool play_intro = true;  // false: `/nointro`
  // Starts the scenario editor instead of the front end (SE.EXE run from
  // DOS).
  bool editor = false;
  Scaling scaling = Scaling::kSmooth;  // how the picture fills the window
  // Shows the floating command button of touch screens
  // (platform/command_panel.h), its place kept in the save directory.
  bool touch_panel = false;
  // The improvements beyond the original the game runs with.
  Features features = Features::for_build();
  // Keys pressed one per frame from the start.
  std::vector<InputEvent> keys;
  // Screenshot mode: runs headless (SDL dummy drivers, a 60 Hz virtual
  // clock) for `frames` frames, saves the last one there and exits.
  std::optional<std::filesystem::path> screenshot;
  int frames = 2;
};

// Returns the player's SDL preference directory, which holds the default
// SAVE and CUSTOM directories and the remembered install (INSTALL.CFG);
// throws `std::runtime_error` when SDL has none.
std::filesystem::path user_dir();

// Runs the game (or the editor alone) from `install` until the player exits
// to DOS; returns the exit status.
// Throws `DataError` when a game file is missing or malformed.
int run_game(const GameAppOptions& options, const Install& install);

}  // namespace sh
