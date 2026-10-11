// The extended variant's gestures without game data: the drag tracking,
// the value mapping of the precise dials, the dial scrubber's press, drag,
// tap, cancel and wheel, and the view drag's turn and inertia.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/fixed.h"
#include "game/stations/view_drag.h"
#include "ui/dial_scrub.h"
#include "ui/drag_gesture.h"
#include "ui/event.h"

namespace sh {

namespace {

ui::Event mouse(std::uint16_t flags, int x, int y, std::uint8_t buttons, std::uint8_t shift = 0) {
  ui::Event e;
  e.flags = flags;
  e.x = x;
  e.y = y;
  e.buttons = buttons;
  e.shift = shift;
  return e;
}
ui::Event press(int x, int y) { return mouse(ui::kEventLeftDown, x, y, ui::kButtonLeft); }
ui::Event drag_to(int x, int y, std::uint8_t shift = 0) {
  return mouse(ui::kEventMoved, x, y, ui::kButtonLeft, shift);
}
ui::Event release(int x, int y) { return mouse(ui::kEventLeftUp, x, y, 0); }
ui::Event wheel(int x, int y, int notches) {
  ui::Event e;
  e.x = x;
  e.y = y;
  e.wheel = notches;
  return e;
}
ui::Event key(std::uint16_t word) {
  ui::Event e;
  e.flags = ui::kEventKey;
  e.key = word;
  return e;
}

// A dial of 0..600 at (100..200, 100..200), recording what it is given.
struct FakeDial {
  std::int32_t order = 300;
  std::vector<std::int32_t> commits;
  std::vector<ui::Event> taps;
  int changes = 0;
  bool enabled = true;

