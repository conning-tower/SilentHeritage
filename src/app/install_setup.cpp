#include "app/install_setup.h"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <cstdint>
#include <format>
#include <span>
#include <stdexcept>
#include <utility>

#include "core/edition.h"
#include "core/error.h"

#ifdef SDL_PLATFORM_ANDROID
#include "app/android_documents.h"
#endif

namespace sh {

namespace {

struct Choice {
  int id;
  const char* text;
};

// Shows a message box with `choices` (the first is the Enter default, the
// last the Esc one); returns the id of the button pressed, -1 when the box
// is closed. Throws `std::runtime_error` when no message box can be shown.
int ask(SDL_MessageBoxFlags kind, const std::string& text, std::span<const Choice> choices) {
  std::vector<SDL_MessageBoxButtonData> buttons;
  for (std::size_t i = 0; i < choices.size(); ++i) {
    SDL_MessageBoxButtonFlags flags = 0;
    if (i == 0) {
      flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    }
    if (i + 1 == choices.size()) {
      flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    }
    buttons.push_back({.flags = flags, .buttonID = choices[i].id, .text = choices[i].text});
  }
  const std::string title = std::format("{}: game files", edition::kName);
  const SDL_MessageBoxData box{.flags = kind | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT,
                               .window = nullptr,
                               .title = title.c_str(),
                               .message = text.c_str(),
                               .numbuttons = static_cast<int>(buttons.size()),
                               .buttons = buttons.data(),
                               .colorScheme = nullptr};
  int pressed = -1;
  if (!SDL_ShowMessageBox(&box, &pressed)) {
    throw std::runtime_error(std::format("{}\n(no message box: {})", text, SDL_GetError()));
  }
  return pressed;
}

void tell(SDL_MessageBoxFlags kind, const std::string& text) {
  constexpr Choice kOk[] = {{0, "OK"}};
  ask(kind, text, kOk);
}

// Returns the problems followed by a blank line, or nothing.
std::string problem_text(const std::vector<std::string>& problems) {
  std::string out;
  for (const std::string& p : problems) {
    out += p;
    out += '\n';
  }
  if (!out.empty()) {
    out += '\n';
  }
  return out;
}

// Asks whether to choose the install folder; false to quit.
bool ask_to_choose(const std::string& lead) {
  constexpr Choice kChoices[] = {{1, "Choose folder..."}, {0, "Quit"}};
  return ask(SDL_MESSAGEBOX_INFORMATION,
             lead + edition::kName +
                 " needs the files of the original game, Silent Hunter v1.31 (SSI, the Commander's Edition): "
                 "the folder where it is installed, which holds SH.EXE and the DATA folder (an install copied "
                 "from another computer works too), or the game's CD: a copy of its files, or the folder holding "
                 "its image (.iso), which are then installed.\n\nChoose that folder?",
             kChoices) == 1;
}

// Asks to choose again after `problem` with a candidate folder; false to
// quit.
bool ask_again(const std::string& folder, const std::string& problem) {
  constexpr Choice kChoices[] = {{1, "Choose another folder..."}, {0, "Quit"}};
  return ask(SDL_MESSAGEBOX_WARNING,
             std::format("{} is not a Silent Hunter install: {}.\n\nChoose the folder holding SH.EXE and the "
                         "DATA folder, or the game's CD.",
                         folder, problem),
             kChoices) == 1;
}

constexpr std::uint64_t kMiB = 1024 * 1024;

// Returns the folder the media are installed into, beside `config_file`
// (in the app's internal storage on Android).
std::filesystem::path installed_media_dir(const std::filesystem::path& config_file) {
#ifdef SDL_PLATFORM_ANDROID
  if (const char* internal = SDL_GetAndroidInternalStoragePath()) {
    return std::filesystem::path(internal) / kInstalledMediaDir;
  }
#endif
  return config_file.parent_path() / kInstalledMediaDir;
}

// Asks whether to install `media` into `dest`.
bool ask_to_install(const GameMedia& media, const std::filesystem::path& dest) {
  constexpr Choice kChoices[] = {{1, "Install"}, {0, "Quit"}};
  return ask(SDL_MESSAGEBOX_INFORMATION,
             std::format("The Silent Hunter CD was found in {}.\n\nInstall: its {} files ({} MiB) are unpacked "
                         "into {} (it takes a moment); the CD or its image is then no longer needed.",
                         media.location, media.files.size(), media.total_size() / kMiB, dest.string()),
             kChoices) == 1;
}

// Unpacks `media` into the media folder and remembers it in `config_file`;
// returns the install. Throws `DataError` or `std::runtime_error` when it
// fails.
Install install_media(const GameMedia& media, const std::filesystem::path& config_file) {
  const std::filesystem::path dest = installed_media_dir(config_file);
  const std::uint64_t total = media.total_size();
  int logged = -10;
  install_game_media(media, dest, [&](std::uint64_t done) {
    const int percent = total == 0 ? 100 : static_cast<int>(done * 100 / total);
    if (percent / 10 != logged / 10) {
      logged = percent;
      SDL_Log("%s: installing the game files: %d%% (%llu of %llu MiB)", edition::kExecutable, percent,
              static_cast<unsigned long long>(done / kMiB), static_cast<unsigned long long>(total / kMiB));
    }
  });
  save_install_config(config_file, InstallSource{.kind = InstallSource::Kind::kCopy, .location = dest.string()});
  InstallCheck check = check_install_folder(dest);
  if (!check.install) {
    throw DataError(std::format("the files installed in {} are not a Silent Hunter install: {}", dest.string(),
                                check.problem));
  }
  tell(SDL_MESSAGEBOX_INFORMATION,
       std::format("Silent Hunter is installed in {}. The CD or image you chose is no longer needed.",
                   dest.string()));
  return std::move(*check.install);
}

// Offers to install the media of `check`; returns the install, nothing
// when the player quits. Throws `DataError` or `std::runtime_error` when the
// installation fails.
std::optional<Install> offer_media(const MediaCheck& check, const std::filesystem::path& config_file) {
  if (!ask_to_install(*check.media, installed_media_dir(config_file))) {
    return std::nullopt;
  }
  return install_media(*check.media, config_file);
}

#ifndef SDL_PLATFORM_ANDROID

// The outcome of SDL's folder dialog, set by its callback (on any thread).
struct FolderPick {
  std::atomic<bool> done = false;
  bool failed = false;
  std::string error;
  std::optional<std::string> folder;
};

void SDLCALL on_folder_picked(void* userdata, const char* const* files, int /*filter*/) {
  auto* pick = static_cast<FolderPick*>(userdata);
  if (files == nullptr) {
    pick->failed = true;
    pick->error = SDL_GetError();
  } else if (files[0] != nullptr) {
    pick->folder = files[0];
  }
  pick->done.store(true, std::memory_order_release);
}

// Shows the system folder dialog and waits for it; returns the folder
// chosen, nothing when cancelled. Throws `std::runtime_error` when no
// dialog is available.
std::optional<std::string> pick_folder(const std::string& start_folder) {
  if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) {
    throw std::runtime_error(std::format("SDL_InitSubSystem(EVENTS): {}", SDL_GetError()));
  }
  FolderPick pick;
  SDL_ShowOpenFolderDialog(on_folder_picked, &pick, nullptr, start_folder.empty() ? nullptr : start_folder.c_str(),
                           false);
  // The dialog runs on a thread of its own (Windows, zenity) or answers
  // through D-Bus messages read by the event pump (the desktop portal).
  while (!pick.done.load(std::memory_order_acquire)) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  SDL_QuitSubSystem(SDL_INIT_EVENTS);
  if (pick.failed) {
    throw std::runtime_error(std::format("no folder dialog is available: {}", pick.error));
  }
  return pick.folder;
}

#else

// Copies the files of `tree` to `dest` through a temporary folder beside
// it, replacing any previous copy; returns the copy's install. Throws
// `DataError` or `std::runtime_error` when it fails, leaving no temporary
// folder.
Install copy_tree(const Install::DocumentTree& tree, const std::vector<DocumentFile>& files,
                  const std::filesystem::path& dest) {
  std::uint64_t total = 0;
  for (const DocumentFile& f : files) {
    total += f.size;
  }
  const std::filesystem::path temp = dest.string() + ".tmp";
  std::filesystem::remove_all(temp);
  try {
    int logged = -10;
    copy_document_files(tree, files, temp, [&](std::uint64_t done) {
      const int percent = total == 0 ? 100 : static_cast<int>(done * 100 / total);
      if (percent / 10 != logged / 10) {
        logged = percent;
        SDL_Log("%s: copying the game files: %d%% (%llu of %llu MiB)", edition::kExecutable, percent,
                static_cast<unsigned long long>(done / kMiB), static_cast<unsigned long long>(total / kMiB));
      }
    });
    std::filesystem::remove_all(dest);
    std::filesystem::rename(temp, dest);
  } catch (const std::runtime_error&) {
    std::error_code ec;
    std::filesystem::remove_all(temp, ec);
    throw;
  }
  return Install(dest);
}

#endif

}  // namespace

