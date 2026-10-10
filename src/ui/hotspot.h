// The global hotspot table of the dialog toolkit (0x108954, at most 120
// entries): the clickable rectangles and hotkeys that controls register,
// with hit testing, hotkey matching and the enable/disable counts.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sh::ui {

class Control;
class Dialog;

// Focus targets of a hotspot (+0x20); any other value redirects the focus
// to that hotspot index (relative to the dialog's first hotspot).
inline constexpr int kFocusSelf = -1;
inline constexpr int kFocusNone = -2;

// One 0x28-byte hotspot. The rectangle is inclusive and in screen
// coordinates; a rectangle of -1 makes a key-only hotspot.
struct Hotspot {
  int x0 = -1;
  int y0 = -1;
  int x1 = -1;
  int y1 = -1;
  // Bit 0: enabled; bits 1..: the disable count, 2 per disable.
  std::uint8_t flags = 1;
  Control* control = nullptr;
  // Which part of the control: 0 its body, 1/2 its arrows, 3/4 its arrow
  // keys, 5.. a track or grid cell + 5.
  int sub = 0;
  // 0xffXX matches the character XX case-insensitively, 0xfeXX the low
  // byte, any other value the exact BIOS key word.
  std::uint32_t key = 0;
  int focus = kFocusSelf;
  const Dialog* owner = nullptr;

  bool enabled() const { return (flags & 1) != 0; }
};

// Returns true when the BIOS key word `key` triggers `hotkey`
// (`gui_dialog_find_hotkey`, 0x3cabd).
bool hotkey_matches(std::uint32_t hotkey, std::uint16_t key);

// The hotspot table. Indexes are absolute; a dialog's hotspots are a
// contiguous run starting at `first_of(dialog)`.
class HotspotTable {
 public:
  static constexpr std::size_t kMaxHotspots = 120;

  std::size_t size() const { return hotspots_.size(); }
  Hotspot& operator[](int i) { return hotspots_.at(static_cast<std::size_t>(i)); }
  const Hotspot& operator[](int i) const { return hotspots_.at(static_cast<std::size_t>(i)); }

  // Appends `hotspot`; ignored when the table is full (`gui_add_hotspot`).
  void add(const Hotspot& hotspot);
  // Removes the last hotspot (`gui_pop_hotspot`).
  void pop();
  // Adds one disable count to hotspot `i` (`gui_hotspot_disable`).
  void disable(int i);
  // Removes one disable count; the hotspot is enabled again when none is
  // left (`gui_hotspot_enable`).
  void enable(int i);
  // Disables / enables every hotspot (`gui_disable_lower_windows`,
  // `gui_enable_lower_windows`): modal callbacks bracket their work with
  // them so that the windows below a new dialog stop reacting.
  void disable_all();
  void enable_all();

  // Returns the topmost enabled hotspot containing (x, y), or -1
  // (`gui_hit_test`, with its one-entry cache 0x108950).
  int hit_test(int x, int y);
  // Returns the hotspot of `control` with sub-index `sub` whose enabled bit
  // equals `enabled`, or -1 (`gui_find_hotspot`).
  int find(const Control* control, int sub, bool enabled) const;
  // Returns the first hotspot of `owner` whose flags share a bit with
  // `mask` and whose hotkey matches `key`, or -1.
  int find_hotkey(const Dialog* owner, std::uint16_t key, std::uint8_t mask) const;
  // Returns the index of the first hotspot of `owner`, or -1
  // (`gui_dialog_first_hotspot`).
  int first_of(const Dialog* owner) const;
  // Deletes every hotspot of `owner` (`gui_dialog_remove_hotspots`).
  void remove_owned_by(const Dialog* owner);

 private:
  std::vector<Hotspot> hotspots_;
  int hover_cache_ = -1;
};

}  // namespace sh::ui