  ui::ScrubTarget target(ui::ScrubScale scale = {.units_per_px = 1, .min = 0, .max = 600, .wrap = false, .snap = 1}) {
    return {.rect = {100, 100, 200, 200},
            .scale = scale,
            .current = [this] { return order; },
            .commit =
                [this](std::int32_t v) {
                  commits.push_back(v);
                  order = v;
                },
            .tap = [this](const ui::Event& e) { taps.push_back(e); },
            .changed = [this] { ++changes; },
            .enabled = [this] { return enabled; }};
  }
};

}  // namespace

TEST_CASE("controls: a press becomes a drag past the threshold", "[controls]") {
  ui::PointerDrag d;
  d.press(100, 100, 0);
  CHECK(d.move(103, 100, 1) == 0);
  CHECK(!d.dragging());
  CHECK(d.move(100, 104, 2) == 0);
  CHECK(!d.dragging());
  // Past 4 px: the whole motion since the press counts.
  CHECK(d.move(106, 101, 3) == 6);
  CHECK(d.dragging());
  CHECK(d.step_dy() == 1);
  CHECK(d.move(110, 101, 4) == 4);
  CHECK(d.dx() == 10);
  d.end();
  CHECK(!d.active());
  CHECK(d.move(200, 200, 5) == 0);
}

TEST_CASE("controls: the pointer speed is measured over the last 10 ticks", "[controls]") {
  ui::PointerDrag d;
  d.press(0, 0, 100);
  // 20 px a tick for 20 ticks: 2000 px/s.
  for (std::uint32_t t = 1; t <= 20; ++t) {
    d.move(static_cast<int>(t) * 20, 0, 100 + t);
  }
  CHECK(d.speed_x(120) == Catch::Approx(2000));
  // A pointer that stopped long ago has no speed.
  CHECK(d.speed_x(140) == 0);
  // Moves within one tick give no speed.
  ui::PointerDrag still;
  still.press(0, 0, 5);
  still.move(50, 0, 5);
  CHECK(still.speed_x(5) == 0);
}

TEST_CASE("controls: scrub rates, wrap, clamp and snap", "[controls]") {
  CHECK(ui::scrub_acceleration(0) == 1);
  CHECK(ui::scrub_acceleration(-300) == 1);
  CHECK(ui::scrub_acceleration(1500) == 5);
  CHECK(ui::scrub_acceleration(1e6) == 20);
  CHECK(ui::scrub_precision(60) == 1);
  CHECK(ui::scrub_precision(-61) == 0.25);
  CHECK(ui::scrub_precision(121) == 1.0 / 16);

  // Angles wrap round and snap to whole degrees.
  ui::ScrubValue course({.units_per_px = 2.5, .min = 0, .max = 3599, .wrap = true, .snap = 10}, 3550);
  course.add(46);
  CHECK(course.value() == 0);  // 3596 rounds to 3600, wraps to 0
  course.add(100);
  CHECK(course.value() == 100);
  course.add(-300);
  CHECK(course.value() == 3400);
  // Depths stop at their ends and do not run past them.
  ui::ScrubValue depth({.units_per_px = 0.5, .min = 0, .max = 600, .wrap = false, .snap = 1}, 590);
  depth.add(50);
  CHECK(depth.value() == 600);
  depth.add(-5);
  CHECK(depth.value() == 595);
  depth.add(-1000);
  CHECK(depth.value() == 0);
  CHECK(depth.start() == 590);
  // The spread's 9-tenth steps.
  ui::ScrubValue spread({.units_per_px = 2.5, .min = 0, .max = 3599, .wrap = true, .snap = 9}, 0);
  spread.add(-5);
  CHECK(spread.value() == 3591);
}

TEST_CASE("controls: a dial drag shows a pending value and gives it on release", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  const std::size_t i = s.add(dial.target());
  // Presses outside the dial are not taken.
  CHECK(!s.event(press(50, 50), 0));
  CHECK(!s.active());

  CHECK(s.event(press(150, 150), 0));
  CHECK(s.active());
  CHECK(!s.pending(i));  // no ghost before the drag
  CHECK(s.event(drag_to(152, 150), 1));
  CHECK(!s.pending(i));
  // 30 px right at 300 px/s or less: 30 units; the drag may leave the dial.
  CHECK(s.event(drag_to(160, 150), 5));
  CHECK(s.event(drag_to(180, 150), 15));
  CHECK(s.event(drag_to(230, 150), 40));
  REQUIRE(s.pending(i));
  CHECK(*s.pending(i) == 380);
  CHECK(dial.commits.empty());
  CHECK(dial.order == 300);
  CHECK(dial.changes > 0);
  CHECK(s.event(release(230, 150), 41));
  CHECK(dial.commits == std::vector<std::int32_t>{380});
  CHECK(!s.active());
  CHECK(!s.pending(i));
  CHECK(dial.taps.empty());
}

TEST_CASE("controls: a dial press without a drag is the original click", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  s.add(dial.target());
  CHECK(s.event(press(120, 130), 0));
  CHECK(s.event(drag_to(122, 131), 2));
  CHECK(s.event(release(122, 131), 4));
  REQUIRE(dial.taps.size() == 1);
  CHECK(dial.taps[0].x == 120);
  CHECK(dial.taps[0].y == 130);
  CHECK(dial.commits.empty());
}

TEST_CASE("controls: a dial drag back to its start gives nothing", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  s.add(dial.target());
  s.event(press(150, 150), 0);
  s.event(drag_to(170, 150), 20);
  s.event(drag_to(150, 150), 40);
  s.event(release(150, 150), 41);
  CHECK(dial.commits.empty());
  CHECK(dial.taps.empty());
}

TEST_CASE("controls: Escape or the right button cancels a dial drag", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  const std::size_t i = s.add(dial.target());
  s.event(press(150, 150), 0);
  s.event(drag_to(190, 150), 40);
  REQUIRE(s.pending(i));
  CHECK(s.event(mouse(ui::kEventRightDown, 190, 150, ui::kButtonLeft | ui::kButtonRight), 41));
  CHECK(!s.pending(i));
  // The rest of the press is taken, then nothing is.
  CHECK(s.event(mouse(ui::kEventRightUp, 190, 150, ui::kButtonLeft), 42));
  CHECK(s.event(drag_to(200, 150), 43));
  CHECK(s.event(release(200, 150), 44));
  CHECK(!s.event(mouse(ui::kEventMoved, 200, 150, 0), 45));
  CHECK(dial.commits.empty());
  CHECK(dial.taps.empty());

  s.event(press(150, 150), 50);
  s.event(drag_to(190, 150), 90);
  CHECK(s.event(key(ui::kKeyEsc), 91));
  CHECK(!s.active());
  CHECK(s.event(release(190, 150), 92));
  CHECK(dial.commits.empty());
  // Other keys are not taken.
  CHECK(!s.event(key(ui::kKeySpace), 93));
}

TEST_CASE("controls: the dial drag's precision and fine mode", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  const std::size_t i = s.add(dial.target());
  // 70 px under the press: a quarter of the rate.
  s.event(press(150, 150), 0);
  s.event(drag_to(150, 220), 10);
  s.event(drag_to(190, 220), 50);
  CHECK(*s.pending(i) == 310);
  s.event(release(190, 220), 51);
  CHECK(dial.order == 310);
  // Shift: a tenth.
  s.event(press(150, 150), 60);
  s.event(drag_to(160, 150, ui::kShiftLeft), 70);
  s.event(drag_to(250, 150, ui::kShiftLeft), 160);
  CHECK(*s.pending(i) == 320);
}

TEST_CASE("controls: fast drags are accelerated", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  const std::size_t i = s.add(dial.target({.units_per_px = 1, .min = 0, .max = 5000, .wrap = false, .snap = 1}));
  s.event(press(150, 150), 0);
  // 10 px in a tick: 1000 px/s, x3.33.
  s.event(drag_to(160, 150), 1);
  CHECK(*s.pending(i) == 333);
  // 70 px in 2 ticks: 3500 px/s, x11.67 on the last 60 px.
  s.event(drag_to(220, 150), 2);
  CHECK(*s.pending(i) == 1033);
}

TEST_CASE("controls: the wheel steps a dial and gives it later", "[controls]") {
  FakeDial dial;
  ui::DialScrubber s;
  const std::size_t i = s.add(dial.target({.units_per_px = 1, .min = 0, .max = 600, .wrap = false, .snap = 5}));
  CHECK(!s.event(wheel(10, 10, 1), 0));
  CHECK(s.event(wheel(150, 150, 1), 0));
  CHECK(s.event(wheel(150, 150, 2), 10));
  CHECK(*s.pending(i) == 315);
  s.process(10 + ui::kWheelCommitTicks - 1);
  CHECK(dial.commits.empty());
  s.process(10 + ui::kWheelCommitTicks);
  CHECK(dial.commits == std::vector<std::int32_t>{315});
  // A press gives a pending wheel value first.
  s.event(wheel(150, 150, -1), 100);
  s.event(press(150, 150), 101);
  CHECK(dial.commits == std::vector<std::int32_t>{315, 310});
}

TEST_CASE("controls: a disabled dial takes nothing, a moved one its new place", "[controls]") {
  FakeDial dial;
  dial.enabled = false;
  ui::DialScrubber s;
  const std::size_t i = s.add(dial.target());
  CHECK(!s.event(press(150, 150), 0));
  CHECK(!s.event(wheel(150, 150, 1), 0));
  dial.enabled = true;
  s.set_rect(i, {300, 300, 310, 310});
  CHECK(!s.event(press(150, 150), 0));
  CHECK(s.event(press(305, 305), 0));
}

TEST_CASE("controls: a view pixel spans the angle at the magnification", "[controls]") {
  CHECK(view_tenths_per_pixel(Fixed16::from_int(1)) == Catch::Approx(1.2456).epsilon(1e-3));
  CHECK(view_tenths_per_pixel(Fixed16::from_int(8)) == Catch::Approx(1.2456 / 8).epsilon(1e-3));
  CHECK(view_drag_acceleration(100) == 1);
  CHECK(view_drag_acceleration(1200) == 2);
  CHECK(view_drag_acceleration(1e5) == 4);
}

namespace {

// A view of 1 tenth a pixel recording its turns.
struct FakeView {
  double across = 0;
  double up = 0;
  int captures = 0;
  int releases = 0;
  std::vector<bool> cursors;

