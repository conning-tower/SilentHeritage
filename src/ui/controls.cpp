#include "ui/controls.h"

#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <type_traits>

namespace sh::ui {

namespace {

// Runs a user callback with the lower windows disabled, as every activate
// slot does (`gui_disable_lower_windows` / `gui_enable_lower_windows`).
template <class Fn>
auto with_lower_windows_disabled(Dialog& dialog, Fn fn) {
  dialog.hotspots().disable_all();
  if constexpr (std::is_void_v<decltype(fn())>) {
    fn();
    dialog.hotspots().enable_all();
  } else {
    auto r = fn();
    dialog.hotspots().enable_all();
    return r;
  }
}

bool is_focused(const Dialog& dialog, const Control* control) { return dialog.focused_control() == control; }

// Enables (focused) or disables the key-only hotspots `subs` of `control`.
void toggle_key_hotspots(Dialog& dialog, const Control* control, bool focused, std::initializer_list<int> subs) {
  HotspotTable& table = dialog.hotspots();
  for (const int sub : subs) {
    const int i = table.find(control, sub, !focused);
    if (i != -1) {
      if (focused) {
        table.enable(i);
      } else {
        table.disable(i);
      }
    }
  }
}

// The relative index the next registered hotspot gets.
int next_hotspot(Dialog& dialog) { return static_cast<int>(dialog.hotspots().size()) - dialog.first_hotspot(); }

// The text of a NUL-terminated buffer.
std::size_t c_length(const std::string& s) { return std::strlen(s.c_str()); }

// Returns the screen area of a rectangle in `dialog`'s coordinates.
Rect screen_area(const Dialog& dialog, Rect r) {
  return {dialog.x() + r.x0, dialog.y() + r.y0, dialog.x() + r.x1, dialog.y() + r.y1};
}

}  // namespace

void add_control_hotspot(Dialog& dialog, Control& control, Rect r, int sub, std::uint32_t key, int focus) {
  Hotspot h;
  h.x0 = dialog.x() + r.x0;
  h.y0 = dialog.y() + r.y0;
  h.x1 = dialog.x() + r.x1;
  h.y1 = dialog.y() + r.y1;
  h.control = &control;
  h.sub = sub;
  h.key = key;
  h.focus = focus;
  h.owner = &dialog;
  dialog.hotspots().add(h);
}

void add_key_hotspot(Dialog& dialog, Control& control, int sub, std::uint32_t key, int focus) {
  Hotspot h;
  h.control = &control;
  h.sub = sub;
  h.key = key;
  h.focus = focus;
  h.owner = &dialog;
  dialog.hotspots().add(h);
}

// --- Area -----------------------------------------------------------------

bool Area::handle(Dialog& dialog, Msg msg, int sub, const Event& event) {
  // gui_area_handle (0x3e98c)
  if (msg == Msg::kRegister) {
    add_control_hotspot(dialog, *this, {x0, y0, x1, y1}, 0, key, kFocusNone);
  } else if (msg == Msg::kNone || static_cast<int>(msg) > static_cast<int>(Msg::kPaint)) {
    return false;
  }
  if (!on_message) {
    return false;
  }
  return with_lower_windows_disabled(dialog, [&] { return on_message(dialog, *this, msg, sub, event); }) != 0;
}

int Area::activate(Dialog& dialog, int sub, const Event& event) {
  // gui_area_activate (0x3ed04)
  if (!on_activate) {
    return 0;
  }
  return with_lower_windows_disabled(dialog, [&] { return on_activate(dialog, *this, sub, event); });
}

void Area::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- Static controls ------------------------------------------------------

bool Label::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_label_handle (0x3cca1)
  if (msg != Msg::kPaint) {
    return false;
  }
  dialog.add_dirty(dialog.skin().label(dialog.gfx(), x, y, font, text, use_colours, custom));
  return true;
}

bool Line::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_line_handle (0x3cce4)
  if (msg != Msg::kPaint) {
    return false;
  }
  dialog.add_dirty(dialog.skin().line(dialog.gfx(), x0, y0, x1, y1, colour));
  return true;
}

bool GroupBox::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_group_box_handle (0x3cd12)
  if (msg != Msg::kPaint) {
    return false;
  }
  dialog.add_dirty(dialog.skin().group_box(dialog.gfx(), x0, y0, x1, y1, caption));
  return true;
}

bool TitleBar::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_title_bar_handle (0x3cd54)
  Skin& skin = dialog.skin();
  const SkinMetrics& m = skin.metrics();
  switch (msg) {
    case Msg::kRegister:
      if (on_close) {
        add_control_hotspot(dialog, *this,
                            {m.close_box_x, m.close_box_y, m.close_box_x + m.close_box_width - 1,
                             m.close_box_y + m.close_box_height - 1},
                            0, 0, kFocusNone);
      }
      return false;
    case Msg::kPress:
      if (!on_close) {
        return false;
      }
      dialog.add_dirty(skin.close_box(dialog.gfx(), m.close_box_x, m.close_box_y, true));
      return true;
    case Msg::kPaint:
      dialog.add_dirty(skin.title_bar(dialog.gfx(), dialog.width(), dialog.height(), text, bool(on_close)));
      [[fallthrough]];
    case Msg::kRelease:
      if (on_close) {
        dialog.add_dirty(skin.close_box(dialog.gfx(), m.close_box_x, m.close_box_y, false));
        return true;
      }
      return false;
    default:
      return false;
  }
}

int TitleBar::activate(Dialog& dialog, int, const Event&) {
  // gui_title_bar_activate (0x3ea40)
  return with_lower_windows_disabled(dialog, [&] { return on_close(0); });
}

bool OwnerDraw::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_owner_draw_handle (0x3ce5e)
  if (msg != Msg::kPaint) {
    return false;
  }
  draw(dialog.gfx(), *this);
  return true;
}

bool TextField::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_text_field_handle (0x3ce81)
  if (msg != Msg::kPaint) {
    return false;
  }
  const std::string s = with_lower_windows_disabled(dialog, [&] { return getter(); });
  dialog.add_dirty(dialog.skin().text_field(dialog.gfx(), x0, y0, x1, y1, font, s, fill, align, use_colours, custom));
  return true;
}

// --- Button ---------------------------------------------------------------

