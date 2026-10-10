// The control types of the dialog toolkit, one class per original vtable
// (0x101afc, 0x102788..0x102cb8): their hotspots, drawing through the skin
// hooks, click actions and, for the text controls, the modal edit loops.
// The original C callbacks are `std::function` members; the field offsets
// of the original objects are noted next to the members.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/dialog.h"

namespace sh::ui {

// Registers a hotspot of `control` for the rectangle `r` in dialog
// coordinates (offset to the screen by the dialog position).
void add_control_hotspot(Dialog& dialog, Control& control, Rect r, int sub, std::uint32_t key, int focus);
// Registers a key-only hotspot (rectangle -1).
void add_key_hotspot(Dialog& dialog, Control& control, int sub, std::uint32_t key, int focus);

// 0x101afc: a user-handled click region (the bridge uses full-screen
// halves). Every message goes to `on_message`, the click to `on_activate`.
class Area : public Control {
 public:
  using MessageFn = std::function<int(Dialog&, Area&, Msg msg, int sub, const Event&)>;
  using ActivateFn = std::function<int(Dialog&, Area&, int sub, const Event&)>;

  Area(int x0, int y0, int x1, int y1, MessageFn on_message, ActivateFn on_activate, std::uint32_t key = 0)
      : x0(x0), y0(y0), x1(x1), y1(y1), on_message(std::move(on_message)), on_activate(std::move(on_activate)),
        key(key) {}

  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x0, y0, x1, y1;   // +0x10..+0x1c
  MessageFn on_message;  // +0x20
  ActivateFn on_activate;  // +0x24
  std::uint32_t key;    // +0x2c
  bool disabled = false;  // +0x30
  HoverFn on_hover;     // +0x34
};

// 0x102818: a text label.
class Label : public Control {
 public:
  Label(int x, int y, std::string text, FontSlot font = FontSlot::kSmall, bool use_colours = true,
        const Font* custom = nullptr)
      : x(x), y(y), text(std::move(text)), font(font), use_colours(use_colours), custom(custom) {}

  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog&, int, const Event&) override { return 0; }

  int x, y;            // +0x10, +0x14
  std::string text;    // +0x18
  FontSlot font;       // +0x1c
  bool use_colours;    // +0x20: through the GUI glyph table
  const Font* custom;  // +0x24
};

// 0x102848: a line in colour `B[colour]` of the GUI colours.
class Line : public Control {
 public:
  Line(int x0, int y0, int x1, int y1, int colour) : x0(x0), y0(y0), x1(x1), y1(y1), colour(colour) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;

  int x0, y0, x1, y1;  // +0x10..+0x1c
  int colour;          // +0x20
};

// 0x102800: a group box with an optional caption.
class GroupBox : public Control {
 public:
  GroupBox(int x0, int y0, int x1, int y1, std::string caption = {})
      : x0(x0), y0(y0), x1(x1), y1(y1), caption(std::move(caption)) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;

  int x0, y0, x1, y1;   // +0x10..+0x1c
  std::string caption;  // +0x20
};

// 0x1027e8: the title bar; with `on_close` set, a close box (hotspot not
// focusable) whose click returns `on_close(0)`.
class TitleBar : public Control {
 public:
  explicit TitleBar(std::string text, std::function<int(int)> on_close = {})
      : text(std::move(text)), on_close(std::move(on_close)) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;

  std::string text;                  // +0x10
  std::function<int(int)> on_close;  // +0x14
};

// 0x102830: a control drawn by a callback on paint.
class OwnerDraw : public Control {
 public:
  using DrawFn = std::function<void(const Viewport& gfx, OwnerDraw&)>;
  explicit OwnerDraw(DrawFn draw) : draw(std::move(draw)) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;

  DrawFn draw;  // +0x10
};

// 0x1027d0: a text field showing the string of `getter`.
class TextField : public Control {
 public:
  TextField(int x0, int y0, int x1, int y1, std::function<std::string()> getter, FontSlot font = FontSlot::kSmall,
            TextAlign align = TextAlign::kLeft)
      : x0(x0), y0(y0), x1(x1), y1(y1), getter(std::move(getter)), font(font), align(align) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;

  int x0, y0, x1, y1;                    // +0x10..+0x1c
  std::function<std::string()> getter;   // +0x20
  FontSlot font;                         // +0x24
  TextAlign align;                       // +0x28
  bool use_colours = true;               // +0x2c
  int fill = -1;                         // +0x30: -1 for the default fill
  const Font* custom = nullptr;          // +0x34
};

// 0x1027b8: a push button. `on_click(n)` gets the click count since the
// press (auto-repeat clicks count up while held, `repeat` ticks apart for
// the mouse) and returns the dialog result.
class Button : public Control {
 public:
  Button(int x, int y, int w, std::string label, std::function<int(int)> on_click, std::uint32_t key = 0,
         std::uint32_t repeat = 0)
      : x(x), y(y), w(w), label(std::move(label)), on_click(std::move(on_click)), key(key), repeat(repeat) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y, w;                       // +0x10..+0x18
  std::string label;                 // +0x1c
  std::function<int(int)> on_click;  // +0x20
  std::uint32_t key;                 // +0x24
  std::uint32_t repeat;              // +0x28: auto-repeat interval (ticks, 0 none)
  bool highlighted = false;          // +0x2c
  bool disabled = false;             // +0x30
  HoverFn on_hover;                  // +0x34
  int count = 0;                     // +0x38
};

