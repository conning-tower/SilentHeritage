// The game's data directories, configured by `SHPATH.INI` in the install
// directory (`load_shpath_ini`, 0x50985).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sh {

class Install;

// The directories `SHPATH.INI` can set, in the order of the original's key
// comparisons.
enum class GameDir : std::uint8_t {
  kData,
  kSfx,
  kSpeech,
  kCines,
  kSave,
  kScen,
  kBud,
  kArt,
  kAlt,
  kCustom,
};
inline constexpr std::size_t kGameDirCount = 10;

// Returns the `SHPATH.INI` key of `dir` ("DATA", "SFX"...).
std::string_view game_dir_key(GameDir dir);

// The configured directories, as DOS paths relative to the install (or as
// written in the file) always ending with '\'. Each defaults to its key
// ("DATA\", "SFX\"...).
class ShPaths {
 public:
  ShPaths();

  // Parses the text of a `SHPATH.INI`: `KEY = path` read like the
  // original's `fscanf("%s = %s")` loop (whitespace-separated words, keys
  // compared case-insensitively, unknown keys ignored, a key lacking its
  // `= path` given the previous path); a '\' is appended to a path that
  // lacks one.
  static ShPaths parse(std::string_view text);
  // Reads `SHPATH.INI` from the install root, or returns the defaults when
  // the file does not exist.
  static ShPaths load(const Install& install);

  // Returns the directory of `dir`, e.g. "DATA\".
  const std::string& dir(GameDir dir) const { return dirs_[static_cast<std::size_t>(dir)]; }
  // Returns the directory without its trailing '\' (e.g. for `Install::list`).
  std::string_view dir_name(GameDir dir) const;
  // Returns the DOS path of `file` in `dir`, e.g. "DATA\WORLD-1.WLD".
  std::string path(GameDir dir, std::string_view file) const { return this->dir(dir) + std::string(file); }

 private:
  std::array<std::string, kGameDirCount> dirs_;
};

}  // namespace sh
