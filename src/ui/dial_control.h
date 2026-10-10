// The heading dial control of the scenario editor's chart panels (SE vtable
// 0x97f04, `gui_dial_handle` 0x176ea; the bearing dial SH carries unused):
// pressing or dragging on the dial sets its linked entry to the bearing of
// the pointer from the dial's centre and runs the entry's `on_done(text, 0)`.
// The editor's dials all set +0x24, so a press never redraws the linked
// entry itself (`gui_text_entry_draw_text`): `on_done` refreshes whatever
// shows the value.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "ui/controls.h"

namespace sh::ui {

// The dial's hotspot: `kDialSize` pixels square around the centre.
inline constexpr int kDialSize = 59;  // the dial metrics 0x9bc70 / 0x9bc72

// Returns the bearing in degrees (0..359, 0 up, clockwise) that a press at
// (`dx`, `dy`) pixels from the dial's centre sets.
int dial_bearing(int dx, int dy);

class DialControl : public Control {
 public:
  // The dial centred at (`x`, `y`) (dialog coordinates) shows and sets the
  // decimal text of `entry`, which must outlive it; the entry need not be
  // a control of the dialog.
  DialControl(int x, int y, TextEntry& entry) : x(x), y(y), entry(&entry) {}

  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y;                     // +0x10, +0x14: the centre
  TextEntry* entry;             // +0x18: the linked entry (its text +0x20, on_done +0x24)
  const int* second = nullptr;  // +0x1c: a second needle's bearing, or none
  // +0x20: no clicks; the skin draws the needle in its alternate colour.
  bool disabled = false;
  // +0x28: called with the entry's text after every redraw.
  std::function<void(const std::string&)> on_change;
  HoverFn on_hover;  // +0x2c
};

}  // namespace sh::ui
