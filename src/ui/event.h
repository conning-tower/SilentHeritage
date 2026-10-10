// The original input event records and their queue (`event_post`,
// `event_get`, `event_peek`, `event_flush`, 0x38e95..0x38ff1), and the GUI
// timer (`gui_get_ticks`, 100 Hz) with its timing constants.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace sh::ui {

// Returns the GUI tick count: 100 per second (`gui_get_ticks`, 0x408a5,
// counted by the AIL timer `gui_timer_tick`). Injected so that tests drive
// time explicitly.
using TickSource = std::function<std::uint32_t()>;

// Returns a tick source counting hundredths of a second from its creation,
// read from `milliseconds` (e.g. `Display::ticks_ms`).
TickSource make_tick_source(std::function<std::uint64_t()> milliseconds);

// Converts 100 Hz GUI ticks to BIOS timer ticks (0x46c, 1193182 / 65536 Hz),
// the clock that stamps the event records.
std::uint32_t bios_ticks(std::uint32_t gui_ticks);

// The GUI timing constants, in GUI ticks.
inline constexpr std::uint32_t kCaretBlinkTicks = 33;  // 0x10893c
inline constexpr std::uint32_t kAutoRepeatTicks = 16;  // 0x108940
inline constexpr std::uint32_t kDoubleClickTicks = 50; // 0x108944

// Bits of `Event::flags`.
enum EventFlag : std::uint16_t {
  kEventMoved = 0x01,
  kEventLeftDown = 0x02,
  kEventLeftUp = 0x04,
  kEventRightDown = 0x08,
  kEventRightUp = 0x10,
  kEventMiddleDown = 0x20,
  kEventMiddleUp = 0x40,
  kEventMouseMask = 0x7f,
  kEventKey = 0x8000,
};

// Mouse button bits as the INT 33h driver reports them (mouse events, byte
// +9 of the record).
enum MouseButtonBit : std::uint8_t {
  kButtonLeft = 1,
  kButtonRight = 2,
  kButtonMiddle = 4,
};

// Mouse button bits that `input_timer_isr` (0x39020) stores in key events.
enum KeyEventButtonBit : std::uint8_t {
  kKeyButtonLeft = 0x10,
  kKeyButtonRight = 0x20,
  kKeyButtonMiddle = 0x40,
};

// The BIOS keyboard flags byte (0x417) stored in every event.
enum BiosShiftBit : std::uint8_t {
  kShiftRight = 0x01,
  kShiftLeft = 0x02,
  kShiftCtrl = 0x04,
  kShiftAlt = 0x08,
  kShiftScrollLock = 0x10,
  kShiftNumLock = 0x20,
  kShiftCapsLock = 0x40,
  kShiftInsert = 0x80,
};

// Notable BIOS key words (`scan << 8 | ascii`).
enum BiosKey : std::uint16_t {
  kKeyEsc = 0x011b,
  kKeyBackspace = 0x0e08,
  kKeyTab = 0x0f09,
  kKeyShiftTab = 0x0f00,
  kKeyEnter = 0x1c0d,
  kKeySpace = 0x3920,
  kKeyHome = 0x4700,
  kKeyUp = 0x4800,
  kKeyPageUp = 0x4900,
  kKeyLeft = 0x4b00,
  kKeyRight = 0x4d00,
  kKeyEnd = 0x4f00,
  kKeyDown = 0x5000,
  kKeyPageDown = 0x5100,
  kKeyDelete = 0x5300,
  // The synthetic key of the event that activates a hotspot (posted by
  // `gui_dialog_handle_event` on Enter, Space or a hotkey).
  kKeyActivate = 0xffff,
};

class Dialog;

// One 24-byte event record: +0 flags, +4 BIOS key word, +8 BIOS shift
// state, +9 mouse buttons, +0xc / +0x10 mouse position, +0x14 BIOS tick
// stamp. The activation event (`key == kKeyActivate`) carries the hotspot
// and the dialog in +8 / +0xc instead: they are `hotspot` and `dialog`.
struct Event {
  std::uint16_t flags = 0;
  // +2, the upper half of the flags dword: 0x8000 marks the auto-repeat
  // press that the spin entry posts to itself.
  std::uint16_t flags_high = 0;
  std::uint16_t key = 0;
  std::uint8_t shift = 0;
  std::uint8_t buttons = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::uint32_t tick = 0;
  int hotspot = -1;                // activation event: absolute hotspot index
  const Dialog* dialog = nullptr;  // activation event: the owner dialog
  // An Esc key event that is Android's Back (no such key in the original):
  // a dialog with a back control activates it (`Dialog::set_back`), any
  // other code takes it as Esc.
  bool back = false;

  bool is_key() const { return (flags & kEventKey) != 0; }
};

// The ring of pending events (200 records at 0x105482).
class EventQueue {
 public:
  static constexpr std::size_t kCapacity = 200;

  // Events are stamped with `bios_ticks(ticks())`.
  explicit EventQueue(TickSource ticks);

  // Appends `event`, stamped with the current BIOS tick (`event_post`).
  // Returns false and counts a lost event when the ring is full.
  bool post(Event event);
  // Removes and returns the oldest event (`event_get`).
  std::optional<Event> get();
  // Returns the `n`-th pending event without removing it (`event_peek`).
  std::optional<Event> peek(std::size_t n = 0) const;
  // Drops every pending event (`event_flush`).
  void flush();

  // `event_queue_status`: the pending count and the lost-event counter.
  std::size_t size() const { return count_; }
  std::uint32_t lost() const { return lost_; }

 private:
  TickSource ticks_;
  std::array<Event, kCapacity> ring_{};
  std::size_t head_ = 0;  // next record to read
  std::size_t count_ = 0;
  std::uint32_t lost_ = 0;
};

}  // namespace sh::ui
