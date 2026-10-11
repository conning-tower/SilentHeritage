#include "ui/dialog.h"

#include <algorithm>

namespace sh::ui {

namespace {

// The default blit callback (`gui_blit_callback`, 0x40828, through
// `mouse_safe_blit_viewport`): copies the source region to the screen.
void blit_to_screen(Image8& screen, const Viewport& src, int x0, int y0, int x1, int y1) {
  if (src.bitmap == &screen && src.x0 == x0 && src.y0 == y0) {
    return;
  }
  for (int y = y0; y <= y1; ++y) {
    const int sy = src.y0 + (y - y0);
    for (int x = x0; x <= x1; ++x) {
      const int sx = src.x0 + (x - x0);
      if (screen.contains(x, y) && src.bitmap->contains(sx, sy)) {
        screen.set(x, y, src.bitmap->at(sx, sy));
      }
    }
  }
}

// Copies the w x h screen area at (x, y) into `dst` (clipped).
void grab(const Image8& screen, int x, int y, Image8& dst) {
  for (int dy = 0; dy < dst.height(); ++dy) {
    for (int dx = 0; dx < dst.width(); ++dx) {
      if (screen.contains(x + dx, y + dy)) {
        dst.set(dx, dy, screen.at(x + dx, y + dy));
      }
    }
  }
}

}  // namespace

Gui::Gui(Image8& screen, TickSource ticks, Skin& skin)
    : screen_(&screen), ticks_(ticks), clock_(ticks), events_(ticks), mouse_(events_), skin_(&skin) {}

void Gui::begin_text_input(const Rect& area, TextFieldKind kind) {
  text_fields_.emplace_back(area, kind);
  if (text_input_.begin) {
    text_input_.begin(area, kind);
  }
}

void Gui::end_text_input() {
  if (text_fields_.empty()) {
    return;
  }
  text_fields_.pop_back();
  if (text_fields_.empty()) {
    if (text_input_.end) {
      text_input_.end();
    }
  } else if (text_input_.begin) {
    text_input_.begin(text_fields_.back().first, text_fields_.back().second);
  }
}

void Gui::show_text_input() {
  if (!text_fields_.empty() && text_input_.begin) {
    text_input_.begin(text_fields_.back().first, text_fields_.back().second);
  }
}

int Control::activate(Dialog&, int, const Event&) { return 0; }
bool Control::is_disabled(int) const { return false; }
bool Control::wants_keyboard() const { return false; }
bool Control::is_pressed() const { return pressed; }
void Control::hover(bool, int, const Event&) {}

Dialog::Dialog(Gui& gui, int x, int y, int w, int h, unsigned flags, Image8* target, std::size_t dirty_capacity)
    : gui_(&gui), x_(x), y_(y), w_(w), h_(h), flags_(flags), target_(target), dirty_capacity_(dirty_capacity) {
  blit_ = [&gui](const Viewport& src, int x0, int y0, int x1, int y1) {
    blit_to_screen(gui.screen(), src, x0, y0, x1, y1);
  };
}

Dialog::~Dialog() {
  if (open_) {
    hotspots().remove_owned_by(this);
  }
}

Control& Dialog::add(std::unique_ptr<Control> control) {
  controls_.push_back(std::move(control));
  return *controls_.back();
}

const Control* Dialog::focused_control() const {
  if (focus_ < 0 || first_hotspot_ < 0) {
    return nullptr;
  }
  const int i = first_hotspot_ + focus_;
  if (i >= static_cast<int>(gui_->hotspots().size())) {
    return nullptr;
  }
  return gui_->hotspots()[i].control;
}

int Dialog::focused_sub() const {
  if (focused_control() == nullptr) {
    return -1;
  }
  return gui_->hotspots()[first_hotspot_ + focus_].sub;
}

void Dialog::register_hotspots() {
  // gui_dialog_register_hotspots (0x3c8e7)
  first_hotspot_ = static_cast<int>(hotspots().size());
  focus_ = 0;
  const Event none;
  for (const auto& c : controls_) {
    c->handle(*this, Msg::kRegister, 0, none);
    c->pressed = false;
  }
  hotspot_count_ = static_cast<int>(hotspots().size()) - first_hotspot_;
}

void Dialog::focus_first_focusable() {
  for (int i = 0; i < hotspot_count_; ++i) {
    const Hotspot& h = rel_hotspot(i);
    if (h.focus != kFocusNone && !h.control->is_disabled(h.sub)) {
      focus_ = prev_focus_ = h.focus != kFocusSelf ? h.focus : i;
      return;
    }
  }
}

void Dialog::open() {
  // gui_dialog_open (0x3be0d)
  gui_->events().flush();
  const int border = (flags_ & kDialogTitled) != 0 ? skin().metrics().border : 0;
  const int bw = w_ + border;
  const int bh = h_ + border;
  if (target_ == nullptr) {
    buffer_ = Image8(bw, bh);
    background_ = Image8(bw, bh);
    gui_->mouse().freeze_motion();
    grab(gui_->screen(), x_, y_, background_);
    // The drawing buffer starts as a copy of the screen only for titled
    // dialogs; the original leaves it uninitialised otherwise.
    if ((flags_ & kDialogTitled) != 0) {
      grab(gui_->screen(), x_, y_, buffer_);
    }
    gui_->mouse().unfreeze_motion();
    gfx_ = {&buffer_, 0, 0, bw - 1, bh - 1};
  } else {
    gfx_ = {target_, x_, y_, x_ + bw - 1, y_ + bh - 1};
  }
  dirty_.clear();
  open_ = true;
  register_hotspots();
  invalidate();
  pressed_index_ = -1;
  pending_activate_ = false;
  pending_msg_ = Msg::kNone;
  repeat_interval_ = 0;
  result_ = 0;
  focus_ = -1;
  highlighted_ = nullptr;
  prev_focus_ = target_index_ = prev_hover_ = focus_;
  focus_first_focusable();
}

void Dialog::close() {
  // gui_dialog_close (0x3c07a)
  hotspots().remove_owned_by(this);
  if (target_ == nullptr) {
    Image8& screen = gui_->screen();
    screen.blit(background_, x_, y_);
    buffer_ = Image8();
    background_ = Image8();
  }
  open_ = false;
  gui_->events().flush();
}

void Dialog::invalidate() {
  // gui_dialog_invalidate (0x3cc09)
  full_redraw_ = true;
  for (const auto& c : controls_) {
    c->dirty = true;
  }
  hover_ = hotspots().hit_test(gui_->mouse().x(), gui_->mouse().y());
  if (hover_ < first_hotspot_ || hover_ >= first_hotspot_ + hotspot_count_) {
    hover_ = -1;
  }
  if (hover_ != -1) {
    hover_ -= first_hotspot_;
  }
}

void Dialog::update_highlight() {
  // gui_dialog_update_highlight (0x3c9fa)
  int hovered = -1;
  Control* control = nullptr;
  int sub = 0;
  if (hover_ != -1) {
    const Hotspot& h = rel_hotspot(hover_);
    control = h.control;
    sub = h.sub;
    if (!control->is_disabled(h.sub)) {
      hovered = hover_;
    }
  }
  if (hovered != prev_hover_ || full_redraw_) {
    if (highlighted_ != nullptr) {
      highlighted_->hover(false, sub, last_event_);
      highlighted_ = nullptr;
    }
    if (hovered != -1) {
      highlighted_ = control;
      control->hover(true, sub, last_event_);
    }
    prev_hover_ = hover_;
  }
}

void Dialog::handle_event(const Event& event) {
  // gui_dialog_handle_event (0x3c0ea)
  first_hotspot_ = hotspots().first_of(this);
  prev_focus_ = focus_;
  prev_hover_ = hover_;
  hover_ = hotspots().hit_test(event.x, event.y);
  last_event_ = event;
  if (hover_ < first_hotspot_ || first_hotspot_ + hotspot_count_ <= hover_) {
    hover_ = -1;
  }
  if (hover_ != -1) {
    hover_ -= first_hotspot_;
  }
  const std::uint16_t mouse = event.flags & kEventMouseMask;
  if (outside_cancel_ && (event.flags & kEventLeftDown) != 0 &&
      (event.x < x_ || event.y < y_ || event.x > x_ + w_ || event.y > y_ + h_)) {
    result_ = kResultCancel;
    pending_msg_ = Msg::kNone;
    return;
  }
  if (mouse != 0) {
    if ((event.flags & kEventLeftUp) != 0) {
      if (hover_ == pressed_index_) {
        if (hover_ != -1) {
          pending_msg_ = Msg::kRelease;
          pending_activate_ = true;
          target_index_ = pressed_index_;
        }
      } else if (prev_hover_ == pressed_index_ && prev_hover_ != -1) {
        pending_msg_ = Msg::kRelease;
        target_index_ = pressed_index_;
      }
      pressed_index_ = -1;
      repeat_interval_ = 0;
    }
    if ((event.flags & kEventLeftDown) != 0 && hover_ != -1 && !rel_disabled(hover_)) {
      pending_msg_ = Msg::kPress;
      target_index_ = pressed_index_ = hover_;
      const int redirect = rel_hotspot(hover_).focus;
      if (redirect >= 0) {
        focus_ = redirect;
      } else if (redirect == kFocusSelf) {
        focus_ = hover_;
      }
      press_time_ = gui_->ticks();
    }
    if ((event.flags & kEventMoved) != 0 && (event.buttons & kButtonLeft) != 0 && pressed_index_ != -1) {
      target_index_ = pressed_index_;
      if (hover_ == pressed_index_) {
        if (prev_hover_ == pressed_index_ && pending_msg_ == Msg::kNone) {
          pending_msg_ = Msg::kRepeat;
        } else {
          pending_msg_ = Msg::kPress;
          press_time_ = gui_->ticks();
        }
      } else {
        pending_msg_ = prev_hover_ == pressed_index_ ? Msg::kRelease : Msg::kNone;
      }
    }
    if ((event.flags & kEventRightUp) != 0 && (flags_ & kDialogCancel) != 0) {
      result_ = kResultCancel;
    }
  }
  if (!event.is_key()) {
    return;
  }
  const std::uint16_t key = event.key;
  const bool navigation = (flags_ & kDialogNoNavigation) == 0;
  const auto cycle_focus = [&](int step) {
    if (focus_ == -1) {
      return;
    }
    // Moves to the next / previous hotspot that takes the focus itself and
    // is enabled.
    do {
      focus_ += step;
      if (focus_ < 0) {
        focus_ = hotspot_count_ - 1;
      } else if (focus_ >= hotspot_count_) {
        focus_ = 0;
      }
    } while (rel_hotspot(focus_).focus != kFocusSelf || rel_disabled(focus_));
    pending_msg_ = Msg::kRelease;
    target_index_ = focus_;
  };
  const int back = key == kKeyEsc && event.back ? back_hotspot() : -1;
  if (key == kKeyEsc && back == -1) {
    if ((flags_ & kDialogCancel) != 0) {
      result_ = kResultCancel;
      pending_msg_ = Msg::kNone;
    }
    return;
  }
  if (key == kKeyShiftTab && navigation) {
    cycle_focus(-1);
    return;
  }
  if (key == kKeyTab && navigation) {
    cycle_focus(1);
    return;
  }
  if (key == kKeyActivate) {
    // The activation event posted below: its hotspot must be one of ours.
    if (event.hotspot < first_hotspot_ || event.hotspot >= first_hotspot_ + hotspot_count_ ||
        event.dialog != this) {
      return;
    }
    pending_msg_ = Msg::kRelease;
    pending_activate_ = true;
    target_index_ = event.hotspot - first_hotspot_;
    return;
  }
  if (back != -1) {
    // Android's Back: the back control is pressed as by its hotkey.
    focus_ = back;
  } else if (!((key == kKeyEnter || key == kKeySpace) && navigation)) {
    // Hotkeys (and Enter / Space without navigation).
    const int i = hotspots().find_hotkey(this, key, 1);
    if (i == -1 || i < first_hotspot_ || first_hotspot_ + hotspot_count_ <= i) {
      return;
    }
    const Hotspot& h = hotspots()[i];
    if (!h.control->is_disabled(h.sub)) {
      focus_ = i - first_hotspot_;
    }
  }
  if (focus_ == -1) {
    return;
  }
  // Presses the focused hotspot now and activates it when the posted
  // activation event comes back.
  pending_msg_ = Msg::kPress;
  target_index_ = focus_;
  Event activation = event;
  activation.key = kKeyActivate;
  activation.hotspot = first_hotspot_ + focus_;
  activation.dialog = this;
  gui_->events().post(activation);
  const int redirect = rel_hotspot(focus_).focus;
  if (redirect != kFocusSelf) {
    focus_ = redirect;
  }
  if (focus_ == kFocusNone) {
    focus_ = prev_focus_;
  }
}

int Dialog::back_hotspot() {
  if (back_ == nullptr) {
    return -1;
  }
  const int i = hotspots().find(back_, back_sub_, true);
  if (i == -1 || i < first_hotspot_ || i >= first_hotspot_ + hotspot_count_) {
    return -1;
  }
  const Hotspot& h = hotspots()[i];
  return h.control->is_disabled(h.sub) ? -1 : i - first_hotspot_;
}

bool Dialog::esc_ends_edit(const Event& event) {
  return (flags_ & kDialogCancel) != 0 || (event.back && back_hotspot() != -1);
}

bool Dialog::redraw_controls() {
  // gui_dialog_redraw_controls (0x3cb51)
  bool drawn = false;
  if (full_redraw_) {
    if ((flags_ & kDialogFramed) != 0) {
      add_dirty(skin().dialog_frame(gfx_, 0, 0, w_, h_, (flags_ & kDialogTitled) != 0));
    }
    drawn = true;
    full_redraw_ = false;
  }
  for (const auto& c : controls_) {
    if (c->dirty) {
      c->handle(*this, c->is_pressed() ? Msg::kPress : Msg::kPaint, kSubRepaint, last_event_);
      c->dirty = false;
      drawn = true;
    }
  }
  return drawn;
}

int Dialog::process() {
  // gui_dialog_process (0x3c645)
  first_hotspot_ = hotspots().first_of(this);
  frame_dirty_ = false;
  update_highlight();
  if (focus_ != -1 && rel_disabled(focus_)) {
    focus_ = prev_focus_ = -1;
    focus_first_focusable();
  }
  frame_dirty_ = redraw_controls();
  if (repeat_interval_ != 0 && pressed_index_ != -1 && hover_ == pressed_index_) {
    const std::uint32_t now = gui_->ticks();
    if (now - press_time_ >= repeat_interval_) {
      pending_msg_ = Msg::kPress;
      pending_activate_ = true;
      press_time_ = gui_->ticks();
      target_index_ = pressed_index_;
    }
  }
  if (pending_activate_) {
    Hotspot& h = rel_hotspot(target_index_);
    pending_activate_ = false;
    result_ = h.control->activate(*this, h.sub, last_event_);
  }
  if (pending_msg_ != Msg::kNone) {
    if (prev_focus_ != focus_ && prev_focus_ != -1) {
      // The control that lost the focus is released.
      Hotspot& old = rel_hotspot(prev_focus_);
      frame_dirty_ = old.control->handle(*this, Msg::kRelease, old.sub, last_event_);
      old.control->pressed = false;
    }
    // The handler may run a modal loop that changes the hotspot table.
    Hotspot target = rel_hotspot(target_index_);
    const Msg msg = pending_msg_;
    frame_dirty_ = target.control->handle(*this, msg, target.sub, last_event_) || frame_dirty_;
    if (msg == Msg::kPress) {
      target.control->pressed = true;
    } else if (msg == Msg::kRelease) {
      target.control->pressed = false;
    }
    pending_msg_ = Msg::kNone;
  }
  if (frame_dirty_) {
    flush_dirty_rects();
  }
  if (focus_ != -1 && result_ == 0 && pressed_index_ == -1) {
    const Hotspot& h = rel_hotspot(focus_);
    if (!h.control->is_disabled(h.sub) && h.control->wants_keyboard()) {
      // A focused text entry starts editing.
      pending_msg_ = Msg::kRelease;
      target_index_ = focus_;
    }
  }
  return result_;
}

void Dialog::add_dirty(Rect r) {
  // dirty_rect_add (0x404c8): normalised, dropped when inside a listed one.
  if (dirty_.size() >= dirty_capacity_) {
    return;
  }
  if (r.x1 < r.x0) {
    std::swap(r.x0, r.x1);
  }
  if (r.y1 < r.y0) {
    std::swap(r.y0, r.y1);
  }
  for (const Rect& d : dirty_) {
    if (d.x0 <= r.x0 && r.x1 <= d.x1 && d.y0 <= r.y0 && r.y1 <= d.y1) {
      return;
    }
  }
  dirty_.push_back(r);
}

void Dialog::flush_dirty_rects() {
  // gui_flush_dirty_rects (0x403fa)
  if (dirty_.empty()) {
    return;
  }
  if (extra_dirty_) {
    Rect extra;
    if (extra_dirty_(gfx_, extra)) {
      add_dirty(extra);
    }
  }
  for (const Rect& r : dirty_) {
    Viewport src{gfx_.bitmap, gfx_.x0 + r.x0, gfx_.y0 + r.y0, gfx_.x0 + r.x1, gfx_.y0 + r.y1};
    int x0 = src.x0;
    int y0 = src.y0;
    int x1 = src.x1;
    int y1 = src.y1;
    if (target_ == nullptr) {
      x0 += x_;
      y0 += y_;
      x1 += x_;
      y1 += y_;
    }
    if (blit_) {
      blit_(src, x0, y0, x1, y1);
    }
  }
  dirty_.clear();
}

void Dialog::call_idle() {
  if (idle_) {
    idle_(gfx_, run_arg_);
  }
  run_arg_ = 0;
}

int Dialog::run_begin() {
  // gui_dialog_run (0x3bd41), up to its loop
  run_arg_ = -1;
  hotspots().disable_all();
  if (gui_->dialog_cursor_sprites() != nullptr) {
    gui_->mouse().set_cursor(*gui_->dialog_cursor_sprites(), gui_->dialog_cursor_frame());
  }
  open();
  if (hotspot_count_ == 0) {
    close();
    hotspots().enable_all();
    return kResultNoHotspots;
  }
  return kResultContinue;
}

int Dialog::run_step() {
  int result = 0;
  while (true) {
    // Events are drained while they are mouse moves.
    if (const std::optional<Event> e = gui_->events().get()) {
      handle_event(*e);
    }
    result = process();
    if (result != 0) {
      break;
    }
    if (!gui_->events().peek() || (last_event_.flags & kEventMoved) == 0) {
      break;
    }
  }
  call_idle();
  if (result != 0) {
    close();
    hotspots().enable_all();
  }
  return result;
}

int Dialog::run() {
  int result = run_begin();
  while (result == kResultContinue) {
    result = run_step();
    if (result == kResultContinue) {
      gui_->pump();
    }
  }
  return result;
}

}  // namespace sh::ui
