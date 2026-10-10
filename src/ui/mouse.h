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

  int x() const { return x_; }
  int y() const { return y_; }
  std::uint8_t buttons() const { return buttons_; }

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

 private:
  EventQueue* events_;
  int width_;
  int height_;
  int x_ = 0;
  int y_ = 0;
  std::uint8_t buttons_ = 0;
  int frozen_ = 0;
  const Shp* sprites_ = nullptr;
  std::size_t frame_ = 0;
  int show_count_ = -1;
  std::optional<ClipRect> hide_rect_;
  // The saved background of the drawn cursor.
  bool drawn_ = false;
  ClipRect saved_rect_;
  std::vector<std::uint8_t> saved_;
};

}  // namespace sh::ui
