// The window and its pointer on modern systems (not in the original): the
// desktop window kept to the aspect ratios the screen fills, and the
// pointer free beyond the frame (beside a 640-wide frame on a wide screen,
// over the black bars), drawn there while the game's coordinates stay
// within its screen.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <optional>
#include <utility>
#include <vector>

#include "assets/shp.h"
#include "core/image.h"
#include "platform/display.h"
#include "synthetic/encoders.h"
#include "ui/event.h"
#include "ui/mouse.h"

namespace sh {
namespace {

// Returns the display's window (the only one).
SDL_Window* only_window() {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  REQUIRE(count == 1);
  SDL_Window* window = windows[0];
  SDL_free(windows);
  return window;
}

// Drains the display's events (the window's own).
void drain(Display& display) {
  while (display.poll()) {
  }
}

// Sends a mouse motion to window coordinates (`x`, `y`) and returns the
// last motion the game receives.
std::optional<InputEvent> move_mouse(Display& display, float x, float y) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.windowID = SDL_GetWindowID(only_window());
  e.motion.x = x;
  e.motion.y = y;
  REQUIRE(SDL_PushEvent(&e));
  std::optional<InputEvent> last;
  for (std::optional<InputEvent> in = display.poll(); in; in = display.poll()) {
    if (in->type == EventType::kMouseMove) {
      last = in;
    }
  }
  return last;
}

// A 5x5 cursor of colour 7 with its hotspot at its centre.
Shp cursor_sprites() {
  std::vector<test::SpriteImage> frames(1);
  frames[0].pixels = Image8(5, 5, 7);
  frames[0].hotspot_x = 2;
  frames[0].hotspot_y = 2;
  return Shp::parse(test::encode_shp(frames), "cursor");
}

// What the mouse handed to its host's drawing beyond the screen.
struct Beyond {
  const SpriteFrame* frame = nullptr;
  int x = 0;
  int y = 0;
};

TEST_CASE("Window: the aspect ratios the screen fills without bars", "[window]") {
  // Without the wide screen: 4:3 alone.
  const AspectRange classic = window_aspect_range(false);
  CHECK(classic.min == classic.max);
  CHECK(classic.min * 3 == 4.0f);
  // With it: 4:3 to the widest layout, 21:9.
  const AspectRange wide = window_aspect_range(true);
  CHECK(wide.min == classic.min);
  CHECK(wide.max * kScreenHeight == static_cast<float>(kMaxScreenWidth));
  CHECK(wide.max * 9 == 21.0f);
}

TEST_CASE("Window: kept to its aspect ratios, which follow the wide screen", "[window]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  int w = 0;
  int h = 0;
  SECTION("Classic: a 16:9 window is narrowed to 4:3, without bars") {
    Display display("test", 1, Scaling::kSharp, false, 1708, 960);
    drain(display);
    SDL_GetWindowSize(only_window(), &w, &h);
    CHECK(w == 1280);
    CHECK(h == 960);
    display.present(Image8(kScreenWidth, kScreenHeight), palette);
    const std::optional<InputEvent> e = move_mouse(display, 2, 2);
    REQUIRE(e);
    CHECK(e->x == 1);
    CHECK(e->y == 1);
  }
  SECTION("Wide: up to 21:9, back to 4:3 when the wide screen is turned off") {
    Display display("test", 1, Scaling::kSharp, true, 2560, 960);
    drain(display);
    SDL_GetWindowSize(only_window(), &w, &h);
    CHECK(w == 2240);
    CHECK(h == 960);
    CHECK(display.screen_width() == kMaxScreenWidth);
    // 16:9 is within the range: kept.
    SDL_SetWindowSize(only_window(), 1708, 960);
    drain(display);
    CHECK(display.screen_width() == 854);
    display.set_wide_aspect(false);
    drain(display);
    SDL_GetWindowSize(only_window(), &w, &h);
    CHECK(w == 1280);
    CHECK(h == 960);
    CHECK(display.screen_width() == kScreenWidth);
  }
  SECTION("Created fullscreen: kept to 4:3 once back to a window") {
    Display display("test", 1, Scaling::kSharp, false, 1708, 960, true);
    drain(display);
    display.set_fullscreen(false);
    drain(display);
    SDL_GetWindowSize(only_window(), &w, &h);
    CHECK(w == 1280);
    CHECK(h == 960);
  }
}

TEST_CASE("Pointer: beyond the screen, its cursor follows while the game's position stops", "[window]") {
  std::uint32_t tick = 0;
  ui::EventQueue events([&] { return tick; });
  ui::Mouse mouse(events);
  const Shp sprites = cursor_sprites();
  mouse.set_cursor(sprites, 0);
  mouse.show();
  std::vector<Beyond> shown;
  mouse.set_cursor_beyond([&shown](const SpriteFrame* frame, int x, int y) { shown.push_back({frame, x, y}); });
  Image8 frame(kScreenWidth, kScreenHeight, 0);

  SECTION("Inside: drawn in the frame alone, as the original draws it") {
    mouse.update(100, 50, 0, 0);
    mouse.draw(frame);
    CHECK(frame.at(98, 48) == 7);
    CHECK(frame.at(102, 52) == 7);
    CHECK(frame.at(103, 50) == 0);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].frame == nullptr);
    mouse.erase(frame);
    CHECK(frame.at(100, 50) == 0);
  }
  SECTION("Over the left side fill: the game at column 0, the cursor beyond") {
    mouse.update(-40, 200, 0, 0);
    CHECK(mouse.x() == 0);
    CHECK(mouse.y() == 200);
    CHECK(mouse.pointer_x() == -40);
    CHECK(events.get()->x == 0);
    mouse.draw(frame);
    // Nothing of it at the frame's edge.
    for (int y = 195; y <= 205; ++y) {
      CHECK(frame.at(0, y) == 0);
    }
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].frame == &sprites.frame(0));
    CHECK(shown[0].x == -40);
    CHECK(shown[0].y == 200);
  }
  SECTION("Across the bottom edge: the frame's part in it, the whole beyond") {
    mouse.update(300, 480, 0, 0);
    CHECK(mouse.y() == kScreenHeight - 1);
    mouse.draw(frame);
    CHECK(frame.at(300, 478) == 7);
    CHECK(frame.at(300, 477) == 0);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].frame != nullptr);
    CHECK(shown[0].y == 480);
  }
  SECTION("In the command bar's band across the window: the game follows") {
    mouse.set_band(ClipRect{-107, 400, 746, 479});
    mouse.update(-50, 450, 0, 0);
    CHECK(mouse.x() == -50);
    // Up out of the band, over the side fill: the game back at column 0.
    mouse.update(-50, 300, 0, 0);
    CHECK(mouse.x() == 0);
    CHECK(mouse.pointer_x() == -50);
    mouse.draw(frame);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].frame != nullptr);
    CHECK(shown[0].x == -50);
  }
  SECTION("In a station's rows added to the band: the game follows there too") {
    mouse.set_band_rows(std::pair{79, 278});
    // Without a band, the rows alone reach nothing.
    mouse.update(-50, 200, 0, 0);
    CHECK(mouse.x() == 0);
    mouse.set_band(ClipRect{-107, 400, 746, 479});
    mouse.update(-50, 200, 0, 0);
    CHECK(mouse.x() == -50);
    mouse.update(700, 278, 0, 0);
    CHECK(mouse.x() == 700);
    // Above the rows, then the rows removed under the pointer.
    mouse.update(700, 78, 0, 0);
    CHECK(mouse.x() == kScreenWidth - 1);
    mouse.update(-50, 200, 0, 0);
    mouse.set_band_rows(std::nullopt);
    CHECK(mouse.x() == 0);
    mouse.update(-60, 450, 0, 0);
    CHECK(mouse.x() == -60);
  }
  SECTION("Hidden: drawn nowhere") {
    mouse.update(-40, 200, 0, 0);
    mouse.hide();
    mouse.draw(frame);
    REQUIRE(shown.size() == 1);
    CHECK(shown[0].frame == nullptr);
  }
  SECTION("Frozen: neither moves") {
    mouse.update(10, 10, 0, 0);
    mouse.freeze_motion();
    mouse.update(-40, 200, 0, 0);
    CHECK(mouse.x() == 10);
    CHECK(mouse.pointer_x() == 10);
  }
}