bool Button::handle(Dialog& dialog, Msg msg, int, const Event& event) {
  // gui_button_handle (0x3ceed)
  Skin& skin = dialog.skin();
  bool down = false;
  switch (msg) {
    case Msg::kRegister:
      add_control_hotspot(dialog, *this, {x, y, x + w - 1, y + skin.metrics().button_height - 1}, 0, key,
                          kFocusSelf);
      count = 0;
      return false;
    case Msg::kPress:
      // Held with the mouse, the button repeats its click.
      dialog.set_repeat_interval(event.is_key() ? 0 : repeat);
      down = true;
      break;
    case Msg::kRelease:
    case Msg::kPaint:
      count = 0;
      break;
    default:
      return false;
  }
  dialog.add_dirty(
      skin.button(dialog.gfx(), x, y, w, label, down, highlighted, is_focused(dialog, this), disabled));
  return true;
}

int Button::activate(Dialog& dialog, int, const Event&) {
  // gui_button_activate (0x3ea1e)
  return with_lower_windows_disabled(dialog, [&] { return on_click(++count); });
}

void Button::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- Check box, radio group, cycle button ----------------------------------

bool CheckBox::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_check_box_handle (0x3d00a)
  Skin& skin = dialog.skin();
  bool down = false;
  switch (msg) {
    case Msg::kRegister:
      add_control_hotspot(dialog, *this,
                          {x, y, x + skin.check_box_width(label) - 1, y + skin.metrics().check_box_height - 1}, 0,
                          key, kFocusSelf);
      return false;
    case Msg::kPress:
      down = true;
      break;
    case Msg::kRelease:
    case Msg::kPaint:
      break;
    default:
      return false;
  }
  const bool checked = value(false);
  dialog.add_dirty(skin.check_box(dialog.gfx(), x, y, down, checked, is_focused(dialog, this), label));
  return true;
}

int CheckBox::activate(Dialog& dialog, int, const Event&) {
  // gui_check_box_activate (0x3ea52)
  with_lower_windows_disabled(dialog, [&] { value(true); });
  return 0;
}

void CheckBox::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

bool RadioGroup::handle(Dialog& dialog, Msg msg, int sub, const Event&) {
  // gui_radio_group_handle (0x3d111)
  Skin& skin = dialog.skin();
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const RadioItem& item = items[static_cast<std::size_t>(i)];
    switch (msg) {
      case Msg::kRegister:
        add_control_hotspot(dialog, *this,
                            {item.x, item.y, item.x + skin.radio_button_width(item.label) - 1,
                             item.y + skin.metrics().radio_height - 1},
                            i, item.key, kFocusSelf);
        break;
      case Msg::kPress:
      case Msg::kRelease:
      case Msg::kPaint: {
        const int sel = selected();
        const bool focused = is_focused(dialog, this) && dialog.focused_sub() == i;
        const bool down = msg == Msg::kPress && i == sub;
        dialog.add_dirty(
            skin.radio_button(dialog.gfx(), item.x, item.y, down, sel == i, focused, item.disabled, item.label));
        break;
      }
      case Msg::kRepeat:
        return false;
      default:
        break;
    }
  }
  return true;
}

int RadioGroup::activate(Dialog& dialog, int sub, const Event&) {
  // gui_radio_group_activate (0x3ea71)
  with_lower_windows_disabled(dialog, [&] { select(sub); });
  return 0;
}

bool RadioGroup::is_disabled(int sub) const { return items.at(static_cast<std::size_t>(sub)).disabled; }

void RadioGroup::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

bool CycleButton::handle(Dialog& dialog, Msg msg, int, const Event&) {
  // gui_cycle_button_handle (0x3d2a1)
  Skin& skin = dialog.skin();
  bool down = false;
  switch (msg) {
    case Msg::kRegister:
      add_control_hotspot(dialog, *this, {x, y, x + w - 1, y + skin.metrics().cycle_height - 1}, 0, key,
                          kFocusSelf);
      return true;
    case Msg::kPress:
      down = true;
      break;
    case Msg::kRelease:
    case Msg::kPaint:
      break;
    case Msg::kRepeat:
      return false;
    default:
      return true;
  }
  const std::string& s = strings.at(static_cast<std::size_t>(index(false)));
  dialog.add_dirty(skin.cycle_button(dialog.gfx(), x, y, w, down, is_focused(dialog, this), disabled, s));
  return true;
}

int CycleButton::activate(Dialog& dialog, int, const Event&) {
  // gui_cycle_button_activate (0x3ea88)
  with_lower_windows_disabled(dialog, [&] { index(true); });
  return 0;
}

void CycleButton::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- Text entry -----------------------------------------------------------

void TextEntry::draw_text(Dialog& dialog) {
  // gui_text_entry_draw_text (0x3f99e)
  dialog.add_dirty(dialog.skin().entry_text(dialog.gfx(), x, y, w, text, read_only));
}

int TextEntry::call_done(Dialog& dialog, int arg) {
  const int r = with_lower_windows_disabled(dialog, [&] { return on_done(text, arg); });
  text.resize(c_length(text));
  caret = std::min(caret, static_cast<int>(text.size()));
  return r;
}

bool TextEntry::edit_key(std::uint16_t k, bool digits_only) {
  // The key cases shared by gui_text_entry_edit and gui_spin_entry_edit.
  const int length = static_cast<int>(text.size());
  switch (k) {
    case kKeyBackspace:
      if (caret == 0) {
        return false;
      }
      --caret;
      text.erase(static_cast<std::size_t>(caret), 1);
      return true;
    case kKeyHome:
      if (caret == 0) {
        return false;
      }
      caret = 0;
      return true;
    case kKeyLeft:
      if (caret == 0) {
        return false;
      }
      --caret;
      return true;
    case kKeyRight:
      if (caret == length) {
        return false;
      }
      ++caret;
      return true;
    case kKeyEnd:
      if (caret == length) {
        return false;
      }
      caret = length;
      return true;
    case kKeyDelete:
      if (caret == length) {
        return false;
      }
      text.erase(static_cast<std::size_t>(caret), 1);
      return true;
    default: {
      const std::uint8_t c = static_cast<std::uint8_t>(k & 0xff);
      const bool accepted = digits_only ? (c >= '0' && c <= '9') : (c >= 0x20 && c <= 0x7e);
      if (!accepted || length + 1 >= max_length) {
        return false;
      }
      text.insert(static_cast<std::size_t>(caret), 1, static_cast<char>(c));
      ++caret;
      return true;
    }
  }
}

