// The application lifecycle events of SDL3 (core/lifecycle.h). SDL hands
// the background, foreground, low memory and termination events only to
// its event watches, at the moment the system reports them, and never
// queues them (SDL_SendAppEvent): on Android, the event poll that reports
// the app paused then blocks until it is resumed, and the process may be
// killed meanwhile.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>

#include "core/lifecycle.h"

namespace sh {

// Returns the lifecycle event of SDL event type `type` reported to a
// `LifecycleWatch`, nothing for the others (the window's focus comes
// through the event queue, see `Display::poll`).
std::optional<LifecycleEvent> watched_lifecycle_event(std::uint32_t type);

// An SDL event watch calling a handler with the lifecycle events. The
// handler runs inside the event poll of SDL's main thread that receives
// the event (`Display::poll`): where the program polls its input, so
// between two of its steps. Events SDL sends from other threads are not
// reported.
class LifecycleWatch {
 public:
  // Watches the events (SDL's events must be initialized, e.g. by a
  // `Display`) until destroyed. Throws std::runtime_error if SDL fails.
  explicit LifecycleWatch(std::function<void(LifecycleEvent)> handler);
  ~LifecycleWatch();
  LifecycleWatch(const LifecycleWatch&) = delete;
  LifecycleWatch& operator=(const LifecycleWatch&) = delete;

 private:
  std::function<void(LifecycleEvent)> handler_;
};

}  // namespace sh
