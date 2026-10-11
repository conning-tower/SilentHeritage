// Turning a station's view by dragging it (extended variant, not in the
// original): the sea follows the pointer (a drag to the left trains the
// view right) by the angle of one screen pixel at the view's
// magnification, faster for a fast pointer, and a fast release keeps the
// view turning while it slows down. A press that does not move past the
// threshold is left to the station's dialog (the original train areas);
// when the drag starts the turn is measured from the state at the press,
// so the original repeat's steps are undone.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/fixed.h"
#include "ui/drag_gesture.h"
#include "ui/event.h"
#include "ui/skin.h"

namespace sh {

class AttackServices;
namespace ui {
class Dialog;
}

// The release speed (screen pixels a second) past which the view keeps
// turning, and the time constant of its slowing down (seconds).
inline constexpr double kFlingSpeed = 600;
inline constexpr double kFlingTau = 0.35;
// The turning rate (tenths of a degree a second) under which it stops.
inline constexpr double kFlingStopRate = 20;

// Returns the tenths of a degree one screen pixel spans at magnification
// `zoom` (focal length 460 px at 1x).
double view_tenths_per_pixel(Fixed16 zoom);
// Returns the rate multiplier of a view drag at `px_per_s`: 1 up to
// `kFlingSpeed`, then in proportion, at most 4.
double view_drag_acceleration(double px_per_s);

class ViewDrag {
 public:
  struct Hooks {
    // The tenths of a degree one pixel turns the view now.
    std::function<double()> tenths_per_px;
    // Keeps the state the turn is measured from (called at the press).
    std::function<void()> capture;
    // Turns the view `across` tenths right and `up` tenths up from the
    // captured state (values past the view's limits are clamped by it).
    std::function<void(double across, double up)> turn;
    // Ends the station dialog's press without effect (the drag started).
    std::function<void()> release_dialog;
    // The cursor of a drag in progress (true) or back to the hover's.
    std::function<void(bool)> cursor;
    // The vertical rate (tenths per pixel, drag down turns up); 0: none.
    double up_tenths_per_px = 0;
    // Whether a fast release keeps the view turning.
    bool inertia = true;
    // Whether the view can be turned now (none: always).
    std::function<bool()> enabled;
  };

  explicit ViewDrag(Hooks hooks) : hooks_(std::move(hooks)) {}

  // The areas (screen coordinates) whose presses can drag.
  void set_regions(std::vector<ui::Rect> regions) { regions_ = std::move(regions); }
  // Takes `event` at GUI tick `tick`; returns whether it belongs to a drag
  // (then the station's dialog must not see it). A press is never taken.
  bool event(const ui::Event& event, std::uint32_t tick);
  // Turns the view on after a fast release, to GUI tick `tick`, and keeps
  // the drag's cursor (the dialog's hover resets it).
  void update(std::uint32_t tick);
  // Stops the drag or the turning.
  void stop();

  bool dragging() const { return drag_.dragging(); }
  bool turning() const { return rate_ != 0; }

 private:
  Hooks hooks_;
  std::vector<ui::Rect> regions_;
  ui::PointerDrag drag_;
  double across_ = 0;
  double up_ = 0;
  // The turning after the release (tenths a second) and its last tick.
  double rate_ = 0;
  std::uint32_t rate_tick_ = 0;
};

// Returns the hooks of a drag of the bridge, periscope or TBT: the view
// bearing (`PlayerCombat::view_bearing`) turned from the press, the TDC's
// bearing follow stopped as by the train areas; `zoom` null for the
// bridge (1x). `dialog()` (the station's dialog of the train areas, or
// null) receives the release; the cursor is MOVE while dragging.
ViewDrag::Hooks view_bearing_hooks(AttackServices& services, const Fixed16* zoom,
                                   std::function<ui::Dialog*()> dialog);

// Ends `dialog`'s press without activating anything (a release away from
// every hotspot).
void release_dialog_press(ui::Dialog& dialog);

}  // namespace sh
