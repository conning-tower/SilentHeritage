// The pull-down / popup menus of the dialog toolkit (`gui_popup_menu_run`
// 0x3b703 and its helpers 0x3bbba..0x3bc78) and the hooks that draw them
// and the menu bar (the renderer slots 0x10895c..0x108970, default
// functions 0x408b5..0x40bbd).
//
// SH.EXE itself never opens a popup menu (the only caller of
// `gui_popup_menu_run` is its own submenu code) nor draws a menu bar: the
// in-game command menus are dialogs. These complete the toolkit.
//
//   ui::PopupMenu menu{{ui::PopupItem::make_action("Open", open), ui::PopupItem::make_submenu("More", more)}};
//   ui::DefaultMenuHooks hooks(gui_resources);
//   int code = ui::popup_menu_run(gui, hooks, menu, x, y, true);
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/default_skin.h"
#include "ui/dialog.h"
#include "ui/draw.h"

namespace sh::ui {

struct PopupMenu;

// The kinds of popup item (+8).
enum class PopupItemType : std::uint8_t {
  kAction = 0,   // runs `action`
  kSubmenu = 1,  // opens `submenu` beside the item
  kToggle = 2,   // `toggle(true)` flips it; its state shows as a check box
};

// One 0x14-byte popup item.
struct PopupItem {
  std::string label;     // +0
  std::string shortcut;  // +4: drawn right-aligned (empty: none)
  PopupItemType type = PopupItemType::kAction;  // +8
  // +0xc, by type. `toggle(false)` returns the state, `toggle(true)` flips
  // it (the original callback with 0 / -1 and the item index).
  std::function<void()> action;
  const PopupMenu* submenu = nullptr;
  std::function<bool(bool flip)> toggle;
  std::uint32_t key = 0;  // +0x10: hotkey, matched like the hotspots' keys

  static PopupItem make_action(std::string label, std::function<void()> action, std::uint32_t key = 0,
                               std::string shortcut = {}) {
    PopupItem item;
    item.label = std::move(label);
    item.shortcut = std::move(shortcut);
    item.action = std::move(action);
    item.key = key;
    return item;
  }
  static PopupItem make_submenu(std::string label, const PopupMenu& submenu, std::uint32_t key = 0) {
    PopupItem item;
    item.label = std::move(label);
    item.type = PopupItemType::kSubmenu;
    item.submenu = &submenu;
    item.key = key;
    return item;
  }
  static PopupItem make_toggle(std::string label, std::function<bool(bool flip)> toggle, std::uint32_t key = 0) {
    PopupItem item;
    item.label = std::move(label);
    item.type = PopupItemType::kToggle;
    item.toggle = std::move(toggle);
    item.key = key;
    return item;
  }
};

// A popup menu (+0 count, +4 items).
struct PopupMenu {
  std::vector<PopupItem> items;
};

// One 16-byte title of a menu bar: its x (+0), label (+4), menu (+8) and
// hotkey (+0xc).
struct MenuBarItem {
  const PopupMenu* menu = nullptr;
  std::string label;
  int x = 0;
  std::uint32_t key = 0;
};

// The menu hooks of the renderer table. Coordinates are relative to the
// drawing viewport `gfx`, except the popup's screen position (x, y) the
// item hook offsets its returned rectangle by (the item's hotspot).
class MenuHooks {
 public:
  virtual ~MenuHooks() = default;
  // 0x10895c: the bar, optionally filled with its shadow first.
  virtual Rect menu_bar(const Viewport& gfx, bool fill) = 0;
  // 0x108960: a bar title starting at x, highlighted when open.
  virtual Rect menu_bar_item(const Viewport& gfx, int x, const MenuBarItem& item, bool highlighted) = 0;
  // 0x108964: the popup frame, `h` high, with a shadow when `shadow`.
  virtual Rect popup_frame(const Viewport& gfx, int x, int y, int h, bool shadow) = 0;
  // 0x108968: item `n` of a popup shown at screen position (x, y); returns
  // its screen rectangle.
  virtual Rect popup_item(const Viewport& gfx, int x, int y, int n, const PopupItem& item, bool highlighted) = 0;
  // 0x10896c: the height of a popup of `count` items.
  virtual int popup_height(int count) = 0;
  // 0x108970: the screen position of the submenu of item `n` of the popup
  // at (x, y).
  virtual Point submenu_origin(int n, int x, int y) = 0;
  // The popup width (0x108a2a) and the shadow size (0x108a2c).
  virtual int popup_width() const = 0;
  virtual int shadow() const = 0;
  // The menu bar size (0x108a24 / 0x108a26).
  virtual int menu_bar_width() const = 0;
  virtual int menu_bar_height() const = 0;
};

// The default hooks (0x408b5..0x40bbd): bevelled boxes in the GUI colours,
// GUI_S text, the default skin's check box for toggles.
class DefaultMenuHooks final : public MenuHooks, private DefaultSkin {
 public:
  // `resources` must outlive the hooks.
  explicit DefaultMenuHooks(GuiResources& resources) : DefaultSkin(resources) {}

  // Sets the menu bar's width (the skin's 640 at first): a bar across a
  // wider screen (not in the original).
  void set_menu_bar_width(int width) { metrics_.menu_bar_width = width; }

  Rect menu_bar(const Viewport& gfx, bool fill) override;
  Rect menu_bar_item(const Viewport& gfx, int x, const MenuBarItem& item, bool highlighted) override;
  Rect popup_frame(const Viewport& gfx, int x, int y, int h, bool shadow) override;
  Rect popup_item(const Viewport& gfx, int x, int y, int n, const PopupItem& item, bool highlighted) override;
  int popup_height(int count) override;
  Point submenu_origin(int n, int x, int y) override;
  int popup_width() const override { return metrics().popup_width; }
  int shadow() const override { return metrics().border; }
  int menu_bar_width() const override { return metrics().menu_bar_width; }
  int menu_bar_height() const override { return metrics().menu_bar_height; }

 private:
  // `gui_draw_submenu_arrow` (0x40bbd): the triangle at (x, y).
  void submenu_arrow(const Viewport& gfx, int x, int y, bool highlighted);
};

// The results of `popup_menu_run` besides a key that closed it.
inline constexpr int kPopupItemRun = 0xffff;  // an item ran: every open menu closes
inline constexpr int kPopupRightClick = -1;   // a right click closed it
inline constexpr int kPopupLeft = 0;          // the pointer moved to another menu's item

// Runs `menu` modally with its corner at screen position (x, y)
// (`gui_popup_menu_run`): the area under it is saved and restored, its
// items are hotspots while it is open. Up / Down move the highlight
// (wrapping), Enter, an item's key or a click releasing on an item runs it
// (a submenu opens on Right, Enter or when dragged over), Esc and Left
// return their key, Right on an item without submenu returns it too.
// Returns `kPopupItemRun` once an item ran, `kPopupRightClick`, the key
// that closed the menu, or `kPopupLeft`. `current`, when given, receives
// the current hotspot when the menu closes (the toolkit's global 0x108950,
// which a menu bar reads after `kPopupLeft`: the title moved to).
int popup_menu_run(Gui& gui, MenuHooks& hooks, const PopupMenu& menu, int x, int y, bool shadow,
                   int* current = nullptr);

}  // namespace sh::ui
