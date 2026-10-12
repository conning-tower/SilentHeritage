// The desktop's Alt+Enter fullscreen toggle and the picture's rectangle the
// mouse is confined to while fullscreen.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <optional>
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

TEST_CASE("Display: the letterbox rectangle the mouse is confined to", "[fullscreen]") {
  // 4:3 windows: the whole window.
  CHECK(letterbox_rect(640, 480) == ScreenRect{0, 0, 640, 480});
  CHECK(letterbox_rect(1600, 1200) == ScreenRect{0, 0, 1600, 1200});
  // Wider: bars left and right.
  CHECK(letterbox_rect(1920, 1080) == ScreenRect{240, 0, 1440, 1080});
  CHECK(letterbox_rect(2560, 1440) == ScreenRect{320, 0, 1920, 1440});
  CHECK(letterbox_rect(1366, 768) == ScreenRect{171, 0, 1024, 768});
  // Taller: bars above and below.
  CHECK(letterbox_rect(1280, 1024) == ScreenRect{0, 32, 1280, 960});
  CHECK(letterbox_rect(801, 1000) == ScreenRect{0, 200, 801, 600});
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

}  // namespace
}  // namespace sh
