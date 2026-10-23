// Where the game files come from when they are not given on the command
// line: the source chosen by the player at the asset setup
// (app/install_setup.h), remembered in INSTALL.CFG in the player's SDL
// preference directory, and the start-up decision between it, `--data` and
// a new setup. Free of dialogs, for the tests.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/game_media.h"
#include "core/install.h"

namespace sh {

// The name of the configuration file in the preference directory.
inline constexpr std::string_view kInstallConfigName = "INSTALL.CFG";

// A remembered source of the game files.
struct InstallSource {
  enum class Kind : std::uint8_t {
    kFolder,        // a host folder used in place
    kDocumentTree,  // an Android Storage Access Framework tree URI used in place
    kCopy,          // a host folder the files were copied to by the setup
  };
  Kind kind = Kind::kFolder;
  std::string location;  // the folder's path, or the tree's URI

  bool operator==(const InstallSource&) const = default;
};

// Returns the source saved in `file`, nothing if there is no such file.
// The file is text:
//
//   SH-INSTALL 1
//   folder|tree|copy <path or URI>
//
// Throws `DataError` when it cannot be read or is malformed.
std::optional<InstallSource> load_install_config(const std::filesystem::path& file);

// Writes `source` to `file` atomically, creating its directory; throws
// `std::runtime_error` on failure.
void save_install_config(const std::filesystem::path& file, const InstallSource& source);

// The outcome of opening a candidate install: the install, or why it is not
// one.
struct InstallCheck {
  std::optional<Install> install;
  std::string problem;  // set when `install` is empty
};

// Opens an install with `open` and checks it is complete enough to play:
// SH.EXE and the DATA directory present (any case), SH.EXE that of v1.31
// (`check_game_exe`). A `DataError` thrown by `open` becomes the problem.
InstallCheck check_install(const std::function<Install()>& open);

// Returns `check_install` of host folder `dir`.
InstallCheck check_install_folder(const std::filesystem::path& dir);

// The outcome of looking for the game's CD files (core/game_media.h) in a
// candidate that is not an install: the media, or why they cannot be
// installed, or neither when there are none.
struct MediaCheck {
  std::optional<GameMedia> media;
  std::string problem;  // set when the media are unusable
};

// Looks for the media in the tree `open` returns and checks their SH.EXE
// like `check_install`. A `DataError` thrown by `open` or the search
// becomes the problem.
MediaCheck check_media(const std::function<std::shared_ptr<const MediaTree>()>& open);

// Returns `check_media` of host path `path`: a folder, or a CD image file.
MediaCheck check_media_path(const std::filesystem::path& path);

// The name of the folder the media are installed into, in the player's
// preference directory (or the app's internal storage on Android).
inline constexpr std::string_view kInstalledMediaDir = "install";

// Opens a remembered source; throws `DataError` when it cannot be opened
// (on a platform without document trees: always for `kDocumentTree`).
using InstallOpener = std::function<Install(const InstallSource&)>;

// Returns the install of host folders (`kFolder` and `kCopy`); throws
// `DataError` for a document tree.
Install open_folder_source(const InstallSource& source);

// The install to start with, or the reasons to run the setup.
struct StartupInstall {
  std::optional<Install> install;  // nothing: run the setup
  // Set when `install` is a remembered source (to be kept), empty for
  // `--data`.
  std::optional<InstallSource> source;
  // What was wrong with `--data` and the remembered source, for the setup's
  // first message (or the error when no setup can run).
  std::vector<std::string> problems;
  // `--data`'s CD files, when it names the media rather than an install:
  // to be installed (with `install` empty).
  std::optional<GameMedia> media;
};

// Decides where the game files come from: with `force_setup` the setup
// runs; otherwise `data_dir` (`--data`) when it is a valid install or holds
// valid media (to be installed), then `remembered` (opened with `open`)
// when it is valid; else the setup runs with the problems found.
StartupInstall choose_startup_install(const std::optional<std::filesystem::path>& data_dir, bool force_setup,
                                      const std::optional<InstallSource>& remembered, const InstallOpener& open);

// Copies `files` of `tree` under host directory `dest` (created; files
// replaced), calling `progress` with the number of bytes copied so far
// after each file. Throws `DataError` when a file cannot be read or has
// another size than listed, `std::runtime_error` when one cannot be
// written.
void copy_document_files(const Install::DocumentTree& tree, const std::vector<DocumentFile>& files,
                         const std::filesystem::path& dest, const std::function<void(std::uint64_t)>& progress);

}  // namespace sh