bool TextEntry::handle(Dialog& dialog, Msg msg, int, const Event& event) {
  // gui_text_entry_handle (0x3d3a7)
  switch (msg) {
    case Msg::kRegister:
      add_control_hotspot(dialog, *this, {x, y, x + w - 1, y + dialog.skin().metrics().entry_height - 1}, 0, key,
                          kFocusSelf);
      text.resize(c_length(text));
      caret = static_cast<int>(text.size());
      return true;
    case Msg::kPress:
    case Msg::kRelease:
      if (read_only) {
        return true;
      }
      if (is_focused(dialog, this)) {
        // The pointer event that focused the entry is replayed into the
        // edit loop (to place the caret).
        dialog.gui().events().flush();
        if ((event.flags & kEventMouseMask) != 0) {
          dialog.gui().events().post(event);
        }
        edit(dialog);
        return true;
      }
      break;
    case Msg::kRepeat:
      return false;
    case Msg::kPaint:
      dialog.add_dirty(dialog.skin().entry_box(dialog.gfx(), x, y, w));
      break;
    default:
      return true;
  }
  draw_text(dialog);
  return true;
}

void TextEntry::edit(Dialog& dialog) {
  // gui_text_entry_edit (0x3f02e): the modal edit loop.
  Gui& gui = dialog.gui();
  Skin& skin = dialog.skin();
  bool caret_on = false;
  bool done = false;
  bool accepted = false;
  bool redraw = true;
  dialog.refresh_first_hotspot();
  const TextInputScope text_input(
      gui, screen_area(dialog, {x, y, x + w - 1, y + skin.metrics().entry_height - 1}), TextFieldKind::kText);
  std::uint32_t blink = gui.ticks();
  std::uint16_t last_flags = 0;
  while (true) {
    if (done) {
      if (on_done) {
        const int r = call_done(dialog, 0);
        if (accepted) {
          dialog.set_result(r);
        }
      }
      draw_text(dialog);
      return;
    }
    redraw = redraw || dirty;
    if (redraw || dialog.redraw_controls()) {
      if (redraw) {
        draw_text(dialog);
        redraw = false;
        blink -= kCaretBlinkTicks;
      }
      dialog.flush_dirty_rects();
    }
    if (gui.ticks() - blink >= kCaretBlinkTicks) {
      blink = gui.ticks();
      caret_on = !caret_on;
      skin.entry_caret(dialog, x, y, text, caret, caret_on);
    }
    if (const std::optional<Event> e = gui.events().get()) {
      last_flags = e->flags;
      const int h = gui.hotspots().hit_test(e->x, e->y);
      dialog.set_hover(h != -1 ? h - dialog.first_hotspot() : -1);
      dialog.set_last_event(*e);
      dialog.update_highlight();
      if ((e->flags & kEventMouseMask) != 0 && (e->flags & kEventLeftDown) != 0) {
        if (h != -1 && gui.hotspots()[h].control == this) {
          caret = skin.entry_char_index_at(e->x - dialog.x(), x, text);
          redraw = true;
          gui.show_text_input();
        } else {
          // A click elsewhere ends the edit and goes to the dialog.
          done = true;
          gui.events().post(*e);
        }
      }
      if (e->is_key()) {
        redraw = false;
        switch (e->key) {
          case kKeyEsc:
            if ((dialog.flags() & kDialogCancel) != 0) {
              done = true;
              gui.events().post(*e);
            }
            break;
          case kKeyEnter:
            accepted = true;
            [[fallthrough]];
          case kKeyTab:
          case kKeyActivate:
            done = true;
            gui.events().post(*e);
            break;
          default:
            redraw = edit_key(e->key, false);
            break;
        }
      }
    }
    if (dialog.has_idle() && (!gui.events().peek() || last_flags == 0)) {
      dialog.call_idle();
    }
    gui.pump();
  }
}

void TextEntry::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- Spin entry -----------------------------------------------------------

bool SpinEntry::handle(Dialog& dialog, Msg msg, int sub, const Event& event) {
  // gui_spin_entry_handle (0x3d4d4)
  Skin& skin = dialog.skin();
  switch (msg) {
    case Msg::kRegister: {
      const int body = next_hotspot(dialog);
      add_control_hotspot(dialog, *this, {x, y, x + w - 1, y + skin.metrics().entry_height - 1}, 0, key,
                          kFocusSelf);
      add_control_hotspot(dialog, *this, skin.spin_up_rect(x, y, w), 1, 0, body);
      add_control_hotspot(dialog, *this, skin.spin_down_rect(x, y, w), 2, 0, body);
      text.resize(c_length(text));
      caret = static_cast<int>(text.size());
      return true;
    }
    case Msg::kPress:
    case Msg::kRelease:
      if (read_only) {
        return true;
      }
      if (!is_focused(dialog, this)) {
        draw_text(dialog);
        return true;
      }
      dialog.add_dirty(skin.spin_arrows(dialog.gfx(), x, y, w, sub, read_only));
      dialog.gui().events().flush();
      if ((event.flags & kEventMouseMask) != 0) {
        dialog.gui().events().post(event);
      }
      edit(dialog);
      break;
    case Msg::kRepeat:
      return false;
    case Msg::kPaint:
      dialog.add_dirty(skin.spin_entry_box(dialog.gfx(), x, y, w));
      draw_text(dialog);
      break;
    default:
      return true;
  }
  dialog.add_dirty(skin.spin_arrows(dialog.gfx(), x, y, w, 0, read_only));
  return true;
}

