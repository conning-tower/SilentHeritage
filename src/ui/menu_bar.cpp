#include "ui/menu_bar.h"

#include <cstddef>

namespace sh::ui {

namespace {

// The hotspot owner of the bar's titles.
class BarOwner final : public Control {
 public:
  bool handle(Dialog&, Msg, int, const Event&) override { return false; }
};

}  // namespace

void menu_bar_run(Gui& gui, MenuHooks& hooks, const MenuBar& bar) {
  HotspotTable& hotspots = gui.hotspots();
  Image8& screen = gui.screen();
  hotspots.disable_all();
  if (gui.dialog_cursor_sprites() != nullptr) {
    gui.mouse().set_cursor(*gui.dialog_cursor_sprites(), gui.dialog_cursor_frame());
  }
  gui.events().flush();

  // The bar is drawn into a buffer copied to the screen; the screen under
  // it is saved first.
  const int w = hooks.menu_bar_width();
  const int h = hooks.menu_bar_height() + (bar.shadow ? hooks.shadow() : 0);
  Image8 buffer(w, h);
  Image8 saved(w, h);
  for (int py = 0; py < h; ++py) {
    for (int px = 0; px < w; ++px) {
      if (screen.contains(px, py)) {
        saved.set(px, py, screen.at(px, py));
      }
    }
  }
  const Viewport gfx = Viewport::full(buffer);
  BarOwner owner;
  const auto draw_item = [&](std::size_t n, bool highlighted, bool add_hotspot) {
    // gui_menu_bar_draw_item (0x14aca)
    const MenuBarItem& item = bar.items[n];
    const Rect r = hooks.menu_bar_item(gfx, item.x, item, highlighted);
    if (add_hotspot) {
      hotspots.add({.x0 = r.x0,
                    .y0 = r.y0,
                    .x1 = r.x1,
                    .y1 = r.y1,
                    .flags = 1,
                    .control = &owner,
                    .sub = static_cast<int>(n),
                    .key = item.key,
                    .focus = kFocusSelf,
                    .owner = nullptr});
    }
  };
  // gui_menu_bar_draw (0x14a8d)
  hooks.menu_bar(gfx, bar.shadow);
  for (std::size_t n = 0; n < bar.items.size(); ++n) {
    draw_item(n, false, true);
  }
  const auto owned = [&](int hotspot) {
    return hotspot >= 0 && hotspot < static_cast<int>(hotspots.size()) && hotspots[hotspot].control == &owner;
  };
  const auto item_of = [&](int hotspot) { return static_cast<std::size_t>(hotspots[hotspot].sub); };
  const int first = hotspots.find(&owner, 0, true);
  const int last = hotspots.find(&owner, static_cast<int>(bar.items.size()) - 1, true);

  int current = -1;      // the toolkit's current hotspot (0x9bb3c)
  int previous = -1;     // local_44
  bool refresh = true;   // local_38: the buffer goes to the screen
  int opening = 0;       // iVar5: the highlighted title's popup is open
  bool done = false;     // local_24
  std::uint8_t shift = 0;  // the BIOS shift state of the last event (0x417)
  while (!done) {
    std::uint16_t result = 0;
    Event e;
    bool have_event = false;
    while (true) {
      result = 0;
      if (current != previous && owned(current)) {
        if (owned(previous)) {
          draw_item(item_of(previous), false, false);
        }
        refresh = true;
        draw_item(item_of(current), true, false);
      }
      if (refresh) {
        refresh = false;
        screen.blit(buffer, 0, 0);
      }
      if (opening != 0 && owned(current)) {
        const int title = current;
        const MenuBarItem& item = bar.items[item_of(title)];
        hotspots.disable(title);
        int moved_to = title;
        result = static_cast<std::uint16_t>(
            popup_menu_run(gui, hooks, *item.menu, item.x, hooks.menu_bar_height(), bar.shadow, &moved_to));
        hotspots.enable(title);
        if (result == 0) {
          // The pointer moved onto another title: its popup opens next.
          draw_item(item_of(title), false, false);
          refresh = true;
          current = moved_to;
          if (owned(current)) {
            draw_item(item_of(current), true, false);
          } else {
            opening = 0;
          }
        } else {
          current = title;
        }
      } else if (opening != 0) {
        opening = 0;
      }
      previous = current;
      if (current == -1 && (shift & kShiftAlt) != 0) {
        current = first;
      }
      if (result != 0) {
        break;
      }
      if (const std::optional<Event> next = gui.events().get()) {
        e = *next;
        have_event = true;
        break;
      }
      gui.pump();
    }
    if (!have_event) {
      // The key that closed the popup.
      e = Event{};
      e.flags = kEventKey;
      e.key = result;
    } else {
      shift = e.shift;
    }
    if (e.is_key()) {
      switch (e.key) {
        case kKeyEsc:
          if (opening != 0) {
            opening = 0;
          } else {
            done = true;
          }
          break;
        case kKeyEnter:
        case kKeyDown:
          if (current != -1) {
            ++opening;
          }
          break;
        case kKeyLeft:
          if (current != -1) {
            if (current <= first) {
              current = last + 1;
            }
            --current;
          }
          break;
        case kKeyRight:
          if (current != -1) {
            ++current;
            if (current > last) {
              current = first;
            }
          }
          break;
        case kKeyActivate:
          // An item ran, or a right click closed the popup.
          done = true;
          break;
        default: {
          // A title's hotkey opens it; any other key changes nothing.
          int found = -1;
          for (int i = 0; i < static_cast<int>(hotspots.size()); ++i) {
            if (hotspots[i].enabled() && hotspots[i].key != 0 && hotkey_matches(hotspots[i].key, e.key)) {
              found = i;
              break;
            }
          }
          if (found == -1) {
            current = previous;
          } else {
            current = found;
            ++opening;
          }
          break;
        }
      }
    }
    if ((e.flags & (kEventMoved | kEventLeftDown)) != 0 && (e.buttons & kButtonLeft) != 0) {
      const int hit = hotspots.hit_test(e.x, e.y);
      if (hit == -1) {
        current = previous;
      } else {
        current = hit;
        ++opening;
      }
    }
    if ((e.flags & kEventRightUp) != 0) {
      done = true;
    }
  }
  for (std::size_t n = 0; n < bar.items.size(); ++n) {
    hotspots.pop();
  }
  screen.blit(saved, 0, 0);
  gui.events().flush();
  hotspots.enable_all();
}

}  // namespace sh::ui
