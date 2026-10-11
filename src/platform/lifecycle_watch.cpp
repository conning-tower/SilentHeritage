#include "platform/lifecycle_watch.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include <format>
#include <stdexcept>
#include <utility>

namespace sh {

namespace {

using Handler = std::function<void(LifecycleEvent)>;

bool SDLCALL on_event(void* userdata, SDL_Event* event) {
  // Android's Java thread also pushes events (window, touch, text): only
  // the main thread's, sent where the program polls, reach the handler.
  if (const std::optional<LifecycleEvent> e = watched_lifecycle_event(event->type); e && SDL_IsMainThread()) {
    (*static_cast<Handler*>(userdata))(*e);
  }
  return true;
}

}  // namespace

std::optional<LifecycleEvent> watched_lifecycle_event(std::uint32_t type) {
  switch (type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
      return LifecycleEvent::kWillEnterBackground;
    case SDL_EVENT_DID_ENTER_BACKGROUND:
      return LifecycleEvent::kDidEnterBackground;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
      return LifecycleEvent::kWillEnterForeground;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      return LifecycleEvent::kDidEnterForeground;
    case SDL_EVENT_LOW_MEMORY:
      return LifecycleEvent::kLowMemory;
    case SDL_EVENT_TERMINATING:
      return LifecycleEvent::kTerminating;
    case SDL_EVENT_QUIT:
      return LifecycleEvent::kQuit;
    default:
      return std::nullopt;
  }
}

LifecycleWatch::LifecycleWatch(std::function<void(LifecycleEvent)> handler) : handler_(std::move(handler)) {
  if (!SDL_AddEventWatch(on_event, &handler_)) {
    throw std::runtime_error(std::format("SDL_AddEventWatch: {}", SDL_GetError()));
  }
}

LifecycleWatch::~LifecycleWatch() { SDL_RemoveEventWatch(on_event, &handler_); }

}  // namespace sh
