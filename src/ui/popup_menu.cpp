#include "ui/popup_menu.h"

#include <array>

#include "gfx/text.h"

namespace sh::ui {

// --- The default hooks ------------------------------------------------------

Rect DefaultMenuHooks::menu_bar(const Viewport& gfx, bool fill) {
  // gui_draw_menu_bar (0x408b5): the fill covers the shadow below the bar.
  const SkinMetrics& m = metrics();
  Rect filled;
  if (fill) {
    gui_fill_rect(gfx, 0, 0, m.menu_bar_width, m.menu_bar_height + m.border, res_.colours.b[0]);
    filled = {0, 0, m.menu_bar_width - 1, m.menu_bar_height + m.border - 1};
  }
  const Rect frame = bevel_frame(gfx, 0, 0, m.menu_bar_width, m.menu_bar_height, 3, 3, false);
  return fill ? filled : frame;
}

Rect DefaultMenuHooks::menu_bar_item(const Viewport& gfx, int x, const MenuBarItem& item, bool highlighted) {
  // gui_draw_menu_bar_item (0x4093a)
  const Font& font = res_.small;
  const Rect r{x + 5, 0, x + 14 + font.text_width(item.label), metrics().menu_bar_height - 1};
  gui_fill_rect(gfx, r.x0, r.y0 + 6, r.x1 - r.x0 + 1, r.y1 - r.y0 - 12,
                highlighted ? res_.colours.a[1] : res_.colours.b[1]);
  gui_draw_text(gfx, x + 10, 8, font, item.label, &res_.table);
  return r;
}

Rect DefaultMenuHooks::popup_frame(const Viewport& gfx, int x, int y, int h, bool shadow) {
  // gui_draw_popup_frame (0x40a0a)
  return bevel_frame(gfx, x, y, metrics().popup_width, h, 3, 3, shadow);
}

Rect DefaultMenuHooks::popup_item(const Viewport& gfx, int x, int y, int n, const PopupItem& item, bool highlighted) {
  // gui_draw_popup_item (0x40a31)
  const int width = metrics().popup_width;
  const int iy = n * 24 + 9;
  raised_box(gfx, 7, iy, width - 36, 20, false, highlighted);
  const Font& font = res_.small;
  gui_draw_text(gfx, 11, iy + 5, font, item.label, &res_.table);
  if (!item.shortcut.empty()) {
    gui_draw_text(gfx, width - 0x1d - font.text_width(item.shortcut) - 3, iy + 5, font, item.shortcut, &res_.table);
  }
  const int mark_x = width - 36 + 7 + 2;
  if (item.type == PopupItemType::kSubmenu) {
    submenu_arrow(gfx, mark_x, iy, highlighted);
  } else if (item.type == PopupItemType::kToggle) {
    check_box(gfx, mark_x, iy, false, item.toggle(false), false, false, {});
  }
  const int x0 = x + 5;
  const int y0 = y + iy;
  return {x0, y0, x0 + width - 19, y0 + 19};
}

int DefaultMenuHooks::popup_height(int count) {
  // gui_popup_item_y (0x40b78)
  return count * 24 + 14;
}

Point DefaultMenuHooks::submenu_origin(int n, int x, int y) {
  // gui_popup_submenu_origin (0x40b89)
  return {x + 7 + metrics().popup_width - 36 + 14, y + 9 + n * 24};
}

void DefaultMenuHooks::submenu_arrow(const Viewport& gfx, int x, int y, bool highlighted) {
  // gui_draw_submenu_arrow (0x40bbd)
  const GuiColours& c = res_.colours;
  const auto& set = highlighted ? c.a : c.b;
  fill_triangle(gfx, {x, y}, {x + 9, y + 9}, {x, y + 18}, set[1]);
  draw_line(gfx, x, y, x, y + 18, set[3]);
  draw_line(gfx, x, y, x + 9, y + 9, set[3]);
  draw_line(gfx, x + 9, y + 9, x, y + 18, set[2]);
}

// --- gui_popup_menu_run -----------------------------------------------------

namespace {

// The hotspot owner of an open popup: the original stores the menu itself
// in the hotspot's owner slot.
class PopupOwner final : public Control {
 public:
  bool handle(Dialog&, Msg, int, const Event&) override { return false; }
};

// The state of a chain of open popups: the current hotspot is the
// toolkit's global 0x108950, shared with the submenus.
class PopupRun {
 public:
  PopupRun(Gui& gui, MenuHooks& hooks) : gui_(gui), hooks_(hooks) {}