void SpinEntry::edit(Dialog& dialog) {
  // gui_spin_entry_edit (0x3f44e): the text entry loop restricted to
  // digits, plus the arrows, which repeat while held.
  Gui& gui = dialog.gui();
  Skin& skin = dialog.skin();
  std::uint32_t blink = gui.ticks();
  bool caret_on = false;
  bool done = false;
  bool accepted = false;
  bool redraw = true;
  bool repeating = false;
  std::uint32_t repeat_time = 0;
  dialog.refresh_first_hotspot();
  const TextInputScope text_input(
      gui, screen_area(dialog, {x, y, x + w - 1, y + skin.metrics().entry_height - 1}), TextFieldKind::kDigits);
  std::uint16_t last_flags = 0;
  while (true) {
    if (done) {
      if (on_done) {
        const int r = call_done(dialog, 0);
        if (accepted) {
          dialog.set_result(r);
        }
      }
      draw_text(dialog);
      return;
    }
    redraw = redraw || dirty;
    if (redraw || dialog.redraw_controls()) {
      if (redraw) {
        draw_text(dialog);
        if (dial != nullptr) {
          dialog.add_dirty(skin.dial(dialog.gfx(), *dial, text));
        }
        redraw = false;
        blink -= kCaretBlinkTicks;
      }
      dialog.flush_dirty_rects();
    }
    if (gui.ticks() - blink >= kCaretBlinkTicks) {
      blink = gui.ticks();
      caret_on = !caret_on;
      skin.entry_caret(dialog, x, y, text, caret, caret_on);
    }
    if (repeating && gui.ticks() - repeat_time >= kAutoRepeatTicks) {
      repeat_time = gui.ticks();
      Event press;
      press.flags = kEventLeftDown;
      press.flags_high = 0x8000;
      press.x = gui.mouse().x();
      press.y = gui.mouse().y();
      gui.events().post(press);
    }
    if (const std::optional<Event> e = gui.events().peek()) {
      last_flags = e->flags;
      const int h = gui.hotspots().hit_test(e->x, e->y);
      dialog.set_hover(h != -1 ? h - dialog.first_hotspot() : -1);
      dialog.set_last_event(*e);
      dialog.update_highlight();
      bool stop = repeating;
      if (h != -1) {
        if ((e->flags & kEventLeftDown) != 0) {
          const Hotspot& hs = gui.hotspots()[h];
          if (hs.control == this) {
            if (hs.sub == 0) {
              caret = skin.entry_char_index_at(e->x - dialog.x(), x, text);
              redraw = true;
              gui.show_text_input();
            } else if (repeating || (e->flags_high & 0x8000) == 0) {
              dialog.add_dirty(skin.spin_arrows(dialog.gfx(), x, y, w, hs.sub, read_only));
              repeat_time = gui.ticks();
              repeating = true;
              if (on_done) {
                call_done(dialog, hs.sub == 1 ? 1 : -1);
              }
              redraw = true;
            }
          } else {
            // Ends the edit; the event stays queued for the dialog.
            done = true;
          }
        }
        stop = (e->flags & kEventLeftUp) != 0;
      }
      if (stop) {
        repeating = false;
        dialog.add_dirty(skin.spin_arrows(dialog.gfx(), x, y, w, 0, read_only));
        redraw = true;
      }
      if (e->is_key()) {
        redraw = false;
        switch (e->key) {
          case kKeyEsc:
            if ((dialog.flags() & kDialogCancel) != 0) {
              done = true;
            }
            break;
          case kKeyEnter:
            accepted = true;
            [[fallthrough]];
          case kKeyTab:
          case kKeyActivate:
            done = true;
            break;
          default:
            redraw = edit_key(e->key, true);
            break;
        }
      }
      if (!done) {
        gui.events().get();
      }
    }
    if (dialog.has_idle() && (!gui.events().peek() || last_flags == 0)) {
      dialog.call_idle();
    }
    gui.pump();
  }
}

// --- Text box -------------------------------------------------------------

int TextBox::layout(Dialog& dialog, int mode, int& io_x, int& io_y) {
  // gui_textbox_layout (0x3f9e0): words are measured glyph by glyph and
  // flushed at spaces, newlines and the end; a word wider than the box is
  // cut; a line ends when the next word does not fit.
  Skin& skin = dialog.skin();
  const Viewport& gfx = dialog.gfx();
  if (mode == kLayoutDraw) {
    dialog.add_dirty(skin.textbox_background(gfx, x, y, w, h));
  }
  const std::size_t n = c_length(text);
  const auto ch = [&](std::size_t i) -> std::uint8_t {
    return i < n ? static_cast<std::uint8_t>(text[i]) : std::uint8_t{0};
  };
  bool flush = false;
  bool newline = false;
  bool done = false;
  bool pending_newline = false;
  int counter = 0;
  int line_width = 0;
  int word_width = 0;
  line = 0;
  std::size_t hit = std::string::npos;
  int px = x;
  int py = y;
  std::size_t word = 0;
  std::size_t p = 0;
  std::size_t next = 0;
  while (true) {
    if (done) {
      if (mode >= 0) {
        io_x = px;
        io_y = py;
      }
      if (mode == kLayoutDraw) {
        total = line + 1;
      }
      if (mode == kLayoutHitTest && hit != std::string::npos) {
        p = hit;
      }
      return static_cast<int>(p);
    }
    if (pending_newline) {
      pending_newline = false;
      newline = true;
      next = p;
    } else {
      const std::uint8_t c = ch(p);
      bool word_break = false;
      if (c == 0) {
        done = true;
        next = p;
        if (p != word) {
          if (w <= line_width + word_width) {
            newline = true;
          }
          flush = true;
        }
      } else if (c == '\n') {
        if (p == word) {
          pending_newline = true;
          p = word + 1;
        }
        flush = true;
        next = p;
        if (w <= line_width + word_width) {
          newline = true;
        }
      } else if (c == ' ') {
        word_break = true;
      } else {
        word_width += skin.textbox_char_width(c);
        if (w <= word_width) {
          word_width -= skin.textbox_char_width(c);
          word_break = true;
        } else {
          next = p + 1;
        }
      }
      if (word_break) {
        flush = true;
        if (p == word) {
          // The space (or the glyph that does not fit) is a word of its own.
          word_width += skin.textbox_char_width(ch(word));
          next = word + 1;
          if (w <= line_width + word_width) {
            pending_newline = true;
          }
        } else {
          next = p;
          if (w <= line_width + word_width) {
            newline = true;
          }
        }
      }
    }
    const int flushed_width = word_width;
    if (newline) {
      newline = false;
      if (top <= line && line <= top + visible) {
        px = x;
        py += line_height;
      }
      line_width = 0;
      ++line;
    }
    p = next;
    if (flush) {
      flush = false;
      p = word;
      if (mode == kLayoutDraw) {
        if (top <= line && line < top + visible) {
          if (ch(word) != '\n') {
            skin.textbox_text(gfx, px, py, std::string_view(text).substr(word, next - word), read_only);
          }
          px += word_width;
        }
      } else {
        for (; p < next; ++p) {
          if (top <= line && line <= top + visible) {
            const int cw = ch(p) == '\n' ? 0 : skin.textbox_char_width(ch(p));
            if (mode == kLayoutHitTest) {
              int lx = px;
              int ly = py;
              skin.textbox_text_origin(lx, ly);
              if (ly <= io_y && io_y < ly + line_height && lx <= io_x && io_x < lx + cw) {
                hit = p;
                return static_cast<int>(p);
              }
            } else if (mode == counter) {
              io_x = px;
              io_y = py;
              return static_cast<int>(next);
            }
            px += cw;
          }
          ++counter;
        }
      }
      word_width = 0;
      line_width += flushed_width;
      word = next;
      p = next;
    }
  }
}

