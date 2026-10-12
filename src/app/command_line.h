// The command line of the game's executable (app/main.cpp; `silent_heritage`
// or `silent_hunter` after the edition, core/edition.h):
//
//   silent_heritage [--data <install dir> | --setup] [--nointro | --editor] [--save <dir>] [--sharp]
//                   [--touch-panel] [--no-orders-button] [--classic] [--keys <k1,k2,...>] [--screenshot <file.bmp> [--frames N]]
//   silent_heritage [--data <install dir> | --setup] --viewer [--open <path>] [--keys ...] [--screenshot ...]
//
// Without `--data` (or when it is not an install) the game files are those
// remembered by the asset setup (app/install_setup.h), which runs when
// there are none or they are gone; `--setup` runs it again.
// Without `--viewer` the game starts (app/game_app.h): `--nointro` skips the
// intro videos like the original's `/nointro`, `--editor` starts the
// scenario editor instead of the front end (SE.EXE run from DOS: no File /
// Run), `--save` names the directory the SAVE files are written to
// (default: the player's preference directory), whose CUSTOM subdirectory
// receives the editor's scenarios. With `--viewer` the asset viewer runs
// instead: `--open Category/Item#frame` opens an item directly (e.g.
// "Sprites/3DSHIPS.CAT:ABOAS.SHP#2").
// `--sharp` scales the picture to the window with nearest-neighbour pixels
// (the original's sharp pixels) instead of the default bilinear smoothing,
// whatever the extended options page's SMOOTHING says.
// `--touch-panel` shows the floating command button of touch screens
// (always on under Android; platform/command_panel.h).
// `--no-orders-button` hides that floating button (Android included, where
// it is shown by default).
// `--window WxH` opens the window at that size, brought within the aspect
// ratios it may take (`window_aspect_range`); the wide-screen layouts
// follow its aspect.
// `--classic` runs the extended variant without its improvements, as the
// classic variant (core/features.h).
// `--keys` presses the given keys, one per frame: names up, down, left,
// right, pgup, pgdn, home, end, enter, esc, tab, space, backspace, comma,
// minus, period, kpplus, kpminus, f1..f12 or a single character (an
// upper-case letter adds Shift), optionally prefixed by "shift+" or
// "alt+"; "-" waits one frame and "-N" N frames; "click:X:Y" clicks the
// left button at (X, Y) (press and release on two frames), "rclick:X:Y"
// the right one, "drag:X1:Y1:X2:Y2" drags with the left button over 11
// frames, "move:X:Y" moves the mouse.
// `--screenshot` runs N frames (default 2) on SDL's dummy video and
// audio drivers with a 60 Hz virtual clock, saves the last frame as an 8-bit
// BMP and exits; with the wide screen, also the window as shown (the side
// fill around a 640-wide frame) as `<file>.window.bmp`.
#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "platform/display.h"

namespace sh {

// The usage line's options, after the program's name.
inline constexpr std::string_view kUsageOptions =
    "[--data <install dir> | --setup] "
    "[--viewer [--open <category/item#frame>] | --nointro | --editor] [--save <dir>] [--sharp] [--touch-panel] [--no-orders-button] [--window WxH] [--classic] [--keys <k1,k2,...>] [--screenshot <file.bmp> [--frames N]]";

struct CommandLine {
  std::optional<std::string> data_dir;
  bool setup = false;
  bool viewer = false;
  bool intro = true;
  bool editor = false;
  Scaling scaling = Scaling::kSmooth;
  bool touch_panel = false;
  bool no_orders_button = false;
  bool classic = false;
  // The window's size, 0 for the default.
  int window_width = 0;
  int window_height = 0;
  std::string save_dir;
  std::optional<std::string> open;
  std::vector<InputEvent> keys;
  std::optional<std::string> screenshot;
  int frames = 2;
};

// Parses the arguments after the program name. Throws `std::runtime_error`
// with the usage or the faulty value on a bad command line.
CommandLine parse_command_line(std::span<const std::string_view> args);

}  // namespace sh
