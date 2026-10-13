// Whether the system moves the pointer when asked to (not in the original,
// whose mouse driver always did): a Wayland compositor may ignore the
// request, and SDL reports nothing of it.
#pragma once

#include <optional>

namespace sh {

// A point of the window, in window coordinates.
struct WindowPoint {
  float x = 0;
  float y = 0;
};

// Judges the warps of the system pointer from the positions reported after
// them. A warp is honoured when a report lands nearer its target than its
// start; it is refused when its first `kReports` reports, or all those
// before the next warp, land nearer its start. Reports at the target itself
// are SDL's own echo of the warp and prove nothing. The warps are known to
// be refused after `kRefusals` refused warps in a row: a user moving fast
// against the warp can make one honoured warp look refused, not several in
// a row.
class WarpCheck {
 public:
  static constexpr int kReports = 4;
  static constexpr int kRefusals = 3;
  // Warps shorter than this (window coordinates) are not judged, nor is a
  // report within `kEcho` of the target.
  static constexpr float kMinDistance = 4;
  static constexpr float kEcho = 1;

  // The system pointer was asked to move from `from` to `to`.
  void warped(WindowPoint from, WindowPoint to);
  // The pointer was reported at `at`, after the last warp.
  void reported(WindowPoint at);
  // The pointer left the window: the warp being judged is forgotten.
  void left() { watch_.reset(); }
  // Whether the warps are known to be refused (for good).
  bool refused() const { return refused_; }

 private:
  // Counts the warp being judged as refused, and forgets it.
  void refuse();

  struct Watch {
    WindowPoint from;
    WindowPoint to;
    int reports = 0;
  };
  std::optional<Watch> watch_;
  int refusals_ = 0;
  bool refused_ = false;
};

}  // namespace sh