TEST_CASE("Pointer: over a wide screen's side fill, beside a 640-wide frame", "[window]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  const Palette palette;
  // A 854-wide screen in a 1708x960 window: the frame at columns 107..746.
  Display display("test", 1, Scaling::kSharp, true, 1708, 960);
  drain(display);
  display.present(Image8(kScreenWidth, kScreenHeight), palette);
  std::uint32_t tick = 0;
  ui::EventQueue events([&] { return tick; });
  ui::Mouse mouse(events);
  const auto feed = [&](float x, float y) {
    const std::optional<InputEvent> e = move_mouse(display, x, y);
    REQUIRE(e);
    mouse.update(e->x, e->y, 0, 0);
  };
  // Just left of the frame.
  feed(213, 100);
  CHECK(mouse.pointer_x() == -1);
  CHECK(mouse.x() == 0);
  feed(20, 100);
  CHECK(mouse.pointer_x() == -97);
  CHECK(mouse.x() == 0);
  // Right of it.
  feed(1700, 100);
  CHECK(mouse.pointer_x() == 850 - 107);
  CHECK(mouse.x() == kScreenWidth - 1);
  // The cursor beyond the frame is drawn without failing.
  display.set_cursor_beyond(cursor_sprites().frame(0).decode(), -99, 98);
  display.present(Image8(kScreenWidth, kScreenHeight), palette);
  display.set_cursor_beyond(std::nullopt);
  display.present(Image8(kScreenWidth, kScreenHeight), palette);
}

}  // namespace
}  // namespace sh
