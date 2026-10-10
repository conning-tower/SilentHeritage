#include "ui/event.h"

#include <utility>

namespace sh::ui {

TickSource make_tick_source(std::function<std::uint64_t()> milliseconds) {
  const std::uint64_t start = milliseconds();
  return [milliseconds = std::move(milliseconds), start]() {
    return static_cast<std::uint32_t>((milliseconds() - start) / 10);
  };
}

std::uint32_t bios_ticks(std::uint32_t gui_ticks) {
  return static_cast<std::uint32_t>(std::uint64_t{gui_ticks} * 1193182 / 6553600);
}

EventQueue::EventQueue(TickSource ticks) : ticks_(std::move(ticks)) {}

bool EventQueue::post(Event event) {
  if (count_ == kCapacity) {
    ++lost_;
    return false;
  }
  event.tick = bios_ticks(ticks_());
  ring_[(head_ + count_) % kCapacity] = event;
  ++count_;
  return true;
}

std::optional<Event> EventQueue::get() {
  if (count_ == 0) {
    return std::nullopt;
  }
  const Event e = ring_[head_];
  head_ = (head_ + 1) % kCapacity;
  --count_;
  return e;
}

std::optional<Event> EventQueue::peek(std::size_t n) const {
  if (n >= count_) {
    return std::nullopt;
  }
  return ring_[(head_ + n) % kCapacity];
}

void EventQueue::flush() {
  head_ = 0;
  count_ = 0;
}

}  // namespace sh::ui
