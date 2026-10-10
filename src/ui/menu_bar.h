// The menu bar of the dialog toolkit (`gui_menu_bar_run`, SE 0x1461b, with
// `gui_menu_bar_draw` / `gui_menu_bar_draw_item`): a modal strip across the
// top of the screen whose titles open popup menus. SH.EXE has no menu bar;
// the scenario editor opens its File / Edit bar with it.
//
//   ui::MenuBar bar{{{.menu = &file, .label = "File", .x = 5}}, true};
//   ui::menu_bar_run(gui, hooks, bar);  // the chosen item's action has run
#pragma once

#include <vector>

#include "ui/popup_menu.h"

namespace sh::ui {

// A menu bar (+0 count, +4 titles, +8 shadow flag).
struct MenuBar {
  std::vector<MenuBarItem> items;
  bool shadow = true;  // the bar and its popups get a shadow
};

// Runs `bar` modally (`gui_menu_bar_run`): the windows below are disabled
// and the strip under the bar is saved, then restored at the end. No title
// is highlighted at first (Alt held highlights the first one); a press on
// a title, Enter or Down on the highlighted one, or its hotkey opens its
// popup under it (`popup_menu_run`); Left / Right move between the titles
// (wrapping) and reopen there while a popup is open; dragging over another
// title opens that one. Esc closes the open popup, then the bar; a right
// click or an item that ran closes everything.
void menu_bar_run(Gui& gui, MenuHooks& hooks, const MenuBar& bar);

}  // namespace sh::ui
