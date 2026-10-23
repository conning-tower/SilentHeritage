// The asset setup: when the game files are not available, native dialogs
// (the game's own fonts and art being part of what is missing) ask the
// player for the folder of the original install, check it and remember it
// (app/install_source.h). On Android the folder is chosen through the
// Storage Access Framework and either read in place or copied into the
// app's internal storage.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "app/install_source.h"
#include "core/install.h"

namespace sh {

// Runs the setup: a message explaining what is needed (after `problems`,
// what was wrong with the previous sources), the folder picker, starting
// at `start_folder` when not empty, and a new choice while the folder is
// not a complete install. Writes the choice to `config_file`. Returns the
// install chosen, nothing when the player quits or no picker is available
// (said in a message). Throws `std::runtime_error` when the choice cannot
// be remembered.
std::optional<Install> run_install_setup(const std::vector<std::string>& problems,
                                         const std::filesystem::path& config_file, const std::string& start_folder);

// Offers to install `media` (`--data`'s CD files) into the preference
// directory, remembered in `config_file`; returns the install, nothing when
// the player declines. Throws `DataError` or `std::runtime_error` when the
// installation fails.
std::optional<Install> run_media_install(const GameMedia& media, const std::filesystem::path& config_file);

// Opens a remembered source: host folders, and on Android document trees.
// Throws `DataError` when it cannot be opened.
Install open_install_source(const InstallSource& source);

}  // namespace sh