// 0x102ca0: a check box. `value(false)` returns the state, `value(true)`
// toggles it (the original callback with 0 / -1).
class CheckBox : public Control {
 public:
  CheckBox(int x, int y, std::string label, std::function<bool(bool toggle)> value, std::uint32_t key = 0)
      : x(x), y(y), label(std::move(label)), value(std::move(value)), key(key) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y;                                // +0x10, +0x14
  std::string label;                       // +0x18
  std::function<bool(bool toggle)> value;  // +0x1c
  std::uint32_t key;                       // +0x20
  bool disabled = false;                   // +0x24
  HoverFn on_hover;                        // +0x28
};

// One 0x14-byte radio item.
struct RadioItem {
  int x = 0;
  int y = 0;
  std::string label;
  std::uint32_t key = 0;
  bool disabled = false;  // +0x10
};

// 0x102c88: a group of radio buttons, one hotspot per item. `selected()`
// returns the selected item and `select(i)` selects one (the original
// callback with 0 / (-1, i)).
class RadioGroup : public Control {
 public:
  RadioGroup(std::vector<RadioItem> items, std::function<int()> selected, std::function<void(int)> select)
      : items(std::move(items)), selected(std::move(selected)), select(std::move(select)) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int sub) const override;
  void hover(bool on, int sub, const Event& event) override;

  std::vector<RadioItem> items;   // +0x10 count, +0x14 items
  std::function<int()> selected;  // +0x18 (query)
  std::function<void(int)> select;  // +0x18 (set)
  HoverFn on_hover;               // +0x1c
};

// 0x102cb8: a button cycling through `strings`. `index(false)` returns the
// current string, `index(true)` advances (the original callback with 0 /
// -1) and returns the new one.
class CycleButton : public Control {
 public:
  CycleButton(int x, int y, int w, std::vector<std::string> strings, std::function<int(bool advance)> index,
              std::uint32_t key = 0)
      : x(x), y(y), w(w), strings(std::move(strings)), index(std::move(index)), key(key) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y, w;                       // +0x10..+0x18
  std::vector<std::string> strings;  // +0x1c count, +0x20 strings
  std::function<int(bool)> index;    // +0x24
  std::uint32_t key;                 // +0x28
  bool disabled = false;             // +0x2c
  HoverFn on_hover;                  // +0x30
};

// 0x10296c: a one-line text entry. Getting the focus starts the modal edit
// loop (`gui_text_entry_edit`): printable characters are inserted up to
// `max_length - 1` characters, Home / End / Left / Right / Backspace /
// Delete edit, and Enter, Tab, Esc (in a cancellable dialog) or a click
// outside end it. Then `on_done(text, 0)` runs; after Enter its return
// value becomes the dialog result.
class TextEntry : public Control {
 public:
  using DoneFn = std::function<int(std::string& text, int arg)>;

  TextEntry(int x, int y, int w, int max_length, std::string text, DoneFn on_done = {}, std::uint32_t key = 0)
      : x(x), y(y), w(w), max_length(max_length), text(std::move(text)), on_done(std::move(on_done)), key(key) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  bool is_disabled(int) const override { return read_only; }
  bool wants_keyboard() const override { return true; }
  bool is_pressed() const override { return false; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y, w;           // +0x10..+0x18
  int max_length;        // +0x1c: buffer size, NUL included
  std::string text;      // +0x20
  DoneFn on_done;        // +0x24
  std::uint32_t key;     // +0x28
  bool read_only = false;  // +0x2c
  HoverFn on_hover;      // +0x30
  int caret = 0;         // +0x34

 protected:
  // Draws the text (`gui_text_entry_draw_text`).
  void draw_text(Dialog& dialog);
  // Applies an editing key to the text; returns true when it changed the
  // text or the caret.
  bool edit_key(std::uint16_t key, bool digits_only);
  // Runs `on_done(text, arg)` with the lower windows disabled and clamps
  // the caret; returns its value.
  int call_done(Dialog& dialog, int arg);

 private:
  void edit(Dialog& dialog);
};

// 0x102c70: a numeric text entry with up / down arrows, which call
// `on_done(text, 1)` / `on_done(text, -1)` (repeating while held), and an
// optional linked bearing dial (+0x3c) redrawn with the text.
class SpinEntry : public TextEntry {
 public:
  using TextEntry::TextEntry;
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;

  const Dial* dial = nullptr;  // +0x3c

 private:
  void edit(Dialog& dialog);
};

// 0x1027a0: a multi-line word-wrapped text box, editable when `max_length`
// is not 0 (then focusing it runs `gui_textbox_edit`).
class TextBox : public Control {
 public:
  using DoneFn = std::function<int(std::string& text, int arg)>;