std::optional<Install> run_media_install(const GameMedia& media, const std::filesystem::path& config_file) {
  if (!ask_to_install(media, installed_media_dir(config_file))) {
    return std::nullopt;
  }
  return install_media(media, config_file);
}

#ifndef SDL_PLATFORM_ANDROID

std::optional<Install> run_install_setup(const std::vector<std::string>& problems,
                                         const std::filesystem::path& config_file, const std::string& start_folder) {
  if (!ask_to_choose(problem_text(problems))) {
    return std::nullopt;
  }
  std::string start = start_folder;
  for (;;) {
    std::optional<std::string> folder;
    try {
      folder = pick_folder(start);
    } catch (const std::runtime_error& e) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", edition::kExecutable, e.what());
      tell(SDL_MESSAGEBOX_ERROR,
           std::format("{}.\n\nStart the game with --data <the folder of the original install>.", e.what()));
      return std::nullopt;
    }
    if (!folder) {
      return std::nullopt;
    }
    InstallCheck check = check_install_folder(*folder);
    if (check.install) {
      save_install_config(config_file, InstallSource{.kind = InstallSource::Kind::kFolder, .location = *folder});
      SDL_Log("%s: game files in %s (remembered in %s)", edition::kExecutable, folder->c_str(),
              config_file.string().c_str());
      return std::move(check.install);
    }
    const MediaCheck media = check_media_path(*folder);
    std::string problem = media.problem.empty() ? check.problem : media.problem;
    if (media.media) {
      try {
        return offer_media(media, config_file);
      } catch (const std::runtime_error& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: installation failed: %s", edition::kExecutable, e.what());
        problem = std::format("the installation failed: {}", e.what());
      }
    }
    if (!ask_again(*folder, problem)) {
      return std::nullopt;
    }
    start = *folder;
  }
}

