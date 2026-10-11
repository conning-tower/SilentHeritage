// The wide screen (`Features::wide_screen`) at the platform and GUI level:
// the screen width following the window, the letterbox and the mouse
// mapping of a frame narrower than the screen, the side fill's tint, and
// the GUI's screen resized for the wide layouts.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <optional>
#include <stdexcept>

#include "core/image.h"
#include "game_data.h"
#include "platform/display.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh {
namespace {

TEST_CASE("Wide screen: the width follows the window's aspect, 640 to 21:9", "[wide]") {
  CHECK(wide_screen_width(1280, 960) == 640);
  CHECK(wide_screen_width(1708, 960) == 854);
  CHECK(wide_screen_width(1920, 1080) == 853);
  CHECK(wide_screen_width(2240, 960) == 1120);
  // Wider than 21:9 and narrower than 4:3: clamped.
  CHECK(wide_screen_width(3840, 1080) == kMaxScreenWidth);
  CHECK(wide_screen_width(1000, 1000) == kScreenWidth);
  CHECK(wide_screen_width(0, 0) == kScreenWidth);
}

TEST_CASE("Wide screen: the letterbox of a wider screen", "[wide]") {
  // The 4:3 default is unchanged.
  CHECK(letterbox_rect(1920, 1080) == ScreenRect{240, 0, 1440, 1080});
  // A screen of the window's aspect fills it.
  CHECK(letterbox_rect(1708, 960, 854) == ScreenRect{0, 0, 1708, 960});
  // Wider than the window: bars above and below.
  CHECK(letterbox_rect(1708, 960, 1120) == ScreenRect{0, 114, 1708, 732});
}

TEST_CASE("Wide screen: the side fill takes the frame palette's brightness and colour", "[wide]") {
  Palette full;
  full.colors.fill(Rgb{255, 255, 255});
  std::array<std::uint8_t, 3> brightest{};
  CHECK(side_fill_tint(full, brightest) == std::array<std::uint8_t, 3>{255, 255, 255});
  // Halfway through a fade to black.
  Palette half;
  half.colors.fill(Rgb{128, 128, 128});
  CHECK(side_fill_tint(half, brightest) == std::array<std::uint8_t, 3>{128, 128, 128});
  // Rigged for red: only the red channel keeps its brightness.
  Palette red;
  red.colors.fill(Rgb{255, 40, 20});
  CHECK(side_fill_tint(red, brightest) == std::array<std::uint8_t, 3>{255, 40, 20});
  // Black: no fill.
  CHECK(side_fill_tint(Palette{}, brightest) == std::array<std::uint8_t, 3>{0, 0, 0});
  CHECK(brightest == std::array<std::uint8_t, 3>{255, 255, 255});
}

// Sends a mouse motion to window coordinates (`x`, `y`) of the display's
// window and returns the event the game receives.
std::optional<InputEvent> move_mouse(Display& display, float x, float y) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  REQUIRE(count == 1);
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.windowID = SDL_GetWindowID(windows[0]);
  e.motion.x = x;
  e.motion.y = y;
  SDL_free(windows);
  REQUIRE(SDL_PushEvent(&e));
  // The last motion: SDL may deliver the previous one again first.
  std::optional<InputEvent> last;
  for (std::optional<InputEvent> in = display.poll(); in; in = display.poll()) {
    if (in->type == EventType::kMouseMove) {
      last = in;
    }
  }
  return last;
}

TEST_CASE("Wide screen: mouse positions are those of the frame shown", "[wide]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  SECTION("Wide display: a 640 frame centred, then a frame as wide as the screen") {
    Display display("test", 1, Scaling::kSharp, true, 1708, 960);
    while (display.poll()) {
    }
    CHECK(display.screen_width() == 854);
    display.present(Image8(kScreenWidth, kScreenHeight), palette);
    std::optional<InputEvent> e = move_mouse(display, 854, 480);
    REQUIRE(e);
    CHECK(e->x == 320);
    CHECK(e->y == 240);
    // Over the left side fill: left of the frame.
    e = move_mouse(display, 100, 480);
    REQUIRE(e);
    CHECK(e->x == 50 - 107);
    display.present(Image8(854, kScreenHeight), palette);
    e = move_mouse(display, 854, 480);
    REQUIRE(e);
    CHECK(e->x == 427);
    CHECK_THROWS_AS(display.present(Image8(kMaxScreenWidth + 1, kScreenHeight), palette), std::invalid_argument);
  }
  SECTION("Faithful display: 640x480 letterboxed in the same window") {
    Display display("test", 1, Scaling::kSharp, false, 1708, 960);
    while (display.poll()) {
    }
    CHECK(display.screen_width() == kScreenWidth);
    display.present(Image8(kScreenWidth, kScreenHeight), palette);
    const std::optional<InputEvent> e = move_mouse(display, 854, 480);
    REQUIRE(e);
    CHECK(e->x == 320);
    CHECK(e->y == 240);
  }
}

TEST_CASE("Wide screen: the GUI's screen is resized in place with the mouse's bounds", "[wide]") {
  SH_REQUIRE_GAME_DATA(install);  // the skin's tables
  static_cast<void>(install);
  Image8 screen(kScreenWidth, kScreenHeight, 0);
  ui::GuiResources resources{};
  ui::DefaultSkin skin{resources};
  std::uint32_t tick = 0;
  ui::Gui gui(screen, [&] { return tick; }, skin);
  gui.set_screen_width(854);
  CHECK(&gui.screen() == &screen);
  CHECK(screen.width() == 854);
  CHECK(screen.height() == kScreenHeight);
  gui.mouse().update(900, 100, 0, 0);
  CHECK(gui.mouse().x() == 853);
  // Back to 640: the pointer is brought inside.
  gui.set_screen_width(kScreenWidth);
  CHECK(screen.width() == kScreenWidth);
  CHECK(gui.mouse().x() == 639);
}

}  // namespace
}  // namespace sh
