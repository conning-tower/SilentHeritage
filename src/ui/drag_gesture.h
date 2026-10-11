// Pointer gestures of the extended variant (not in the original): the
// tracking of a press that becomes a drag past a threshold, with the
// pointer's speed, and the mapping of a horizontal drag onto a value (the
// precise dials). Free of the dialogs and of the game: the callers feed
// positions in screen pixels and GUI ticks (100 Hz).
#pragma once

#include <cstdint>
#include <deque>
#include <optional>

namespace sh::ui {

// The motion (in screen pixels) past which a press is a drag.
inline constexpr int kDragThreshold = 4;

// A press followed until its release: whether it became a drag, the motion
// since the press and the pointer's horizontal speed over the last 0.1 s.
class PointerDrag {
 public:
  // Starts following a press at (x, y) at GUI tick `tick`.
  void press(int x, int y, std::uint32_t tick);
  // Takes the pointer at (x, y); returns the horizontal motion (pixels)
  // since the previous position, 0 until the drag starts. The drag starts
  // when the pointer is farther than `kDragThreshold` from the press on
  // either axis.
  int move(int x, int y, std::uint32_t tick);
  // Forgets the press.
  void end();

  bool active() const { return active_; }
  bool dragging() const { return dragging_; }
  int press_x() const { return press_x_; }
  int press_y() const { return press_y_; }
  // The motion since the press.
  int dx() const { return x_ - press_x_; }
  int dy() const { return y_ - press_y_; }
  // The motion since the previous position, vertical (0 until the drag).
  int step_dy() const { return step_dy_; }
  // Returns the horizontal speed (pixels per second, signed) over the
  // positions of the last 10 ticks, 0 without two ticks apart.
  double speed_x(std::uint32_t tick) const;

 private:
  struct Sample {
    std::uint32_t tick;
    int x;
  };
  bool active_ = false;
  bool dragging_ = false;
  int press_x_ = 0, press_y_ = 0;
  int x_ = 0, y_ = 0;
  int step_dy_ = 0;
  std::deque<Sample> samples_;
};

// How a horizontal drag sets a value: `units_per_px` at a slow pointer,
// the value kept in `min`..`max` (wrapping round from `max` to `min` when
// `wrap`, e.g. tenths of a degree 0..3599), a multiple of `snap`.
struct ScrubScale {
  double units_per_px = 1;
  std::int32_t min = 0;
  std::int32_t max = 0;
  bool wrap = false;
  std::int32_t snap = 1;
};

// Returns the rate multiplier of a pointer moving at `px_per_s`: 1 up to
// 300 px/s, then in proportion, at most 20.
double scrub_acceleration(double px_per_s);
// Returns the rate multiplier of a pointer `dy` pixels above or below the
// press: 1 within 60 px, 1/4 within 120 px, else 1/16.
double scrub_precision(int dy);

// A value being set by a drag: the start value plus the drag's units.
class ScrubValue {
 public:
  ScrubValue(ScrubScale scale, std::int32_t start) : scale_(scale), start_(start), raw_(start) {}
  // Adds `units` (unsnapped); a clamped value does not run past its ends.
  void add(double units);
  // Returns the value: wrapped or clamped, rounded to the nearest
  // multiple of `snap`.
  std::int32_t value() const;
  std::int32_t start() const { return start_; }
  const ScrubScale& scale() const { return scale_; }

 private:
  ScrubScale scale_;
  std::int32_t start_;
  double raw_;
};

}  // namespace sh::ui
