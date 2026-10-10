// Writes the Android launcher icon PNGs of every density (launcher_icon.h)
// into a resource directory, from the game's art or from a full-bleed
// foreground picture (the SH plate rendered from its SVG). Run by
// android/build_apk.sh; no icon is stored in the repository.
//
// Usage:
//   make_launcher_icon --res <dir> --install <game dir>
//   make_launcher_icon --res <dir> --foreground <square.pam> [--monochrome dark|light]
// The monochrome layer keeps the picture's dark parts for the game art and,
// by default, its light parts for a foreground.

#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "core/install.h"
#include "tools/launcher_icon.h"

namespace {

constexpr std::string_view kUsage =
    "usage: make_launcher_icon --res <dir> (--install <game dir> | --foreground <square.pam> "
    "[--monochrome dark|light])\n";

int usage() {
  std::cerr << kUsage;
  return 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::optional<std::filesystem::path> res;
  std::optional<std::filesystem::path> install_dir;
  std::optional<std::filesystem::path> foreground;
  std::optional<sh::MonochromeSource> mono;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (i + 1 >= argc) {
      return usage();
    }
    const std::string_view value = argv[++i];
    if (arg == "--res") {
      res = value;
    } else if (arg == "--install") {
      install_dir = value;
    } else if (arg == "--foreground") {
      foreground = value;
    } else if (arg == "--monochrome" && (value == "dark" || value == "light")) {
      mono = value == "dark" ? sh::MonochromeSource::kDarkness : sh::MonochromeSource::kLightness;
    } else {
      return usage();
    }
  }
  if (!res || install_dir.has_value() == foreground.has_value() || (install_dir && mono)) {
    return usage();
  }
  try {
    if (install_dir) {
      const sh::Install install(*install_dir);
      sh::write_launcher_icons(sh::game_art_foreground(install), sh::MonochromeSource::kDarkness, *res);
    } else {
      const sh::RgbaBitmap picture = sh::decode_pam(sh::read_file(*foreground), foreground->string());
      sh::write_launcher_icons(picture, mono.value_or(sh::MonochromeSource::kLightness), *res);
    }
  } catch (const std::exception& e) {
    std::cerr << "make_launcher_icon: " << e.what() << '\n';
    return 1;
  }
  return 0;
}
