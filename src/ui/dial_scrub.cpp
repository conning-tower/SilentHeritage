#include "ui/dial_scrub.h"

#include <cmath>

#include "assets/fnt.h"
#include "assets/shp.h"
#include "core/fixed.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "ui/draw.h"

namespace sh::ui {

namespace {

// Shift held (either key): the drag's rate is a tenth.
constexpr std::uint8_t kShiftKeys = kShiftLeft | kShiftRight;
constexpr double kFineRate = 0.1;

bool inside(const Rect& r, int x, int y) { return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1; }

}  // namespace

std::size_t DialScrubber::add(ScrubTarget target) {
  targets_.push_back(std::move(target));
  return targets_.size() - 1;
}

void DialScrubber::clear() {
  targets_.clear();
  target_.reset();
  value_.reset();
  drag_.end();
}

std::optional<std::size_t> DialScrubber::hit(int x, int y) const {
  for (std::size_t i = 0; i < targets_.size(); ++i) {
    const ScrubTarget& t = targets_[i];
    if (inside(t.rect, x, y) && (!t.enabled || t.enabled())) {
      return i;
    }
  }
  return std::nullopt;
}

void DialScrubber::notify() {
  if (target_ && targets_[*target_].changed) {
    targets_[*target_].changed();
  }
}

void DialScrubber::give() {
  if (!target_) {
    return;
  }
  const std::size_t index = *target_;
  const std::int32_t v = value_->value();
  const bool changed = v != value_->start();
  target_.reset();
  value_.reset();
  drag_.end();
  if (changed && targets_[index].commit) {
    targets_[index].commit(v);
  }
  if (targets_[index].changed) {
    targets_[index].changed();
  }
}

void DialScrubber::cancel() {
  const std::optional<std::size_t> index = target_;
  target_.reset();
  value_.reset();
  drag_.end();
  if (index && targets_[*index].changed) {
    targets_[*index].changed();
  }
}

bool DialScrubber::event(const Event& event, std::uint32_t tick) {
  if (event.wheel != 0 && !event.is_key()) {
    if (target_ && mode_ == Mode::kPress) {
      return true;
    }
    const std::optional<std::size_t> i = hit(event.x, event.y);
    if (!i) {
      return false;
    }
    if (target_ && *target_ != *i) {
      give();
    }
    if (!target_) {
      target_ = *i;
      mode_ = Mode::kWheel;
      value_.emplace(targets_[*i].scale, targets_[*i].current());
    }
    value_->add(static_cast<double>(event.wheel) * std::max(value_->scale().snap, 1));
    wheel_tick_ = tick;
    notify();
    return true;
  }
  if (event.is_key()) {
    if (target_ && mode_ == Mode::kPress && event.key == kKeyEsc) {
      cancel();
      swallow_ = true;
      return true;
    }
    return false;
  }
  const std::uint16_t flags = event.flags;
  if (swallow_) {
    // The rest of a cancelled press: up to the release of every button.
    if ((event.buttons & (kButtonLeft | kButtonRight)) == 0 && (flags & (kEventLeftUp | kEventRightUp)) != 0) {
      swallow_ = false;
    }
    return true;
  }
  if (target_ && mode_ == Mode::kPress) {
    if ((flags & (kEventRightDown | kEventRightUp)) != 0) {
      cancel();
      swallow_ = (event.buttons & (kButtonLeft | kButtonRight)) != 0;
      return true;
    }
    if ((flags & kEventLeftUp) != 0) {
      if (drag_.dragging()) {
        give();
      } else {
        const std::size_t index = *target_;
        const Event press = press_;
        target_.reset();
        value_.reset();
        drag_.end();
        if (targets_[index].tap) {
          targets_[index].tap(press);
        }
      }
      return true;
    }
    if ((flags & kEventMoved) != 0) {
      const bool was = drag_.dragging();
      const int step = drag_.move(event.x, event.y, tick);
      if (drag_.dragging()) {
        const std::int32_t before = value_->value();
        const double fine = (event.shift & kShiftKeys) != 0 ? kFineRate : 1.0;
        value_->add(step * value_->scale().units_per_px * scrub_acceleration(drag_.speed_x(tick)) *
                    scrub_precision(drag_.dy()) * fine);
        if (!was || value_->value() != before) {
          notify();
        }
      }
    }
    return true;
  }
  if ((flags & kEventLeftDown) != 0) {
    const std::optional<std::size_t> i = hit(event.x, event.y);
    if (!i) {
      return false;
    }
    if (target_) {
      give();
    }
    target_ = *i;
    mode_ = Mode::kPress;
    press_ = event;
    value_.emplace(targets_[*i].scale, targets_[*i].current());
    drag_.press(event.x, event.y, tick);
    return true;
  }
  return false;
}

void DialScrubber::process(std::uint32_t tick) {
  if (target_ && mode_ == Mode::kWheel && tick - wheel_tick_ >= kWheelCommitTicks) {
    give();
  }
}

std::optional<std::int32_t> DialScrubber::pending(std::size_t index) const {
  if (!target_ || *target_ != index || (mode_ == Mode::kPress && !drag_.dragging())) {
    return std::nullopt;
  }
  return value_->value();
}

std::uint8_t nearest_colour(const VgaPalette& palette, int r, int g, int b) {
  int best = 0;
  int best_d = 1 << 30;
  for (int i = 0; i < 256; ++i) {
    const auto c8 = [&](int k) { return palette[static_cast<std::size_t>(i * 3 + k)] * 255 / 63; };
    const int dr = c8(0) - r;
    const int dg = c8(1) - g;
    const int db = c8(2) - b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return static_cast<std::uint8_t>(best);
}

std::uint8_t ghost_colour(const VgaPalette& palette) { return nearest_colour(palette, 255, 190, 40); }

GlyphColourTable ghost_digit_colours(const GlyphColourTable& base, std::uint8_t ink, std::uint8_t back) {
  GlyphColourTable amber = base;
  for (int i = 0xc0; i <= 0xcb; ++i) {
    amber[static_cast<std::size_t>(i)] = ink;
  }
  for (int i = 0xe0; i <= 0xef; ++i) {
    amber[static_cast<std::size_t>(i)] = back;
  }
  return amber;
}

void draw_ghost_sprite(const Viewport& vp, const SpriteFrame& frame, int x, int y, std::int32_t angle,
                       std::uint8_t colour) {
  ColourRemap remap{};
  remap.fill(colour);
  remap[0xff] = 0xff;
  Image8 scratch;
  sprite_draw_transformed(vp, frame, x, y, scratch, Angle10(angle), Fixed16::from_int(1), Fixed16::from_int(1),
                          kSpriteRemap, &remap);
}

void draw_ghost_needle(const Viewport& vp, int cx, int cy, std::int32_t angle, int inner, int outer,
                       std::uint8_t colour) {
  const double a = angle * 3.14159265358979 / 1800.0;
  const double sx = std::sin(a);
  const double sy = -std::cos(a);
  for (const double w : {-1.0, 0.0, 1.0}) {
    // Offset across the needle.
    const double ox = -sy * w;
    const double oy = sx * w;
    const auto at = [&](double r, double s, double o) { return static_cast<int>(std::lround(s * r + o)); };
    draw_line(vp, cx + at(inner, sx, ox), cy + at(inner, sy, oy), cx + at(outer, sx, ox), cy + at(outer, sy, oy),
              colour);
  }
}

Rect draw_readout(const Viewport& vp, int cx, int y, const Font& font, const std::string& text, std::uint8_t ink,
                  std::uint8_t back) {
  const int w = font.text_width(text) + 8;
  const int h = font.height() + 4;
  const Rect r{cx - w / 2, y, cx - w / 2 + w - 1, y + h - 1};
  fill_rect(vp, r.x0, r.y0, r.x1, r.y1, back);
  draw_line(vp, r.x0, r.y0, r.x1, r.y0, ink);
  draw_line(vp, r.x0, r.y1, r.x1, r.y1, ink);
  draw_line(vp, r.x0, r.y0, r.x0, r.y1, ink);
  draw_line(vp, r.x1, r.y0, r.x1, r.y1, ink);
  GlyphColourTable colours{};
  colours.fill(ink);
  colours[0] = 0xff;
  colours[0xff] = 0xff;
  gui_draw_text(vp, r.x0 + 4, r.y0 + 2, font, text, &colours);
  return r;
}

}  // namespace sh::ui