bool TextBox::scroll_up(Dialog& dialog) {
  // gui_textbox_scroll_up (0x3fd45)
  if (top == 0) {
    return false;
  }
  int cx = 0;
  int cy = 0;
  layout(dialog, caret, cx, cy);
  dialog.skin().textbox_text_origin(cx, cy);
  --top;
  caret = layout(dialog, kLayoutHitTest, cx, cy);
  return true;
}

bool TextBox::scroll_down(Dialog& dialog) {
  // gui_textbox_scroll_down (0x3fda4)
  if (total <= top + visible) {
    return false;
  }
  int cx = 0;
  int cy = 0;
  layout(dialog, caret, cx, cy);
  dialog.skin().textbox_text_origin(cx, cy);
  ++top;
  caret = layout(dialog, kLayoutHitTest, cx, cy);
  return true;
}

bool TextBox::handle(Dialog& dialog, Msg msg, int, const Event& event) {
  // gui_textbox_handle (0x3d73c)
  switch (msg) {
    case Msg::kRegister:
      if (max_length != 0) {
        add_control_hotspot(dialog, *this, {x, y, x + w - 1, y + h - 1}, 0, key, kFocusSelf);
      }
      caret = 0;
      text.resize(c_length(text));
      top = 0;
      line = 0;
      visible = h / line_height;
      return true;
    case Msg::kPress:
    case Msg::kRelease:
      if (read_only) {
        return true;
      }
      if (is_focused(dialog, this)) {
        dialog.gui().events().flush();
        if ((event.flags & kEventMouseMask) != 0) {
          dialog.gui().events().post(event);
        }
        edit(dialog);
        return true;
      }
      break;
    case Msg::kRepeat:
      return false;
    case Msg::kPaint:
      dialog.add_dirty(dialog.skin().textbox_frame(dialog.gfx(), x, y, w, h));
      break;
    default:
      return true;
  }
  int lx = 0;
  int ly = 0;
  layout(dialog, kLayoutDraw, lx, ly);
  return true;
}

void TextBox::edit(Dialog& dialog) {
  // gui_textbox_edit (0x3fe4f)
  Gui& gui = dialog.gui();
  Skin& skin = dialog.skin();
  bool caret_on = false;
  bool done = false;
  bool redraw = true;
  dialog.refresh_first_hotspot();
  const TextInputScope text_input(gui, screen_area(dialog, {x, y, x + w - 1, y + h - 1}), TextFieldKind::kText);
  std::uint32_t blink = gui.ticks();
  std::uint16_t last_flags = 0;
  int cx = 0;
  int cy = 0;
  const auto draw_caret = [&](bool on) {
    // gui_textbox_draw_cursor (0x3fcf6)
    int px = 0;
    int py = 0;
    layout(dialog, caret, px, py);
    const std::size_t i = static_cast<std::size_t>(caret);
    std::uint8_t c = i < text.size() ? static_cast<std::uint8_t>(text[i]) : 0;
    if (c == 0 || c == '\n') {
      c = '_';
    }
    skin.textbox_caret(dialog, px, py, c, on);
  };
  while (true) {
    if (done) {
      if (on_done) {
        const int r = with_lower_windows_disabled(dialog, [&] { return on_done(text, 0); });
        dialog.set_result(r);
      }
      layout(dialog, kLayoutDraw, cx, cy);
      return;
    }
    redraw = redraw || dirty;
    if (redraw || dialog.redraw_controls()) {
      if (redraw) {
        layout(dialog, kLayoutDraw, cx, cy);
        redraw = false;
        blink -= kCaretBlinkTicks;
      }
      dialog.flush_dirty_rects();
    }
    if (gui.ticks() - blink >= kCaretBlinkTicks) {
      blink = gui.ticks();
      caret_on = !caret_on;
      draw_caret(caret_on);
    }
    if (const std::optional<Event> e = gui.events().get()) {
      last_flags = e->flags;
      const int hit = gui.hotspots().hit_test(e->x, e->y);
      dialog.set_hover(hit != -1 ? hit - dialog.first_hotspot() : -1);
      dialog.set_last_event(*e);
      dialog.update_highlight();
      if ((e->flags & kEventMouseMask) != 0 && (e->flags & kEventLeftDown) != 0) {
        if (hit != -1 && gui.hotspots()[hit].control == this) {
          cx = e->x - dialog.x();
          cy = e->y - dialog.y();
          caret = layout(dialog, kLayoutHitTest, cx, cy);
          redraw = true;
          gui.show_text_input();
        } else {
          done = true;
          gui.events().post(*e);
        }
      }
      if (e->is_key()) {
        redraw = false;
        const int length = static_cast<int>(text.size());
        std::uint8_t insert = 0;
        switch (e->key) {
          case kKeyBackspace:
            if (caret != 0) {
              --caret;
              text.erase(static_cast<std::size_t>(caret), 1);
              redraw = true;
            }
            break;
          case kKeyHome:
            if (caret != 0) {
              caret = 0;
              top = 0;
              redraw = true;
            }
            break;
          case kKeyLeft:
            if (caret != 0) {
              --caret;
              redraw = true;
            }
            break;
          case kKeyRight:
            if (caret != length) {
              ++caret;
              redraw = true;
            }
            break;
          case kKeyPageUp:
            for (int i = 0; i < visible - 1 && scroll_up(dialog); ++i) {
              redraw = true;
            }
            break;
          case kKeyPageDown:
            for (int i = 0; i < visible - 1 && scroll_down(dialog); ++i) {
              redraw = true;
            }
            break;
          case kKeyUp:
          case kKeyDown: {
            layout(dialog, caret, cx, cy);
            skin.textbox_text_origin(cx, cy);
            if (e->key == kKeyUp) {
              if (line == top) {
                if (top == 0) {
                  break;
                }
                --top;
              } else {
                cy -= line_height;
              }
            } else if (top + visible - 1 > line) {
              cy += line_height;
            } else if (total - 1 > line) {
              ++top;
            } else {
              break;
            }
            caret = layout(dialog, kLayoutHitTest, cx, cy);
            redraw = true;
            break;
          }
          case kKeyEnd:
            if (caret != length) {
              caret = length;
              layout(dialog, caret, cx, cy);
              top = std::max(line - (visible - 1), 0);
              redraw = true;
            }
            break;
          case kKeyDelete:
            if (caret != length) {
              text.erase(static_cast<std::size_t>(caret), 1);
              redraw = true;
            }
            break;
          case kKeyEsc:
            if ((dialog.flags() & kDialogCancel) != 0) {
              done = true;
              gui.events().post(*e);
            }
            break;
          case kKeyTab:
          case kKeyActivate:
            done = true;
            gui.events().post(*e);
            break;
          case kKeyEnter:
            insert = '\n';
            break;
          default: {
            const std::uint8_t c = static_cast<std::uint8_t>(e->key & 0xff);
            if (c >= 0x20 && c <= 0x7e) {
              insert = c;
            }
            break;
          }
        }
        if (insert != 0 && length + 1 < max_length) {
          text.insert(static_cast<std::size_t>(caret), 1, static_cast<char>(insert));
          ++caret;
          layout(dialog, caret, cx, cy);
          if (top + visible <= line) {
            ++top;
          }
          redraw = true;
        }
      }
    }
    if (dialog.has_idle() && (!gui.events().peek() || last_flags == 0)) {
      dialog.call_idle();
    }
    gui.pump();
  }
}

