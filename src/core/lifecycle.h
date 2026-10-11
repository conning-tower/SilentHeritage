// The application lifecycle events the system sends the program (not in
// the original): going to the background and back, low memory,
// termination, and the window's focus. The platform layer reports them
// (platform/lifecycle_watch.h, platform/display.h) and the game keeps its
// resume snapshot with them (game/resume_keeper.h).
#pragma once

#include <cstdint>
#include <string_view>

namespace sh {

enum class LifecycleEvent : std::uint8_t {
  kWillEnterBackground,  // SDL_EVENT_WILL_ENTER_BACKGROUND
  kDidEnterBackground,   // SDL_EVENT_DID_ENTER_BACKGROUND
  kWillEnterForeground,  // SDL_EVENT_WILL_ENTER_FOREGROUND
  kDidEnterForeground,   // SDL_EVENT_DID_ENTER_FOREGROUND
  kLowMemory,            // SDL_EVENT_LOW_MEMORY
  kTerminating,          // SDL_EVENT_TERMINATING
  kQuit,                 // SDL_EVENT_QUIT: the window closed, the system ends the program
  kFocusLost,            // SDL_EVENT_WINDOW_FOCUS_LOST
  kFocusGained,          // SDL_EVENT_WINDOW_FOCUS_GAINED
};

// Returns the event's name in the lifecycle log.
constexpr std::string_view lifecycle_event_name(LifecycleEvent e) {
  switch (e) {
    case LifecycleEvent::kWillEnterBackground:
      return "will-enter-background";
    case LifecycleEvent::kDidEnterBackground:
      return "did-enter-background";
    case LifecycleEvent::kWillEnterForeground:
      return "will-enter-foreground";
    case LifecycleEvent::kDidEnterForeground:
      return "did-enter-foreground";
    case LifecycleEvent::kLowMemory:
      return "low-memory";
    case LifecycleEvent::kTerminating:
      return "terminating";
    case LifecycleEvent::kQuit:
      return "quit";
    case LifecycleEvent::kFocusLost:
      return "focus-lost";
    case LifecycleEvent::kFocusGained:
      return "focus-gained";
  }
  return "?";
}

}  // namespace sh