Install open_install_source(const InstallSource& source) { return open_folder_source(source); }

#else

std::optional<Install> run_install_setup(const std::vector<std::string>& problems,
                                         const std::filesystem::path& config_file, const std::string& /*start*/) {
  const char* internal = SDL_GetAndroidInternalStoragePath();
  if (internal == nullptr) {
    throw std::runtime_error(std::format("no internal storage: {}", SDL_GetError()));
  }
  const std::filesystem::path copy_dir = std::filesystem::path(internal) / kInstalledMediaDir;
  std::string lead = problem_text(problems);
  for (;;) {
    if (!ask_to_choose(lead)) {
      return std::nullopt;
    }
    const std::optional<std::string> uri = pick_android_folder();
    if (!uri) {
      return std::nullopt;
    }
    std::shared_ptr<const Install::DocumentTree> tree;
    InstallCheck check = check_install([&] {
      tree = android_document_tree(*uri);
      return Install::document_tree(tree);
    });
    if (!check.install) {
      const MediaCheck media =
          tree ? check_media([&] { return document_media_tree(tree); }) : MediaCheck{};
      std::string problem = media.problem.empty() ? check.problem : media.problem;
      if (media.media) {
        try {
          return offer_media(media, config_file);
        } catch (const std::runtime_error& e) {
          SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: installation failed: %s", edition::kExecutable, e.what());
          problem = std::format("the installation failed: {}", e.what());
        }
      }
      if (!ask_again(*uri, problem)) {
        return std::nullopt;
      }
      lead.clear();
      continue;
    }
    const std::vector<DocumentFile> files = list_document_files(*tree);
    std::uint64_t total = 0;
    for (const DocumentFile& f : files) {
      total += f.size;
    }
    constexpr Choice kChoices[] = {{1, "Use in place"}, {2, "Copy"}, {0, "Quit"}};
    const int choice = ask(SDL_MESSAGEBOX_INFORMATION,
                           std::format("The game files were found.\n\nUse in place: they are read from the folder, "
                                       "which must stay on the device.\nCopy: {} files ({} MiB) are copied into "
                                       "the app's storage (it takes a moment); the folder may then be removed.",
                                       files.size(), total / kMiB),
                           kChoices);
    if (choice == 1) {
      std::error_code ec;
      std::filesystem::remove_all(copy_dir, ec);  // a copy of a previous setup
      save_install_config(config_file, InstallSource{.kind = InstallSource::Kind::kDocumentTree, .location = *uri});
      SDL_Log("%s: game files read in place from %s", edition::kExecutable, uri->c_str());
      return std::move(check.install);
    }
    if (choice != 2) {
      return std::nullopt;
    }
    try {
      Install copy = copy_tree(*tree, files, copy_dir);
      save_install_config(config_file,
                          InstallSource{.kind = InstallSource::Kind::kCopy, .location = copy_dir.string()});
      tell(SDL_MESSAGEBOX_INFORMATION,
           std::format("{} files ({} MiB) copied. The folder you chose is no longer needed.", files.size(),
                       total / kMiB));
      return copy;
    } catch (const std::runtime_error& e) {
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: copy failed: %s", edition::kExecutable, e.what());
      lead = std::format("The copy failed: {}\n\n", e.what());
    }
  }
}

Install open_install_source(const InstallSource& source) {
  if (source.kind == InstallSource::Kind::kDocumentTree) {
    return Install::document_tree(android_document_tree(source.location));
  }
  return open_folder_source(source);
}

#endif

}  // namespace sh
