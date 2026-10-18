// Which touches press the emulated mouse button (not in the original, which
// had no touch screen). A touch starting where the game takes no pointer
// (the black bars, a wide screen's side fill beside a 640-wide station) is
// not a press: it would be clamped onto the frame's edge, where it would
// press whatever is there. A touch starting at a side edge of the
// window may be the system's back gesture (Android's gesture navigation),
// which the system takes over once the finger moves: its press is held
// until the finger lifts and dropped if the system cancelled the touch.
#pragma once

#include <cstdint>
#include <optional>

namespace sh {

class TouchPress {
 public:
  // The width of the system's back-gesture zone at each side edge, in
  // density-independent pixels (Android's default is 24, up to 40 at the
  // most sensitive setting).
  static constexpr float kEdgeDp = 32;

  // What becomes of the press of a touch.
  enum class Kind : std::uint8_t {
    kNone,     // no touch down
    kPassed,   // pressed now, as a mouse press
    kDropped,  // never pressed
    kHeld,     // pressed when the finger lifts, if not cancelled
  };

  // Whether window column `x` of a window `width` wide is in the back-gesture
  // zone, `scale` window units per density-independent pixel.
  static bool at_side_edge(float x, float width, float scale);

  // A touch went down, where the game takes the pointer or not (`in_screen`),
  // at a side edge or not. Returns what becomes of its press.
  Kind down(bool in_screen, bool at_edge);
  // What became of the press of the touch down, if any.
  Kind kind() const { return kind_; }
  // The touch lifted, or the system cancelled it (`cancelled`). Returns
  // whether its held press is to be made now, as a tap.
  bool up(bool cancelled);

  // The finger events of a touch whose press was not passed are dropped
  // too (the charts' finger scrolling): the first finger down after
  // `down` is that touch's. Returns whether the event of `finger` is
  // dropped; `lifted` for its last event.
  bool drop_finger_down(std::int64_t finger);
  bool drop_finger(std::int64_t finger, bool lifted);

 private:
  Kind kind_ = Kind::kNone;
  // Whether the finger of a touch not passed is still to be seen.
  bool finger_pending_ = false;
  std::optional<std::int64_t> dropped_finger_;
};

}  // namespace sh
