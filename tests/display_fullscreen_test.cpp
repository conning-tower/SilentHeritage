// The desktop's Alt+Enter fullscreen toggle, the mouse confined to the
// picture while fullscreen, and the system pointer put where the game moves
// its pointer, unless the system is seen to refuse it.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <optional>
#include <utility>
#include <vector>

#include "platform/display.h"
#include "platform/pointer_warp.h"

namespace sh {
namespace {

void push_key(Uint32 type, SDL_Keycode key, SDL_Keymod mod, bool repeat = false) {
  SDL_Event e{};
  e.type = type;
  e.key.key = key;
  e.key.mod = mod;
  e.key.down = type == SDL_EVENT_KEY_DOWN;
  e.key.repeat = repeat;
  REQUIRE(SDL_PushEvent(&e));
}

TEST_CASE("Display: Alt+Enter toggles fullscreen and never reaches the game", "[fullscreen]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  Display display("test", 1);
  while (display.poll()) {
  }
  REQUIRE_FALSE(display.fullscreen());
  // Alt+Return: down, a repeat and the release are all taken.
  push_key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_LALT);
  push_key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_LALT, true);
  push_key(SDL_EVENT_KEY_UP, SDLK_RETURN, SDL_KMOD_LALT);
  for (std::optional<InputEvent> e = display.poll(); e; e = display.poll()) {
    CHECK(e->type != EventType::kKeyDown);
    CHECK(e->type != EventType::kKeyUp);
  }
  CHECK(display.fullscreen());
  // Alt with the keypad's Enter: back to the window.
  push_key(SDL_EVENT_KEY_DOWN, SDLK_KP_ENTER, SDL_KMOD_RALT);
  push_key(SDL_EVENT_KEY_UP, SDLK_KP_ENTER, SDL_KMOD_RALT);
  for (std::optional<InputEvent> e = display.poll(); e; e = display.poll()) {
    CHECK(e->type != EventType::kKeyDown);
    CHECK(e->type != EventType::kKeyUp);
  }
  CHECK_FALSE(display.fullscreen());
  // Enter alone is the game's.
  push_key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_NONE);
  std::optional<InputEvent> e = display.poll();
  while (e && e->type != EventType::kKeyDown) {
    e = display.poll();
  }
  REQUIRE(e);
  CHECK(e->key == SDLK_RETURN);
  CHECK_FALSE(display.fullscreen());
}

TEST_CASE("Display: created fullscreen (wide too), smoothing changed live, Alt+Enter reported", "[fullscreen]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  for (const bool wide : {false, true}) {
    Display display("test", 1, Scaling::kSmooth, wide, 0, 0, true);
    while (display.poll()) {
    }
    CHECK(display.fullscreen());
    CHECK(display.screen_width() >= kScreenWidth);
    display.present(Image8(display.screen_width(), kScreenHeight), palette);
    display.set_scaling(Scaling::kSharp);
    display.present(Image8(kScreenWidth, kScreenHeight), palette);
    std::vector<bool> toggled;
    display.on_fullscreen_toggled([&toggled](bool on) { toggled.push_back(on); });
    push_key(SDL_EVENT_KEY_DOWN, SDLK_RETURN, SDL_KMOD_LALT);
    push_key(SDL_EVENT_KEY_UP, SDLK_RETURN, SDL_KMOD_LALT);
    while (display.poll()) {
    }
    CHECK(toggled == std::vector<bool>{false});
    // Set from the options page: not reported as a toggle.
    display.set_fullscreen(true);
    CHECK(display.fullscreen());
    CHECK(toggled.size() == 1);
  }
}

// Returns the display's window (the only one).
SDL_Window* only_window() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  REQUIRE(count == 1);
  SDL_Window* window = windows[0];
  SDL_free(windows);
  return window;
}

// Drains the display's events.
void drain_events(Display& display) {
  while (display.poll()) {
  }
}

// Pushes the window event `type` (no data).
void push_window_event(Uint32 type) {
  SDL_Event e{};
  e.type = type;
  e.window.windowID = SDL_GetWindowID(only_window());
  REQUIRE(SDL_PushEvent(&e));
}

