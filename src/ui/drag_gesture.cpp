#include "ui/drag_gesture.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sh::ui {

namespace {

// The speed is measured over this many ticks.
constexpr std::uint32_t kSpeedWindow = 10;

}  // namespace

void PointerDrag::press(int x, int y, std::uint32_t tick) {
  active_ = true;
  dragging_ = false;
  press_x_ = x_ = x;
  press_y_ = y_ = y;
  step_dy_ = 0;
  samples_.clear();
  samples_.push_back({tick, x});
}

int PointerDrag::move(int x, int y, std::uint32_t tick) {
  if (!active_) {
    return 0;
  }
  const int step_x = x - x_;
  step_dy_ = y - y_;
  x_ = x;
  y_ = y;
  samples_.push_back({tick, x});
  while (samples_.size() > 2 && tick - samples_.front().tick > kSpeedWindow) {
    samples_.pop_front();
  }
  if (!dragging_) {
    if (std::abs(dx()) <= kDragThreshold && std::abs(dy()) <= kDragThreshold) {
      step_dy_ = 0;
      return 0;
    }
    dragging_ = true;
    // The motion below the threshold counts too.
    step_dy_ = dy();
    return dx();
  }
  return step_x;
}

void PointerDrag::end() {
  active_ = false;
  dragging_ = false;
  samples_.clear();
}

double PointerDrag::speed_x(std::uint32_t tick) const {
  // The oldest sample within the window (the last two at least).
  auto first = samples_.begin();
  while (samples_.end() - first > 2 && tick - first->tick > kSpeedWindow) {
    ++first;
  }
  if (first == samples_.end()) {
    return 0;
  }
  const Sample& last = samples_.back();
  if (last.tick == first->tick || tick - last.tick > kSpeedWindow) {
    return 0;
  }
  return (last.x - first->x) * 100.0 / static_cast<double>(last.tick - first->tick);
}

double scrub_acceleration(double px_per_s) { return std::clamp(std::abs(px_per_s) / 300.0, 1.0, 20.0); }

double scrub_precision(int dy) {
  const int d = std::abs(dy);
  if (d <= 60) {
    return 1;
  }
  return d <= 120 ? 0.25 : 1.0 / 16;
}

void ScrubValue::add(double units) {
  raw_ += units;
  if (!scale_.wrap) {
    raw_ = std::clamp(raw_, static_cast<double>(scale_.min), static_cast<double>(scale_.max));
  }
}

std::int32_t ScrubValue::value() const {
  const std::int32_t snap = std::max(scale_.snap, 1);
  auto v = static_cast<std::int64_t>(std::llround(raw_ / snap)) * snap;
  if (scale_.wrap) {
    const std::int64_t period = static_cast<std::int64_t>(scale_.max) - scale_.min + 1;
    v = ((v - scale_.min) % period + period) % period + scale_.min;
  } else {
    v = std::clamp<std::int64_t>(v, scale_.min, scale_.max);
  }
  return static_cast<std::int32_t>(v);
}

}  // namespace sh::ui