  TextBox(int x, int y, int w, int h, int line_height, int max_length, std::string text, DoneFn on_done = {},
          std::uint32_t key = 0)
      : x(x), y(y), w(w), h(h), line_height(line_height), max_length(max_length), text(std::move(text)),
        on_done(std::move(on_done)), key(key) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  bool is_disabled(int) const override { return read_only; }
  bool wants_keyboard() const override { return true; }
  bool is_pressed() const override { return false; }
  void hover(bool on, int sub, const Event& event) override;

  // The modes of `layout` besides a character index.
  static constexpr int kLayoutDraw = -1;
  static constexpr int kLayoutHitTest = -2;
  // Word-wraps the text (`gui_textbox_layout`). With `kLayoutDraw` it draws
  // the visible lines and counts the lines; with `kLayoutHitTest` it
  // returns the index of the character at layout position (x, y); with a
  // character index n >= 0 it returns the layout position of character n
  // in (x, y). The line counter `line` ends on the last line reached.
  int layout(Dialog& dialog, int mode, int& x, int& y);
  // Scrolls one line, keeping the caret at the same place on screen
  // (`gui_textbox_scroll_up` / `_down`); returns false at the limit.
  bool scroll_up(Dialog& dialog);
  bool scroll_down(Dialog& dialog);

  int x, y, w, h;          // +0x10..+0x1c
  int line_height;         // +0x20
  int max_length;          // +0x24: 0 when not editable
  std::string text;        // +0x28
  DoneFn on_done;          // +0x2c
  std::uint32_t key;       // +0x30
  bool read_only = false;  // +0x34
  HoverFn on_hover;        // +0x38
  int caret = 0;           // +0x3c
  int top = 0;             // +0x44: first visible line
  int visible = 0;         // +0x48: h / line_height
  int total = 0;           // +0x4c: lines of the last drawing
  int line = 0;            // +0x50: line counter of `layout`

 private:
  void edit(Dialog& dialog);
};

// 0x102788: a scrolling list. Sub-indexes of its hotspots: 0 the rows, 1/2
// the arrows, 3/4 the Up / Down keys (enabled while focused), 5 the track.
class List : public Control {
 public:
  List(int x, int y, int w, std::function<int()> count, std::function<std::string(int)> item_text)
      : x(x), y(y), w(w), count(std::move(count)), item_text(std::move(item_text)) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y, w;                                  // +0x10..+0x18
  std::function<int()> count;                   // +0x24
  std::function<std::string(int)> item_text;    // +0x1c base, +0x20 stride
  std::function<void(int)> on_activate;         // +0x28: double click, Enter
  std::function<void(int)> on_select;           // +0x2c
  std::uint32_t key = 0;                        // +0x30
  bool disabled = false;                        // +0x34
  HoverFn on_hover;                             // +0x38
  bool no_scrollbar = false;                    // +0x3c
  int top = 0;                                  // +0x40
  int selection = -1;                           // +0x44

 private:
  void clamp(int rows);
  void draw(Dialog& dialog);
  void notify_select();
  void update_arrows(Dialog& dialog, int sub);
  int row_at(const Dialog& dialog, const Event& event, int row_height) const;
};

// 0x102c58: a horizontal slider over 0..max with Left / Right arrows (and
// keys while focused).
class Slider : public Control {
 public:
  Slider(int x, int y, int w, int max, int& value, std::function<void(int&)> on_change = {}, std::uint32_t key = 0)
      : x(x), y(y), w(w), max(max), value(&value), on_change(std::move(on_change)), key(key) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int) const override { return disabled; }
  void hover(bool on, int sub, const Event& event) override;

  int x, y, w;                        // +0x10..+0x18
  int max;                            // +0x1c
  int* value;                         // +0x20
  std::function<void(int&)> on_change;  // +0x24
  std::uint32_t key;                  // +0x28
  bool disabled = false;              // +0x2c
  HoverFn on_hover;                   // +0x30

 private:
  bool drag(Dialog& dialog, const Event& event);
  void draw_thumb(Dialog& dialog);
};

// 0x102c40: a grid of buttons with arrow-key neighbour links; clicking (or
// Enter / Space on) the selected cell returns `on_select(selection)`.
class ButtonGrid : public Control {
 public:
  ButtonGrid(std::vector<GridCell> cells, std::function<int(int)> on_select, int selection = 0)
      : cells(std::move(cells)), on_select(std::move(on_select)), selection(selection) {}
  bool handle(Dialog& dialog, Msg msg, int sub, const Event& event) override;
  int activate(Dialog& dialog, int sub, const Event& event) override;
  bool is_disabled(int sub) const override;
  void hover(bool on, int sub, const Event& event) override;

  std::vector<GridCell> cells;         // +0x10 count, +0x14 cells
  std::function<int(int)> on_select;   // +0x18
  int selection;                       // +0x1c
  HoverFn on_hover;                    // +0x20

 private:
  void draw_cell(Dialog& dialog, int cell, bool highlighted);
};

}  // namespace sh::ui