// Pushes the window's mouse enter (`inside`) or leave event.
void push_pointer_crossing(bool inside) {
  push_window_event(inside ? SDL_EVENT_WINDOW_MOUSE_ENTER : SDL_EVENT_WINDOW_MOUSE_LEAVE);
}


// Returns the last mouse motion `poll` returns, if any.
std::optional<InputEvent> last_motion(Display& display) {
  std::optional<InputEvent> last;
  for (std::optional<InputEvent> e = display.poll(); e; e = display.poll()) {
    if (e->type == EventType::kMouseMove) {
      last = e;
    }
  }
  return last;
}

TEST_CASE("Display: the system pointer goes where the game puts its pointer", "[fullscreen]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  // A wide window at 2.25 times, a 640 frame centred in it: the frame's
  // pixels to the window and back.
  Display display("test", 1, Scaling::kSharp, true, 1920, 1080);
  while (display.poll()) {
  }
  display.present(Image8(kScreenWidth, kScreenHeight), palette);
  // Off the window (the dummy driver puts the pointer in the window it
  // shows): never pulled back in.
  push_pointer_crossing(false);
  CHECK_FALSE(last_motion(display));
  CHECK_FALSE(display.pointer_inside());
  display.warp_pointer(30, 200);
  CHECK_FALSE(last_motion(display));
  push_pointer_crossing(true);
  CHECK_FALSE(last_motion(display));
  CHECK(display.pointer_inside());
  // Beyond the frame too, over the side fill.
  for (const auto& [x, y] :
       {std::pair{30, 200}, {kScreenWidth - 31, 479}, {0, 0}, {609, 31}, {-40, 200}, {kScreenWidth + 20, 10}}) {
    display.warp_pointer(x, y);
    const std::optional<InputEvent> e = last_motion(display);
    REQUIRE(e);
    CHECK(e->x == x);
    CHECK(e->y == y);
  }
  push_pointer_crossing(false);
  CHECK_FALSE(last_motion(display));
  display.warp_pointer(100, 100);
  CHECK_FALSE(last_motion(display));
}

