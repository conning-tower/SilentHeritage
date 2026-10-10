// The screen shake of hits, close blasts and groundings
// (`screen_shake_update` 0x6e03c, step 1 of `mission_main_loop`): the
// original moves the VESA display start (int 10h AX=4F07) down the video
// memory and back, so the picture jumps up by a few rows.
#pragma once

#include <cstdint>

#include "core/image.h"

namespace sh {

// The number of shake steps; the tenth ends the shake.
inline constexpr std::int32_t kShakeSteps = 10;
// The game timer ticks between steps (more than 5).
inline constexpr std::int32_t kShakeStepTicks = 6;

class ScreenShake {
 public:
  // Starts a shake (sets 0x126d74); a shake already running goes on.
  void trigger() { active_ = true; }
  // Runs one frame's `screen_shake_update` with the game timer 0x129be4
  // (`timer`, which the first frame of a shake resets to 0): every
  // `kShakeStepTicks` ticks the display start moves to the next row of
  // SH.EXE's table (`game_exe`), the tenth step ending the shake.
  void update(std::int32_t& timer);
  // Ends the shake at once, with the picture back in place (the original
  // waits for it before the GAME OVER box).
  void finish();

  bool active() const { return active_; }
  // The display start row (0: in place).
  std::int32_t display_start() const { return row_; }

 private:
  bool active_ = false;    // 0x126d74
  bool armed_ = true;      // 0x10d6d0
  std::int32_t step_ = 0;  // 0x126d78
  std::int32_t mark_ = 0;  // 0x126d7c: the timer at the last step
  std::int32_t row_ = 0;   // the display start set last
};

// Returns `frame` as the screen shows it from display start row `row`:
// its rows from `row` on at the top, then `row` rows of colour 0 (the
// video memory past the picture, approximated as cleared).
Image8 displayed_frame(const Image8& frame, std::int32_t row);

}  // namespace sh
