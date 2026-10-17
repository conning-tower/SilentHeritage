#include "platform/touch_press.h"

namespace sh {

bool TouchPress::at_side_edge(float x, float width, float scale) {
  const float edge = kEdgeDp * scale;
  return x < edge || x >= width - edge;
}

TouchPress::Kind TouchPress::down(bool in_screen, bool at_edge) {
  kind_ = !in_screen ? Kind::kDropped : at_edge ? Kind::kHeld : Kind::kPassed;
  finger_pending_ = kind_ != Kind::kPassed;
  dropped_finger_.reset();
  return kind_;
}

bool TouchPress::up(bool cancelled) {
  const bool tap = kind_ == Kind::kHeld && !cancelled;
  kind_ = Kind::kNone;
  return tap;
}

bool TouchPress::drop_finger_down(std::int64_t finger) {
  if (!finger_pending_) {
    return false;
  }
  finger_pending_ = false;
  dropped_finger_ = finger;
  return true;
}

bool TouchPress::drop_finger(std::int64_t finger, bool lifted) {
  if (dropped_finger_ != finger) {
    return false;
  }
  if (lifted) {
    dropped_finger_.reset();
  }
  return true;
}

}  // namespace sh
