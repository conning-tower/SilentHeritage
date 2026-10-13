#include "platform/pointer_warp.h"

#include <cmath>

namespace sh {

namespace {

float distance(WindowPoint a, WindowPoint b) { return std::hypot(a.x - b.x, a.y - b.y); }

}  // namespace

void WarpCheck::warped(WindowPoint from, WindowPoint to) {
  if (watch_ && watch_->reports > 0) {
    // Only reports near its start before this one.
    refuse();
  }
  watch_.reset();
  if (!refused_ && distance(from, to) >= kMinDistance) {
    watch_ = Watch{.from = from, .to = to};
  }
}

void WarpCheck::reported(WindowPoint at) {
  if (!watch_ || distance(at, watch_->to) <= kEcho) {
    return;
  }
  if (distance(at, watch_->to) < distance(at, watch_->from)) {
    refusals_ = 0;
    watch_.reset();
    return;
  }
  if (++watch_->reports >= kReports) {
    refuse();
  }
}

void WarpCheck::refuse() {
  watch_.reset();
  if (++refusals_ >= kRefusals) {
    refused_ = true;
  }
}

}  // namespace sh
