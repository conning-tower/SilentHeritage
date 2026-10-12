// The mouse of the original (module mouse, 0x590e4..0x5a259): its state as
// the INT 33h driver callback `mouse_update` maintains it, the input events
// it posts (with the keyboard events of `input_timer_isr`), and the
// software cursor drawn from CURSORS.SHP with its show / hide counter.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "assets/shp.h"
#include "core/platform.h"
#include "core/image.h"
#include "gfx/viewport.h"
#include "ui/event.h"

namespace sh::ui {

// Frames of CURSORS.SHP (40 frames; labels as drawn in the sprites).
enum class CursorId : std::uint8_t {
  kArrow = 0,
  kCross = 1,
  kBox = 2,
  kPointer = 3,  // the default cursor (`load_cursors`, the GUI config)
  kMove = 4,
  kWait = 5,
  kSet = 6,
  kRotateLeftFast = 7,   // "<< ROTATE LEFT" (train left)
  kRotateRightFast = 8,  // "ROTATE RIGHT >>" (train right)
  kRotateLeft = 9,
  kRotateRight = 10,
  kZoom = 11,
  kScopeUp = 12,
  kScopeDown = 13,
  kPreviousPage = 14,
  kNextPage = 15,
  kFire = 16,
  kUp = 17,
  kMark = 18,
  kSelect = 19,
  kTurn = 20,
  kPpi = 21,
  kAScope = 22,
  kOff = 23,
  kOpenBook = 24,
  kRaiseGun = 25,
  kLowerGun = 26,
  kStatus = 27,
  kCharts = 28,
  kBridge = 29,
  kGauges = 30,
  kRadar = 31,
  kScope = 32,
  kCaptainsCabin = 33,
  kTdc = 34,
  kStatusStation = 35,
  kShipIdBook = 36,
  kCalendar = 37,
  kLogBook = 38,
  kRadioLog = 39,
};

// A finger on a touch screen, in screen pixels (not in the original).
struct Touch {
  std::int64_t id = 0;
  int x = 0;
  int y = 0;
};

// How the pointer reaches the screen's edges, which scroll the charts (not
// in the original, whose edge band is `kBand`): held inside the picture
// (fullscreen), so the edge rows and columns themselves are reached; or a
// touch screen, whose pointer never scrolls a chart at an edge.
enum class PointerEdges : std::uint8_t { kBand, kExact, kNone };

// The mouse: position, buttons, event generation and the cursor.
class Mouse {
 public:
  // `mouse_init(width, height)`: the position is limited to the screen.
  Mouse(EventQueue& events, int width = kScreenWidth, int height = kScreenHeight);

  // The driver callback (`mouse_update`): takes the new position and
  // buttons (`MouseButtonBit`) and posts an event with the moved / pressed /
  // released bits, if any changed. `shift` is the BIOS shift state (0x417).
  // While the motion is frozen the position is not updated.
  void update(int x, int y, std::uint8_t buttons, std::uint8_t shift);
  // Posts a key event as `input_timer_isr` does for each BIOS keyboard word
  // `bios_word`: an extended key's 0xe0 ASCII byte is dropped (input mode
  // bit 0x1000 is never set by the game), and the mouse state is attached.
  void post_key(std::uint16_t bios_word, std::uint8_t shift);
  // Posts Android's Back: the Esc key event marked `Event::back`.
  void post_back(std::uint8_t shift);
  // Posts a wheel event of `notches` (up positive) at the pointer, with no
  // flags (not in the original, whose dialogs ignore it).
  void post_wheel(int notches, std::uint8_t shift);

  // Changes the screen the position is limited to (not in the original:
  // the wide-screen layouts), clamping the position.
  void set_bounds(int width, int height);
  // Lets the pointer reach columns `x0`..`x1` (beyond the screen's) on rows
  // `top` and below: the command bar drawn across a wide window over a
  // 640-wide screen (not in the original). Nothing for none.
  void set_band(std::optional<ClipRect> band);

  int x() const { return x_; }
  int y() const { return y_; }
  std::uint8_t buttons() const { return buttons_; }

  // The fingers on a touch screen, in the order they touched (not in the
  // original: the first one is also the left button). `touch` adds or
  // moves finger `id`, `lift` removes it.
  void touch(std::int64_t id, int x, int y);
  void lift(std::int64_t id);
  const std::vector<Touch>& touches() const { return touches_; }
  // Set by the host from the window's state (`kBand` by default).
  void set_edges(PointerEdges edges) { edges_ = edges; }
  PointerEdges edges() const { return edges_; }

  // `mouse_freeze_motion` / `mouse_unfreeze_motion` (counter 0x1248c0).
  void freeze_motion() { ++frozen_; }
  void unfreeze_motion() { --frozen_; }

  // Selects the cursor (`mouse_set_cursor`): frame `frame` of `sprites`,
  // drawn at the pointer with the frame's hotspot.
  void set_cursor(const Shp& sprites, std::size_t frame);
  void set_cursor(const Shp& sprites, CursorId id) { set_cursor(sprites, static_cast<std::size_t>(id)); }
  const Shp* cursor_sprites() const { return sprites_; }
  std::size_t cursor_frame() const { return frame_; }

  // The show counter 0x124838 (starts at -1, hidden): `mouse_show`
  // increments it unless it is 0, `mouse_hide` decrements it. The cursor is
  // drawn while it is 0.
  void show();
  void hide();
  int show_count() const { return show_count_; }
  bool shown() const { return show_count_ == 0; }

  // `mouse_set_hide_rect`: the cursor is not drawn while it overlaps
  // `rect` (screen coordinates); nothing for no rectangle.
  void set_hide_rect(std::optional<ClipRect> rect) { hide_rect_ = rect; }

  // Returns the cursor rectangle clipped to the screen when shown
  // (`mouse_get_cursor_rect`).
  std::optional<ClipRect> cursor_rect() const;

  // Draws the cursor onto `frame` (`mouse_draw_cursor`) when shown and not
  // over the hide rectangle, saving the pixels under it; `erase` puts them
  // back (`mouse_erase_cursor`). Call `draw` before presenting a frame and
  // `erase` after.
  void draw(Image8& frame);
  void erase(Image8& frame);

  // Whether `draw` draws the cursor at all (not in the original): not on
  // Android, where a finger points. The cursor's state is kept either way.
  bool draws_cursor() const { return draws_cursor_; }
  void set_draws_cursor(bool draws) { draws_cursor_ = draws; }

 private:
  // Returns the key event of `bios_word` with the mouse state.
  Event key_event(std::uint16_t bios_word, std::uint8_t shift) const;

  EventQueue* events_;
  int width_;
  int height_;
  std::optional<ClipRect> band_;
  int x_ = 0;
  int y_ = 0;
  std::uint8_t buttons_ = 0;
  std::vector<Touch> touches_;
  PointerEdges edges_ = PointerEdges::kBand;
  int frozen_ = 0;
  const Shp* sprites_ = nullptr;
  std::size_t frame_ = 0;
  int show_count_ = -1;
  std::optional<ClipRect> hide_rect_;
  bool draws_cursor_ = !kAndroid;
  // The saved background of the drawn cursor.
  bool drawn_ = false;
  ClipRect saved_rect_;
  std::vector<std::uint8_t> saved_;
};

}  // namespace sh::ui