// (The dummy driver does not confine the mouse: only the area asked for is
// seen.)
TEST_CASE("Display: fullscreen keeps the mouse in the picture, off the black bars", "[fullscreen]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  SECTION("A display of the picture's shape: the whole window, freed in a window") {
    Display display("test", 1, Scaling::kSharp, false);
    drain_events(display);
    CHECK_FALSE(display.mouse_confinement());
    display.set_fullscreen(true);
    drain_events(display);
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(only_window(), &w, &h);
    CHECK(display.mouse_confinement() == WindowRect{0, 0, w, h});
    display.set_fullscreen(false);
    drain_events(display);
    CHECK_FALSE(display.mouse_confinement());
  }
  // The dummy driver's fullscreen is 4:3: the window manager makes a
  // window of another shape fullscreen instead.
  const auto fullscreen_at = [](Display& display, int w, int h) {
    push_window_event(SDL_EVENT_WINDOW_ENTER_FULLSCREEN);
    drain_events(display);
    REQUIRE(display.fullscreen());
    REQUIRE(SDL_SetWindowAspectRatio(only_window(), 0, 0));
    REQUIRE(SDL_SetWindowSize(only_window(), w, h));
    drain_events(display);
  };
  SECTION("4:3 on 16:9: the bars beside it are out") {
    Display display("test", 1, Scaling::kSharp, false, 1280, 960);
    drain_events(display);
    fullscreen_at(display, 1920, 1080);
    CHECK(display.mouse_confinement() == WindowRect{240, 0, 1440, 1080});
    // Back to a window: free again.
    push_window_event(SDL_EVENT_WINDOW_LEAVE_FULLSCREEN);
    drain_events(display);
    CHECK_FALSE(display.mouse_confinement());
  }
  SECTION("Wide on 16:9: the side fill beside a 640-wide frame is in") {
    Display display("test", 1, Scaling::kSharp, true, 1708, 960);
    drain_events(display);
    fullscreen_at(display, 1920, 1080);
    CHECK(display.screen_width() == 853);
    // The screen's 1919.25 points, rounded outward.
    CHECK(display.mouse_confinement() == WindowRect{0, 0, 1920, 1080});
    display.present(Image8(kScreenWidth, kScreenHeight), palette);
    CHECK(display.mouse_confinement() == WindowRect{0, 0, 1920, 1080});
    display.present(Image8(853, kScreenHeight), palette);
    CHECK(display.mouse_confinement() == WindowRect{0, 0, 1920, 1080});
  }
  SECTION("Wide display, the setting off, on 16:9: 4:3 between black bars") {
    Display display("test", 1, Scaling::kSharp, true, 1708, 960);
    drain_events(display);
    fullscreen_at(display, 1920, 1080);
    display.set_wide_aspect(false);
    CHECK(display.screen_width() == kScreenWidth);
    CHECK(display.mouse_confinement() == WindowRect{240, 0, 1440, 1080});
    // On again: the whole screen at once.
    display.set_wide_aspect(true);
    CHECK(display.screen_width() == 853);
    CHECK(display.mouse_confinement() == WindowRect{0, 0, 1920, 1080});
  }
  SECTION("Wide on 32:9: the widest screen, 21:9, between bars") {
    Display display("test", 1, Scaling::kSharp, true, 1708, 960);
    drain_events(display);
    fullscreen_at(display, 3840, 1080);
    CHECK(display.screen_width() == kMaxScreenWidth);
    CHECK(display.mouse_confinement() == WindowRect{660, 0, 2520, 1080});
    display.present(Image8(kScreenWidth, kScreenHeight), palette);
    CHECK(display.mouse_confinement() == WindowRect{660, 0, 2520, 1080});
    // The window narrowed to 16:9: the picture fills it.
    REQUIRE(SDL_SetWindowSize(only_window(), 1920, 1080));
    drain_events(display);
    CHECK(display.mouse_confinement() == WindowRect{0, 0, 1920, 1080});
  }
}

TEST_CASE("Display: the warps stop once the system is seen to refuse them", "[fullscreen]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  // At 2x: the game's (0, 200) is window point (1, 401).
  Display display("test", 2, Scaling::kSharp);
  drain_events(display);
  display.present(Image8(kScreenWidth, kScreenHeight), palette);
  push_pointer_crossing(true);
  drain_events(display);
  // The user's pointer moves to the screen's left edge (the dummy driver
  // moves it as asked), then the game sends it 30 pixels in.
  const auto edge_scroll = [&display] {
    SDL_WarpMouseInWindow(only_window(), 1, 401);
    drain_events(display);
    display.warp_pointer(30, 200);
    return last_motion(display);
  };
  SECTION("Honoured: the motions go on from where the pointer was sent") {
    for (int i = 0; i < 10; ++i) {
      const std::optional<InputEvent> e = edge_scroll();
      REQUIRE(e);
      CHECK(e->x == 30);
      SDL_WarpMouseInWindow(only_window(), 64, 404);
      drain_events(display);
    }
    CHECK_FALSE(display.pointer_warp_refused());
  }
  SECTION("Refused: the motions go on from the edge, until no more warps") {
    // The motion back to the edge is what a compositor that ignores the
    // warp reports next.
    for (int i = 0; i < WarpCheck::kRefusals; ++i) {
      CHECK(edge_scroll());
    }
    CHECK_FALSE(display.pointer_warp_refused());
    SDL_WarpMouseInWindow(only_window(), 1, 401);
    drain_events(display);
    display.warp_pointer(30, 200);
    CHECK(display.pointer_warp_refused());
    // No warp from now on.
    SDL_WarpMouseInWindow(only_window(), 1, 401);
    drain_events(display);
    display.warp_pointer(30, 200);
    CHECK_FALSE(last_motion(display));
  }
}

}  // namespace
}  // namespace sh
