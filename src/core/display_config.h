// The window's settings (not in the original) and DISPLAY.CFG, the file in
// the save directory keeping them: `key = value` lines `fullscreen = 0|1`
// and `smoothing = 0|1`. A file of a single `0` or `1` line (written by
// earlier versions) holds the fullscreen state alone. Unknown lines and
// values are skipped.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace sh {

inline constexpr std::string_view kDisplayConfigName = "DISPLAY.CFG";

// How the window shows the picture.
struct DisplaySettings {
  bool fullscreen = false;
  // Bilinear scaling of the picture, else nearest neighbour.
  bool smoothing = true;

  friend bool operator==(const DisplaySettings&, const DisplaySettings&) = default;
};

// The settings of the extended options page: the current ones, and those
// the player cannot change (shown greyed).
struct DisplayOptions {
  DisplaySettings settings;
  // Always fullscreen (Android).
  bool fullscreen_fixed = false;
  // Sharp scaling forced for this run (`--sharp`).
  bool smoothing_fixed = false;
};

// The settings a DISPLAY.CFG holds, each absent when the file has none.
struct DisplayConfig {
  std::optional<bool> fullscreen;
  std::optional<bool> smoothing;

  friend bool operator==(const DisplayConfig&, const DisplayConfig&) = default;
};

// Returns the settings of DISPLAY.CFG's text `text`.
DisplayConfig parse_display_config(std::string_view text);
// Returns the text of `config`: a line for each setting present.
std::string format_display_config(const DisplayConfig& config);
// Returns the settings of `file`, none without a readable file.
DisplayConfig read_display_config(const std::filesystem::path& file);
// Writes `config` to `file`, creating its directory. Returns false when it
// cannot be written.
bool write_display_config(const std::filesystem::path& file, const DisplayConfig& config);

// Returns the settings a launch starts with: those of `saved` over the
// defaults, which are windowed and smooth. With `extended` (the extended
// variant's settings), the window is fullscreen and smoothing is read from
// `saved` too; otherwise smoothing stays on (`--sharp` aside). With
// `always_fullscreen` (Android), fullscreen.
DisplaySettings initial_display_settings(const DisplayConfig& saved, bool extended, bool always_fullscreen);

}  // namespace sh
