#include "ui/dial_control.h"

#include <cstdlib>

#include "core/fixed_math.h"

namespace sh::ui {

int dial_bearing(int dx, int dy) {
  // heading_from_delta(dy - 1, dx - 1) is measured from the +x axis of the
  // screen towards +y; a quarter turn makes it a bearing from the top.
  const int angle = fx::heading_from_delta(dy - 1, dx - 1).tenths() / 10;
  const int bearing = angle + 90;
  return bearing > 359 ? angle - 270 : bearing;
}

bool DialControl::handle(Dialog& dialog, Msg msg, int, const Event& event) {
  // gui_dial_handle (0x176ea)
  switch (msg) {
    case Msg::kRegister: {
      const int x0 = x - kDialSize / 2;
      const int y0 = y - kDialSize / 2;
      add_control_hotspot(dialog, *this, {x0, y0, x0 + kDialSize, y0 + kDialSize}, 0, 0, kFocusNone);
      return true;
    }
    case Msg::kPress:
    case Msg::kRepeat: {
      const int bearing = dial_bearing(event.x - (dialog.x() + x), event.y - (dialog.y() + y));
      if (std::atoi(entry->text.c_str()) != bearing) {
        entry->text = std::to_string(bearing);
      }
      if (entry->on_done) {
        dialog.hotspots().disable_all();
        entry->on_done(entry->text, 0);
        dialog.hotspots().enable_all();
      }
      break;
    }
    case Msg::kRelease:
      return false;
    case Msg::kPaint:
      break;
    default:
      return true;
  }
  const Dial look{.x = x, .y = y, .second = second, .alternate_colour = disabled};
  dialog.add_dirty(dialog.skin().dial(dialog.gfx(), look, entry->text));
  if (on_change) {
    on_change(entry->text);
  }
  return true;
}

void DialControl::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

}  // namespace sh::ui
