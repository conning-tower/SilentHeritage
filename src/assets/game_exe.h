// The process-wide image of the player's SH.EXE: the original keeps its
// constant tables and most of its text in the executable, and the
// reimplementation reads them from there (by linear address) instead of
// carrying copies. Loaded once at startup, before any game code runs; code
// handed an `ExeImage` reads that one instead.
#pragma once

#include "assets/exe_image.h"
#include "core/install.h"

namespace sh {

// Loads SH.EXE from `install`, makes it the process-wide image and installs
// the fixed-point math tables read from it (`fx::install_tables`). Throws
// `DataError` when SH.EXE is missing or not the original's (see `ExeImage`),
// or when its tables do not hold the expected values.
void load_game_exe(const Install& install);

// As `load_game_exe`, from an image already built.
void set_game_exe(ExeImage exe);

// Returns the image of the last `load_game_exe` / `set_game_exe`. Throws
// `std::logic_error` when none ran.
const ExeImage& game_exe();

// Returns whether a process-wide image is loaded.
bool game_exe_loaded();

}  // namespace sh