void TextBox::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- List -----------------------------------------------------------------

void List::clamp(int rows) {
  // gui_list_clamp (0x3ee9e)
  const int n = count();
  if (n <= top + rows) {
    top = std::max(n - rows, 0);
  }
  if (n <= selection) {
    selection = n - 1;
  }
  if (n == 0) {
    selection = -1;
  }
}

int List::row_at(const Dialog& dialog, const Event& event, int row_height) const {
  return (event.y - (y + dialog.y() + 1)) / row_height + top;
}

void List::notify_select() {
  // gui_list_notify_select (0x3ee6d)
  if (on_select && count() != 0 && selection != -1) {
    on_select(selection);
  }
}

void List::draw(Dialog& dialog) {
  // gui_list_draw (0x3ed39)
  Skin& skin = dialog.skin();
  const SkinMetrics& m = skin.metrics();
  dialog.add_dirty(skin.list_frame(dialog.gfx(), x, y, w));
  clamp(m.list_rows);
  const int end = std::min(top + m.list_rows, count());
  int ry = y;
  for (int i = top; i < end; ++i) {
    dialog.add_dirty(skin.list_item(dialog.gfx(), x, ry, w, item_text(i), selection == i));
    ry += m.list_row_height;
  }
  // gui_list_draw_scrollbar (0x3ee39)
  if (!no_scrollbar) {
    dialog.add_dirty(skin.list_scrollbar(dialog.gfx(), x, y, w, top, count()));
  }
}

void List::update_arrows(Dialog& dialog, int sub) {
  // gui_list_update_arrows (0x3eee3): an arrow that reaches the end is
  // disabled, and a synthetic release at its corner ends its repeat.
  if (no_scrollbar) {
    return;
  }
  HotspotTable& table = dialog.hotspots();
  const auto release = [&](int i) {
    if (sub == 1 || sub == 2) {
      Event up;
      up.flags = kEventLeftUp;
      up.x = table[i].x0;
      up.y = table[i].y0;
      dialog.gui().events().post(up);
    }
  };
  if (top == 0) {
    const int i = table.find(this, 1, true);
    if (i != -1) {
      table.disable(i);
      release(i);
    }
  } else {
    const int i = table.find(this, 1, false);
    if (i != -1) {
      table.enable(i);
    }
  }
  if (dialog.skin().metrics().list_rows + top < count()) {
    const int i = table.find(this, 2, false);
    if (i != -1) {
      table.enable(i);
    }
  } else {
    const int i = table.find(this, 2, true);
    if (i != -1) {
      table.disable(i);
      release(i);
    }
  }
}

