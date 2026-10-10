#include "game/screen_shake.h"

#include <algorithm>

#include "assets/game_exe.h"

namespace sh {

namespace {

// The display start rows of the shake steps 1..10 (int[10] after the
// armed flag 0x10d6d0).
constexpr std::uint32_t kShakeRows = 0x10d6d4;

}  // namespace

void ScreenShake::update(std::int32_t& timer) {
  // screen_shake_update (0x6e03c)
  if (!active_) {
    return;
  }
  if (armed_) {
    timer = 0;
    mark_ = 0;
    armed_ = false;
    return;
  }
  if (timer - mark_ >= kShakeStepTicks) {
    ++step_;
    mark_ = timer;
    row_ = game_exe().i32(kShakeRows + 4 * static_cast<std::uint32_t>(step_ - 1));
    if (step_ >= kShakeSteps) {
      step_ = 0;
      active_ = false;
      armed_ = true;
    }
  }
}

void ScreenShake::finish() {
  active_ = false;
  armed_ = true;
  step_ = 0;
  row_ = 0;
}

Image8 displayed_frame(const Image8& frame, std::int32_t row) {
  Image8 out(frame.width(), frame.height());
  out.fill(0);
  const int start = std::clamp(row, 0, frame.height());
  for (int y = start; y < frame.height(); ++y) {
    for (int x = 0; x < frame.width(); ++x) {
      out.set(x, y - start, frame.at(x, y));
    }
  }
  return out;
}

}  // namespace sh
