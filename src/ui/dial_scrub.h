// The precise setting of dials (not in the original): a press on a dial
// shows a pending value (a ghost) at the current order, a horizontal drag
// moves it, the release gives it through the dial's own setter. A release
// without a drag is the original click. Escape or the right button during
// the drag cancels it, and the wheel over a dial moves its order one unit
// a notch (given 0.4 s after the last notch).
//
// A panel registers its dials as `ScrubTarget`s, passes its events to
// `DialScrubber::event` before its dialog (the events it takes must not
// reach the dialog) and calls `process` each frame; each dial's drawing
// asks `pending` for its ghost.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "assets/pal.h"
#include "gfx/text.h"
#include "gfx/viewport.h"
#include "ui/drag_gesture.h"
#include "ui/event.h"
#include "ui/skin.h"

namespace sh {
class Font;
struct SpriteFrame;
}  // namespace sh

namespace sh::ui {

// The wheel's pending value is given this many ticks after its last notch.
inline constexpr std::uint32_t kWheelCommitTicks = 40;

// A dial that takes a pending value.
struct ScrubTarget {
  // The dial's area in screen coordinates.
  Rect rect;
  ScrubScale scale;
  // Returns the order the ghost starts from.
  std::function<std::int32_t()> current;
  // Gives `value` (the dial's original setter); not called when the value
  // did not change.
  std::function<void(std::int32_t)> commit;
  // The original click at the press `event` (a release without a drag).
  std::function<void(const Event&)> tap;
  // The pending value appeared, changed or went: redraw the dial.
  std::function<void()> changed;
  // Whether the dial takes clicks now (none: always).
  std::function<bool()> enabled;
};

class DialScrubber {
 public:
  // Adds a dial; returns its index for `pending`.
  std::size_t add(ScrubTarget target);
  // Moves dial `index` to `rect` (screen coordinates).
  void set_rect(std::size_t index, Rect rect) { targets_.at(index).rect = rect; }
  // Forgets the dials (and a scrub in progress, without giving it).
  void clear();

  // Takes `event` at GUI tick `tick`; returns whether it belongs to a
  // scrub (then the panel's dialog must not see it).
  bool event(const Event& event, std::uint32_t tick);
  // Gives the wheel's pending value once it is `kWheelCommitTicks` old.
  void process(std::uint32_t tick);

  // The pending value of dial `index`, if it has one.
  std::optional<std::int32_t> pending(std::size_t index) const;
  // Whether a press, drag or wheel setting is in progress.
  bool active() const { return target_.has_value(); }
  // Whether the events are taken: a setting in progress, or the rest of a
  // cancelled press.
  bool capturing() const { return target_.has_value() || swallow_; }
  // Whether the pending value is from a drag past the threshold (the tap
  // shows no ghost).
  bool dragging() const { return drag_.dragging(); }
  // The press's screen position (the readout avoids the finger).
  int press_y() const { return drag_.press_y(); }

 private:
  enum class Mode : std::uint8_t { kPress, kWheel };
  std::optional<std::size_t> hit(int x, int y) const;
  void notify();
  void give();
  void cancel();

  std::vector<ScrubTarget> targets_;
  std::optional<std::size_t> target_;
  Mode mode_ = Mode::kPress;
  PointerDrag drag_;
  std::optional<ScrubValue> value_;
  Event press_;
  std::uint32_t wheel_tick_ = 0;
  // A cancelled press: its events are taken up to the buttons' release.
  bool swallow_ = false;
};

// Returns the colour of `palette` (6-bit) nearest to the 8-bit (r, g, b).
std::uint8_t nearest_colour(const VgaPalette& palette, int r, int g, int b);
// Returns the amber of the ghosts in `palette`.
std::uint8_t ghost_colour(const VgaPalette& palette);
// Returns `base` with the ink of DIGITS3.FNT (0xc0..0xcb) in `ink` and its
// cells (0xe0..0xef) in `back` (the digits of a pending value).
GlyphColourTable ghost_digit_colours(const GlyphColourTable& base, std::uint8_t ink, std::uint8_t back);
// Draws `frame` turned by `angle` (tenths) about (x, y) of `vp` with every
// opaque pixel in `colour` (the ghost of a needle or marker).
void draw_ghost_sprite(const Viewport& vp, const SpriteFrame& frame, int x, int y, std::int32_t angle,
                       std::uint8_t colour);
// Draws a 3-pixel-wide needle in `colour` from `inner` to `outer` pixels
// from (cx, cy) of `vp` at `angle` (tenths, clockwise from up).
void draw_ghost_needle(const Viewport& vp, int cx, int cy, std::int32_t angle, int inner, int outer,
                       std::uint8_t colour);
// Draws `text` in `font` in `ink` on a framed plate of colour `back`
// centred on column `cx` from row `y` of `vp` (the readout of a pending
// value); returns the plate's rectangle.
Rect draw_readout(const Viewport& vp, int cx, int y, const Font& font, const std::string& text, std::uint8_t ink,
                  std::uint8_t back);

}  // namespace sh::ui