bool List::handle(Dialog& dialog, Msg msg, int sub, const Event& event) {
  // gui_list_handle (0x3d8ba) and its body (0x3de1f)
  Skin& skin = dialog.skin();
  const SkinMetrics& m = skin.metrics();
  const bool focused = is_focused(dialog, this);
  toggle_key_hotspots(dialog, this, focused, {3, 4});
  dialog.add_dirty(skin.list_focus(dialog.gfx(), x, y, w, focused));
  clamp(m.list_rows);
  bool result = false;
  bool down = true;
  // The track drag: the thumb follows the pointer.
  const auto track = [&]() {
    if (sub != 5 || count() < m.list_rows) {
      return;
    }
    const Rect tr = skin.list_track_rect(x, y, w);
    int t = ((event.y - (tr.y0 + dialog.y())) * count()) / m.list_track;
    if (count() < t + m.list_rows) {
      t = count() - m.list_rows;
    } else if (t < 0) {
      t = 0;
    }
    if (t != top) {
      top = t;
      draw(dialog);
      result = true;
      update_arrows(dialog, sub);
    }
  };
  switch (msg) {
    case Msg::kRegister: {
      const int body = next_hotspot(dialog);
      add_control_hotspot(dialog, *this, {x, y, x + w - 1, y + m.list_height - 1}, 0, key, kFocusSelf);
      if (!no_scrollbar) {
        add_control_hotspot(dialog, *this, skin.list_up_rect(x, y, w), 1, 0, body);
        dialog.hotspots().disable(static_cast<int>(dialog.hotspots().size()) - 1);
        add_control_hotspot(dialog, *this, skin.list_down_rect(x, y, w), 2, 0, body);
        add_control_hotspot(dialog, *this, skin.list_track_rect(x, y, w), 5, 0, body);
      }
      add_key_hotspot(dialog, *this, 3, kKeyUp, body);
      dialog.hotspots().disable(static_cast<int>(dialog.hotspots().size()) - 1);
      add_key_hotspot(dialog, *this, 4, kKeyDown, body);
      dialog.hotspots().disable(static_cast<int>(dialog.hotspots().size()) - 1);
      top = 0;
      selection = -1;
      break;
    }
    case Msg::kRelease:
      down = false;
      [[fallthrough]];
    case Msg::kPress:
      update_arrows(dialog, sub);
      if (sub == 0) {
        // A click on a row selects it (keys only redraw).
        result = true;
        if (!is_focused(dialog, this) || event.flags == kEventKey) {
          break;
        }
        const int row = row_at(dialog, event, m.list_row_height);
        if (row >= count() || row == selection || row >= top + m.list_rows || row < top) {
          result = false;
          break;
        }
        selection = row;
        notify_select();
        draw(dialog);
      } else if (sub < 3) {
        dialog.set_repeat_interval(down ? kAutoRepeatTicks : 0);
        dialog.add_dirty(skin.list_arrow(dialog.gfx(), x, y, w, sub == 2, down));
        result = true;
        draw(dialog);
      } else if (sub > 4) {
        track();
      } else {
        result = true;
        draw(dialog);
      }
      break;
    case Msg::kRepeat:
      if (sub != 0) {
        track();
        break;
      }
      {
        const int row = row_at(dialog, event, m.list_row_height);
        if (row >= count() || row == selection || row >= top + m.list_rows) {
          break;
        }
        selection = row;
        notify_select();
        result = true;
        draw(dialog);
      }
      break;
    case Msg::kPaint:
      update_arrows(dialog, sub);
      notify_select();
      draw(dialog);
      if (!no_scrollbar) {
        dialog.add_dirty(skin.list_arrow(dialog.gfx(), x, y, w, false, false));
        dialog.add_dirty(skin.list_arrow(dialog.gfx(), x, y, w, true, false));
      }
      result = true;
      break;
    default:
      break;
  }
  return result;
}

int List::activate(Dialog& dialog, int sub, const Event& event) {
  // gui_list_activate (0x3eab1); always returns 0.
  Gui& gui = dialog.gui();
  const SkinMetrics& m = dialog.skin().metrics();
  clamp(m.list_rows);
  switch (sub) {
    case 0: {
      // Enter / Space, or a second click on the same row within the double
      // click time (timed with clock()).
      bool fire = false;
      if (event.flags == kEventKey) {
        fire = true;
      } else {
        if (row_at(dialog, event, m.list_row_height) >= count()) {
          return 0;
        }
        if (gui.list_last_selection == selection) {
          if (gui.clock() < gui.list_last_click + kDoubleClickTicks) {
            fire = true;
          }
        } else {
          gui.list_last_selection = selection;
        }
      }
      gui.list_last_click = gui.clock();
      if (!fire || selection == -1) {
        return 0;
      }
      if (on_activate) {
        with_lower_windows_disabled(dialog, [&] { on_activate(selection); });
      }
      break;
    }
    case 1:
      if (top < 1) {
        return 0;
      }
      --top;
      break;
    case 2:
      if (count() <= m.list_rows + top) {
        return 0;
      }
      ++top;
      break;
    case 3:
    case 4:
      if (count() == 0) {
        return 0;
      }
      if (sub == 3) {
        if (selection > 0) {
          --selection;
        }
        if (selection == -1) {
          selection = top;
        }
      } else if (selection < count() - 1) {
        ++selection;
      }
      if (selection < top) {
        top = selection;
      } else if (top + m.list_rows <= selection) {
        top = selection - (m.list_rows - 1);
      }
      notify_select();
      break;
    default:
      return 0;
  }
  update_arrows(dialog, sub);
  return 0;
}

void List::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- Slider ---------------------------------------------------------------

void Slider::draw_thumb(Dialog& dialog) {
  // gui_slider_draw_thumb (0x3ec81)
  dialog.add_dirty(dialog.skin().slider_track(dialog.gfx(), x, y, w, (*value * 1000) / max, disabled));
}

bool Slider::drag(Dialog& dialog, const Event& event) {
  // The value under the pointer on the track.
  const Rect tr = dialog.skin().slider_track_rect(x, y, w);
  int v = ((event.x - (dialog.x() + tr.x0)) * max) / (tr.x1 - tr.x0);
  v = std::clamp(v, 0, max);
  if (v == *value) {
    return false;
  }
  *value = v;
  draw_thumb(dialog);
  if (on_change) {
    with_lower_windows_disabled(dialog, [&] { on_change(*value); });
  }
  return true;
}

