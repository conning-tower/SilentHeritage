// DISPLAY.CFG and the settings a launch starts with (core/display_config.h).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "core/display_config.h"

namespace sh {
namespace {

TEST_CASE("DISPLAY.CFG: key lines, the earlier single value, bad lines skipped", "[display]") {
  CHECK(parse_display_config("") == DisplayConfig{});
  // Written by earlier versions: the fullscreen state alone.
  CHECK(parse_display_config("1\n") == DisplayConfig{.fullscreen = true, .smoothing = {}});
  CHECK(parse_display_config("0\n") == DisplayConfig{.fullscreen = false, .smoothing = {}});
  CHECK(parse_display_config("fullscreen = 0\r\nsmoothing=1\n") ==
        DisplayConfig{.fullscreen = false, .smoothing = true});
  CHECK(parse_display_config("smoothing = 0\n") == DisplayConfig{.fullscreen = {}, .smoothing = false});
  CHECK(parse_display_config("# note\nfullscreen = yes\nscale = 3\nsmoothing = 0") ==
        DisplayConfig{.fullscreen = {}, .smoothing = false});
  const DisplayConfig both{.fullscreen = true, .smoothing = false};
  CHECK(format_display_config(both) == "fullscreen = 1\nsmoothing = 0\n");
  CHECK(format_display_config({.fullscreen = false, .smoothing = {}}) == "fullscreen = 0\n");
  CHECK(parse_display_config(format_display_config(both)) == both);
}

TEST_CASE("DISPLAY.CFG is written and read back; a missing file holds nothing", "[display]") {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_display_config_test";
  std::filesystem::remove_all(dir);
  const std::filesystem::path file = dir / "SAVE" / std::string(kDisplayConfigName);
  CHECK(read_display_config(file) == DisplayConfig{});
  const DisplayConfig config{.fullscreen = false, .smoothing = false};
  REQUIRE(write_display_config(file, config));
  CHECK(read_display_config(file) == config);
  std::ofstream(file, std::ios::trunc) << "1\n";
  CHECK(read_display_config(file) == DisplayConfig{.fullscreen = true, .smoothing = {}});
  std::filesystem::remove_all(dir);
}

TEST_CASE("A launch starts fullscreen in the extended variant, windowed in the classic one", "[display]") {
  // Nothing saved: the classic variant as before, windowed and smooth.
  CHECK(initial_display_settings({}, false, false) == DisplaySettings{.fullscreen = false, .smoothing = true});
  CHECK(initial_display_settings({}, true, false) == DisplaySettings{.fullscreen = true, .smoothing = true});
  // The saved settings win; the classic variant ignores the smoothing.
  const DisplayConfig saved{.fullscreen = false, .smoothing = false};
  CHECK(initial_display_settings(saved, true, false) == DisplaySettings{.fullscreen = false, .smoothing = false});
  CHECK(initial_display_settings(saved, false, false) == DisplaySettings{.fullscreen = false, .smoothing = true});
  CHECK(initial_display_settings({.fullscreen = true, .smoothing = {}}, false, false).fullscreen);
  // Android: always fullscreen.
  CHECK(initial_display_settings(saved, true, true).fullscreen);
  CHECK(initial_display_settings(saved, false, true).fullscreen);
}

}  // namespace
}  // namespace sh