  int run(const PopupMenu& menu, int x, int y, bool shadow);
  int current() const { return current_; }

 private:
  int select(const PopupMenu& menu, int x, int y, bool shadow);
  bool owns(const PopupOwner& owner, int hotspot) const {
    return hotspot >= 0 && hotspot < static_cast<int>(gui_.hotspots().size()) &&
           gui_.hotspots()[hotspot].control == &owner;
  }

  Gui& gui_;
  MenuHooks& hooks_;
  int current_ = -1;  // 0x108950
};

int PopupRun::run(const PopupMenu& menu, int x, int y, bool shadow) {
  HotspotTable& hotspots = gui_.hotspots();
  Image8& screen = gui_.screen();
  const int extra = shadow ? hooks_.shadow() : 0;
  const int h = hooks_.popup_height(static_cast<int>(menu.items.size()));
  const int w = hooks_.popup_width() + extra;
  const int full_h = h + extra;
  // The drawing buffer and the saved screen, both starting as the screen
  // under the popup.
  Image8 buffer(w, full_h);
  for (int py = 0; py < full_h; ++py) {
    for (int px = 0; px < w; ++px) {
      if (screen.contains(x + px, y + py)) {
        buffer.set(px, py, screen.at(x + px, y + py));
      }
    }
  }
  const Image8 saved = buffer;
  const Viewport gfx = Viewport::full(buffer);
  PopupOwner owner;

  const auto draw_item = [&](int n, bool highlighted, bool add_hotspot) {
    // gui_popup_menu_draw_item (0x3bc0f)
    const PopupItem& item = menu.items[static_cast<std::size_t>(n)];
    const Rect r = hooks_.popup_item(gfx, x, y, n, item, highlighted);
    if (add_hotspot) {
      hotspots.add({.x0 = r.x0,
                    .y0 = r.y0,
                    .x1 = r.x1,
                    .y1 = r.y1,
                    .flags = 1,
                    .control = &owner,
                    .sub = n,
                    .key = item.key,
                    .focus = kFocusSelf,
                    .owner = nullptr});
    }
  };
  const auto draw_menu = [&](bool add_hotspots) {
    // gui_popup_menu_draw (0x3bbba)
    hooks_.popup_frame(gfx, 0, 0, h, shadow);
    for (int n = 0; n < static_cast<int>(menu.items.size()); ++n) {
      draw_item(n, false, add_hotspots);
    }
  };
  const auto sub_of = [&](int hotspot) { return hotspots[hotspot].sub; };

  draw_menu(true);
  draw_item(0, true, false);
  current_ = hotspots.find(&owner, 0, true);
  int previous = current_;  // local_20
  const int first = current_;
  const int last = hotspots.find(&owner, static_cast<int>(menu.items.size()) - 1, true);
  bool done = false;
  bool redraw = true;  // local_28
  int selects = 0;     // local_3c
  int pending = 0;     // local_10
  while (true) {
    // The top of the loop: the highlight follows the current hotspot, the
    // buffer goes to the screen, a selected item runs.
    while (true) {
      if (done) {
        break;
      }
      pending = 0;
      if (owns(owner, current_)) {
        if (current_ != previous) {
          draw_item(sub_of(previous), false, false);
          redraw = true;
          draw_item(sub_of(current_), true, false);
        }
      } else {
        done = true;
      }
      if (redraw) {
        redraw = false;
        screen.blit(buffer, x, y);
      }
      if (selects != 0 && !done) {
        pending = select(menu, x, y, shadow);
        draw_menu(false);
        draw_item(sub_of(current_), true, false);
        const auto key = static_cast<std::uint16_t>(pending);
        if (key == kKeyEsc || key == kKeyLeft) {
          // A submenu closed by Esc or Left: this one stays open.
          pending = 0;
        } else if (key == kKeyRight) {
          // Right in a submenu without a submenu of its own goes up the
          // chain (a menu bar would open its next title).
          current_ = -1;
        }
        redraw = true;
        selects = 0;
      }
      previous = current_;
      if (done) {
        continue;
      }
      if (static_cast<std::uint16_t>(pending) != 0) {
        break;
      }
      if (gui_.events().peek()) {
        break;
      }
      gui_.pump();
    }
    if (done) {
      break;
    }
    Event e;
    if (static_cast<std::uint16_t>(pending) != 0) {
      e.flags = kEventKey;
      e.key = static_cast<std::uint16_t>(pending);
    } else {
      e = *gui_.events().get();
    }
    if (e.is_key()) {
      pending = e.key;
      switch (e.key) {
        case kKeyEsc:
        case kKeyLeft:
        case kKeyActivate:
          done = true;
          break;
        case kKeyEnter:
          ++selects;
          break;
        case kKeyUp:
          if (current_ <= first) {
            current_ = last + 1;
          }
          --current_;
          break;
        case kKeyDown:
          ++current_;
          if (current_ > last) {
            current_ = first;
          }
          break;
        case kKeyRight:
          if (current_ != -1 && menu.items[static_cast<std::size_t>(sub_of(current_))].type == PopupItemType::kSubmenu) {
            ++selects;
          } else {
            done = true;
          }
          break;
        default: {
          // Any hotkey of any open window: an item of this menu runs, one
          // of another window closes the menu.
          current_ = -1;
          for (int i = 0; i < static_cast<int>(hotspots.size()); ++i) {
            if (hotspots[i].enabled() && hotkey_matches(hotspots[i].key, e.key)) {
              current_ = i;
              break;
            }
          }
          if (current_ == -1) {
            current_ = previous;
          } else if (owns(owner, current_)) {
            ++selects;
          } else {
            done = true;
          }
          break;
        }
      }
      continue;
    }
    if ((e.flags & (kEventMoved | kEventLeftDown)) != 0 && (e.buttons & kButtonLeft) != 0) {
      const int hit = hotspots.hit_test(e.x, e.y);
      current_ = hit == -1 ? previous : hit;
      if (hit != -1 && owns(owner, hit) &&
          menu.items[static_cast<std::size_t>(sub_of(hit))].type == PopupItemType::kSubmenu) {
        ++selects;
      }
    }
    if ((e.flags & kEventLeftUp) != 0) {
      const int hit = hotspots.hit_test(e.x, e.y);
      if (hit == -1) {
        current_ = previous;
      } else {
        current_ = hit;
        ++selects;
      }
    }
    if ((e.flags & kEventRightUp) != 0) {
      pending = kPopupRightClick;
      break;
    }
  }
  for (std::size_t i = 0; i < menu.items.size(); ++i) {
    hotspots.pop();
  }
  screen.blit(saved, x, y);
  return pending == kPopupRightClick ? kPopupRightClick : static_cast<std::uint16_t>(pending);
}

int PopupRun::select(const PopupMenu& menu, int x, int y, bool shadow) {
  // gui_popup_menu_select (0x3bc78)
  if (current_ == -1) {
    return 0;
  }
  HotspotTable& hotspots = gui_.hotspots();
  const int hotspot = current_;
  const int n = hotspots[hotspot].sub;
  const PopupItem& item = menu.items[static_cast<std::size_t>(n)];
  int result = 0;
  switch (item.type) {
    case PopupItemType::kAction:
      hotspots.disable_all();
      item.action();
      hotspots.enable_all();
      result = kPopupItemRun;
      break;
    case PopupItemType::kSubmenu: {
      const Point origin = hooks_.submenu_origin(n, x, y);
      hotspots.disable(hotspot);
      // Only the low word of the submenu's result is kept.
      result = static_cast<std::uint16_t>(run(*item.submenu, origin.x, origin.y, shadow));
      hotspots.enable(hotspot);
      break;
    }
    case PopupItemType::kToggle:
      hotspots.disable_all();
      item.toggle(true);
      hotspots.enable_all();
      result = kPopupItemRun;
      break;
  }
  current_ = hotspot;
  return result;
}

}  // namespace

int popup_menu_run(Gui& gui, MenuHooks& hooks, const PopupMenu& menu, int x, int y, bool shadow, int* current) {
  PopupRun run(gui, hooks);
  const int result = run.run(menu, x, y, shadow);
  if (current != nullptr) {
    *current = run.current();
  }
  return result;
}

}  // namespace sh::ui