bool Slider::handle(Dialog& dialog, Msg msg, int sub, const Event& event) {
  // gui_slider_handle (0x3de4f) and its body (0x3e261)
  Skin& skin = dialog.skin();
  const bool focused = is_focused(dialog, this);
  toggle_key_hotspots(dialog, this, focused, {3, 4});
  dialog.add_dirty(skin.slider_focus(dialog.gfx(), x, y, w, focused));
  bool result = false;
  bool down = true;
  switch (msg) {
    case Msg::kRegister: {
      // The left arrow carries the key and the focus; the other parts
      // redirect to it.
      const int left = next_hotspot(dialog);
      add_control_hotspot(dialog, *this, skin.slider_left_rect(x, y, w), 1, key, kFocusSelf);
      add_control_hotspot(dialog, *this, skin.slider_track_rect(x, y, w), 0, 0, left);
      add_control_hotspot(dialog, *this, skin.slider_right_rect(x, y, w), 2, 0, left);
      add_key_hotspot(dialog, *this, 3, kKeyLeft, left);
      add_key_hotspot(dialog, *this, 4, kKeyRight, left);
      break;
    }
    case Msg::kRelease:
      down = false;
      [[fallthrough]];
    case Msg::kPress:
      if (disabled) {
        break;
      }
      if (sub == 0) {
        if (is_focused(dialog, this)) {
          result = drag(dialog, event);
        }
      } else if (sub <= 4) {
        const int offset = sub > 2 ? 2 : 0;
        dialog.set_repeat_interval(down ? kAutoRepeatTicks : 0);
        dialog.add_dirty(skin.slider_arrow(dialog.gfx(), x, y, w, sub - offset - 1 == 1, down, disabled));
        result = true;
        draw_thumb(dialog);
      }
      break;
    case Msg::kRepeat:
      if (sub == 0 && !disabled) {
        result = drag(dialog, event);
      }
      break;
    case Msg::kPaint:
      dialog.add_dirty(skin.slider_frame(dialog.gfx(), x, y, w, disabled));
      dialog.add_dirty(skin.slider_arrow(dialog.gfx(), x, y, w, false, false, disabled));
      dialog.add_dirty(skin.slider_arrow(dialog.gfx(), x, y, w, true, false, disabled));
      result = true;
      draw_thumb(dialog);
      break;
    default:
      break;
  }
  return result;
}

int Slider::activate(Dialog& dialog, int sub, const Event&) {
  // gui_slider_activate (0x3ec24): sub 0 (the track), 1 and 3 decrement,
  // 2 and 4 increment.
  if (sub == 0 || sub == 1 || sub == 3) {
    if (*value < 1) {
      return 0;
    }
    --*value;
  } else if (sub == 2 || sub == 4) {
    if (max <= *value) {
      return 0;
    }
    ++*value;
  } else {
    return 0;
  }
  if (on_change) {
    with_lower_windows_disabled(dialog, [&] { on_change(*value); });
  }
  return 0;
}

void Slider::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

// --- Button grid ----------------------------------------------------------

void ButtonGrid::draw_cell(Dialog& dialog, int cell, bool highlighted) {
  // gui_button_grid_draw_cell (0x3e94a)
  dialog.add_dirty(dialog.skin().grid_cell(dialog.gfx(), cells.at(static_cast<std::size_t>(cell)), highlighted));
}

bool ButtonGrid::is_disabled(int sub) const {
  return sub < 5 ? false : cells.at(static_cast<std::size_t>(sub - 5)).disabled;
}

bool ButtonGrid::handle(Dialog& dialog, Msg msg, int sub, const Event& event) {
  // gui_button_grid_handle (0x3e428)
  Gui& gui = dialog.gui();
  const int ch = dialog.skin().metrics().grid_cell_height;
  const bool focused = is_focused(dialog, this);
  toggle_key_hotspots(dialog, this, focused, {1, 2, 3, 4});
  switch (msg) {
    case Msg::kRegister: {
      int x0 = 32000;
      int y0 = 32000;
      int x1 = 0;
      int y1 = 0;
      for (const GridCell& c : cells) {
        x0 = std::min(x0, c.x);
        y0 = std::min(y0, c.y);
        x1 = std::max(x1, c.x + c.w - 1);
        y1 = std::max(y1, c.y + ch - 1);
      }
      const int body = next_hotspot(dialog);
      add_control_hotspot(dialog, *this, {x0, y0, x1, y1}, 0, 0, kFocusSelf);
      const std::uint16_t arrows[] = {kKeyUp, kKeyDown, kKeyLeft, kKeyRight};
      for (int i = 0; i < 4; ++i) {
        add_key_hotspot(dialog, *this, i + 1, arrows[i], body);
        dialog.hotspots().disable(static_cast<int>(dialog.hotspots().size()) - 1);
      }
      for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
        if (cells[static_cast<std::size_t>(i)].key != 0) {
          add_key_hotspot(dialog, *this, i + 5, cells[static_cast<std::size_t>(i)].key, body);
        }
      }
      gui.grid_disarmed = true;
      break;
    }
    case Msg::kPress:
    case Msg::kRepeat: {
      if (event.is_key() && (event.key == kKeySpace || event.key == kKeyEnter)) {
        gui.grid_disarmed = false;
        return false;
      }
      int cell = -1;
      if (sub == 0) {
        // The cell under the pointer.
        for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
          const GridCell& c = cells[static_cast<std::size_t>(i)];
          if (event.x >= c.x + dialog.x() && event.y >= c.y + dialog.y() && event.x <= c.x + c.w - 1 + dialog.x() &&
              event.y <= c.y + ch - 1 + dialog.y()) {
            cell = i;
            break;
          }
        }
      } else if (sub <= 4) {
        // Follows the neighbour links, skipping disabled cells.
        cell = selection;
        do {
          const GridCell& c = cells.at(static_cast<std::size_t>(cell));
          cell = sub == 1 ? c.up : sub == 2 ? c.down : sub == 3 ? c.left : c.right;
        } while (cell != -1 && cells.at(static_cast<std::size_t>(cell)).disabled);
      } else if (sub - 5 < static_cast<int>(cells.size())) {
        cell = sub - 5;
      }
      if (cell == -1 || cells.at(static_cast<std::size_t>(cell)).disabled) {
        return false;
      }
      gui.grid_disarmed = false;
      if (cell == selection) {
        return false;
      }
      draw_cell(dialog, selection, false);
      selection = cell;
      draw_cell(dialog, cell, true);
      break;
    }
    case Msg::kRelease:
      gui.grid_disarmed = true;
      draw_cell(dialog, selection, is_focused(dialog, this));
      break;
    case Msg::kPaint:
      for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
        draw_cell(dialog, i, focused && selection == i);
      }
      break;
    default:
      break;
  }
  return true;
}

int ButtonGrid::activate(Dialog& dialog, int sub, const Event&) {
  // gui_button_grid_activate (0x3ecbe)
  if ((sub < 1 || sub > 4) && !dialog.gui().grid_disarmed &&
      !cells.at(static_cast<std::size_t>(selection)).disabled) {
    return with_lower_windows_disabled(dialog, [&] { return on_select(selection); });
  }
  return 0;
}

void ButtonGrid::hover(bool on, int sub, const Event& event) {
  if (on_hover) {
    on_hover(on, sub, event);
  }
}

}  // namespace sh::ui
