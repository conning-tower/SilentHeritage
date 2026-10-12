// The desktop's Alt+Enter fullscreen toggle, the mouse confined to the
// window while fullscreen, and the system pointer put where the game moves
// its pointer.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <optional>
#include <utility>
#include <vector>

#include "platform/display.h"

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

// Pushes the window's mouse enter (`inside`) or leave event.
void push_pointer_crossing(bool inside) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  REQUIRE(count == 1);
  SDL_Event e{};
  e.type = inside ? SDL_EVENT_WINDOW_MOUSE_ENTER : SDL_EVENT_WINDOW_MOUSE_LEAVE;
  e.window.windowID = SDL_GetWindowID(windows[0]);
  SDL_free(windows);
  REQUIRE(SDL_PushEvent(&e));
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
  display.warp_pointer(30, 200);
  CHECK_FALSE(last_motion(display));
  push_pointer_crossing(true);
  CHECK_FALSE(last_motion(display));
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

}  // namespace
}  // namespace sh
