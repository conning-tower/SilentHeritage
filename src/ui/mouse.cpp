#include "ui/mouse.h"

#include <algorithm>

#include "gfx/sprite.h"

namespace sh::ui {

Mouse::Mouse(EventQueue& events, int width, int height) : events_(&events), width_(width), height_(height) {}

void Mouse::set_bounds(int width, int height) {
  width_ = width;
  height_ = height;
  x_ = std::clamp(x_, 0, width_ - 1);
  y_ = std::clamp(y_, 0, height_ - 1);
}

void Mouse::set_band(std::optional<ClipRect> band) {
  band_ = band;
  if (!band_ || y_ < band_->top) {
    x_ = std::clamp(x_, 0, width_ - 1);
  }
}

void Mouse::update(int x, int y, std::uint8_t buttons, std::uint8_t shift) {
  const std::uint8_t old_buttons = buttons_;
  buttons_ = buttons & (kButtonLeft | kButtonRight | kButtonMiddle);
  bool moved = false;
  if (frozen_ < 1) {
    const int ny = std::clamp(y, 0, height_ - 1);
    const int nx = band_ && ny >= band_->top ? std::clamp(x, band_->left, band_->right) : std::clamp(x, 0, width_ - 1);
    moved = nx != x_ || ny != y_;
    x_ = nx;
    y_ = ny;
  }
  std::uint16_t flags = moved ? kEventMoved : 0;
  const auto edge = [&](std::uint8_t bit, std::uint16_t down, std::uint16_t up) {
    if ((buttons_ & bit) != (old_buttons & bit)) {
      flags |= (buttons_ & bit) != 0 ? down : up;
    }
  };
  edge(kButtonLeft, kEventLeftDown, kEventLeftUp);
  edge(kButtonRight, kEventRightDown, kEventRightUp);
  edge(kButtonMiddle, kEventMiddleDown, kEventMiddleUp);
  if (flags != 0) {
    Event e;
    e.flags = flags;
    e.shift = shift;
    e.buttons = buttons_;
    e.x = x_;
    e.y = y_;
    events_->post(e);
  }
}

Event Mouse::key_event(std::uint16_t bios_word, std::uint8_t shift) const {
  Event e;
  e.flags = kEventKey;
  e.key = (bios_word & 0xff) == 0xe0 ? static_cast<std::uint16_t>(bios_word & 0xff00) : bios_word;
  e.shift = shift;
  e.buttons = static_cast<std::uint8_t>(((buttons_ & kButtonLeft) != 0 ? kKeyButtonLeft : 0) |
                                        ((buttons_ & kButtonRight) != 0 ? kKeyButtonRight : 0) |
                                        ((buttons_ & kButtonMiddle) != 0 ? kKeyButtonMiddle : 0));
  e.x = x_;
  e.y = y_;
  return e;
}

void Mouse::post_key(std::uint16_t bios_word, std::uint8_t shift) { events_->post(key_event(bios_word, shift)); }

void Mouse::post_back(std::uint8_t shift) {
  Event e = key_event(kKeyEsc, shift);
  e.back = true;
  events_->post(e);
}

void Mouse::post_wheel(int notches, std::uint8_t shift) {
  Event e;
  e.shift = shift;
  e.buttons = buttons_;
  e.x = x_;
  e.y = y_;
  e.wheel = notches;
  events_->post(e);
}

void Mouse::touch(std::int64_t id, int x, int y) {
  for (Touch& t : touches_) {
    if (t.id == id) {
      t.x = x;
      t.y = y;
      return;
    }
  }
  touches_.push_back({id, x, y});
}

void Mouse::lift(std::int64_t id) {
  std::erase_if(touches_, [id](const Touch& t) { return t.id == id; });
}

void Mouse::set_cursor(const Shp& sprites, std::size_t frame) {
  sprites_ = &sprites;
  frame_ = frame;
}

void Mouse::show() {
  if (show_count_ != 0) {
    ++show_count_;
  }
}

void Mouse::hide() { --show_count_; }

std::optional<ClipRect> Mouse::cursor_rect() const {
  if (!shown() || sprites_ == nullptr) {
    return std::nullopt;
  }
  const SpriteFrame& f = sprites_->frame(frame_);
  ClipRect r{x_ + f.x0, y_ + f.y0, x_ + f.x1, y_ + f.y1};
  if (r.left > width_ - 1 || r.top > height_ - 1 || r.right < 0 || r.bottom < 0) {
    return std::nullopt;
  }
  r.left = std::max(r.left, 0);
  r.top = std::max(r.top, 0);
  r.right = std::min(r.right, width_ - 1);
  r.bottom = std::min(r.bottom, height_ - 1);
  return r;
}

void Mouse::draw(Image8& frame) {
  // mouse_draw_cursor (0x590e4)
  erase(frame);
  const std::optional<ClipRect> r = cursor_rect();
  if (!r) {
    return;
  }
  if (hide_rect_ && !(r->right < hide_rect_->left || r->left > hide_rect_->right ||
                      r->bottom < hide_rect_->top || r->top > hide_rect_->bottom)) {
    return;
  }
  ClipRect c = *r;
  c.right = std::min(c.right, frame.width() - 1);
  c.bottom = std::min(c.bottom, frame.height() - 1);
  if (c.empty()) {
    return;
  }
  saved_rect_ = c;
  saved_.clear();
  for (int y = c.top; y <= c.bottom; ++y) {
    for (int x = c.left; x <= c.right; ++x) {
      saved_.push_back(frame.at(x, y));
    }
  }
  draw_sprite_clipped(Viewport::full(frame), sprites_->frame(frame_), x_, y_);
  drawn_ = true;
}

void Mouse::erase(Image8& frame) {
  // mouse_erase_cursor (0x592ff)
  if (!drawn_) {
    return;
  }
  std::size_t i = 0;
  for (int y = saved_rect_.top; y <= saved_rect_.bottom; ++y) {
    for (int x = saved_rect_.left; x <= saved_rect_.right; ++x) {
      frame.set(x, y, saved_[i++]);
    }
  }
  drawn_ = false;
}

}  // namespace sh::ui