  ViewDrag::Hooks hooks(bool inertia = true, double up_rate = 0) {
    ViewDrag::Hooks h;
    h.tenths_per_px = [] { return 1.0; };
    h.capture = [this] { ++captures; };
    h.turn = [this](double a, double u) {
      across = a;
      up = u;
    };
    h.release_dialog = [this] { ++releases; };
    h.cursor = [this](bool on) { cursors.push_back(on); };
    h.inertia = inertia;
    h.up_tenths_per_px = up_rate;
    return h;
  }
};

}  // namespace

TEST_CASE("controls: a view drag turns the view the other way", "[controls]") {
  FakeView v;
  ViewDrag drag(v.hooks());
  drag.set_regions({{0, 0, 99, 99}});
  // The press reaches the station's dialog.
  CHECK(!drag.event(press(150, 50), 0));
  CHECK(v.captures == 0);
  CHECK(!drag.event(press(50, 50), 0));
  CHECK(v.captures == 1);
  CHECK(!drag.event(drag_to(53, 50), 1));
  CHECK(v.releases == 0);
  // Past the threshold: taken, the dialog released, the sea follows.
  CHECK(drag.event(drag_to(60, 50), 10));
  CHECK(v.releases == 1);
  CHECK(v.cursors == std::vector<bool>{true});
  CHECK(v.across == -10);
  CHECK(drag.event(drag_to(20, 50), 50));
  CHECK(v.across == 30);
  CHECK(drag.event(release(20, 50), 100));
  CHECK(!drag.turning());
  CHECK(v.cursors == std::vector<bool>{true, false});
  // A press and release without a drag stays the dialog's.
  CHECK(!drag.event(press(50, 50), 200));
  CHECK(!drag.event(release(50, 50), 201));
}

TEST_CASE("controls: a fast release keeps the view turning, slower and slower", "[controls]") {
  FakeView v;
  ViewDrag drag(v.hooks());
  drag.set_regions({{0, 0, 639, 199}});
  drag.event(press(300, 50), 0);
  // 2000 px/s to the left: x3.33, then the turn goes on to the right.
  for (std::uint32_t t = 1; t <= 10; ++t) {
    drag.event(drag_to(300 - static_cast<int>(t) * 20, 50), t);
  }
  const double at_release = v.across;
  drag.event(release(100, 50), 10);
  REQUIRE(drag.turning());
  drag.update(20);
  const double first = v.across - at_release;
  CHECK(first > 0);
  drag.update(30);
  const double second = v.across - at_release - first;
  CHECK(second > 0);
  CHECK(second < first);
  // It stops on its own, short of rate x tau.
  for (std::uint32_t t = 40; t < 1000 && drag.turning(); t += 10) {
    drag.update(t);
  }
  CHECK(!drag.turning());
  CHECK(v.across - at_release < 2000 * (10.0 / 3) * kFlingTau + 1);
  // A press stops the turning at once.
  drag.event(press(300, 50), 2000);
  for (std::uint32_t t = 1; t <= 10; ++t) {
    drag.event(drag_to(300 - static_cast<int>(t) * 20, 50), 2000 + t);
  }
  drag.event(release(100, 50), 2010);
  REQUIRE(drag.turning());
  drag.event(press(500, 500), 2011);
  CHECK(!drag.turning());
}

TEST_CASE("controls: a view drag without inertia, with a vertical axis", "[controls]") {
  FakeView v;
  ViewDrag drag(v.hooks(false, 0.5));
  drag.set_regions({{0, 0, 639, 479}});
  drag.event(press(300, 200), 0);
  for (std::uint32_t t = 1; t <= 10; ++t) {
    drag.event(drag_to(300 - static_cast<int>(t) * 20, 200 + static_cast<int>(t) * 4), t);
  }
  CHECK(v.up == 20);
  drag.event(release(100, 240), 10);
  CHECK(!drag.turning());
}

}  // namespace sh
