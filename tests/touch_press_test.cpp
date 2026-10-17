// Which touches press the emulated mouse button: none starting in the bars
// beside the screen, and those starting at a side edge (where Android's back
// gesture starts) only once lifted without the system cancelling them.
#include <catch2/catch_test_macros.hpp>

#include "platform/touch_press.h"

namespace sh {
namespace {

constexpr std::int64_t kFinger = 7;

TEST_CASE("TouchPress: the back-gesture zone at each side edge", "[touch]") {
  // A 2400 pixel wide phone at 2.75 pixels per dp: 88 pixels each side.
  CHECK(TouchPress::at_side_edge(0, 2400, 2.75f));
  CHECK(TouchPress::at_side_edge(87, 2400, 2.75f));
  CHECK_FALSE(TouchPress::at_side_edge(88, 2400, 2.75f));
  CHECK_FALSE(TouchPress::at_side_edge(1200, 2400, 2.75f));
  CHECK_FALSE(TouchPress::at_side_edge(2311, 2400, 2.75f));
  CHECK(TouchPress::at_side_edge(2312, 2400, 2.75f));
  CHECK(TouchPress::at_side_edge(2399, 2400, 2.75f));
}

TEST_CASE("TouchPress: a touch on the screen presses at once", "[touch]") {
  TouchPress press;
  CHECK(press.down(true, false) == TouchPress::Kind::kPassed);
  CHECK_FALSE(press.drop_finger_down(kFinger));
  CHECK_FALSE(press.drop_finger(kFinger, false));
  CHECK_FALSE(press.up(false));
  CHECK(press.kind() == TouchPress::Kind::kNone);
}

TEST_CASE("TouchPress: a touch starting in the bars never presses", "[touch]") {
  TouchPress press;
  // Also at an edge: the back gesture over a letterboxed screen.
  CHECK(press.down(false, true) == TouchPress::Kind::kDropped);
  CHECK(press.drop_finger_down(kFinger));
  CHECK(press.drop_finger(kFinger, false));
  CHECK_FALSE(press.up(false));
  CHECK(press.drop_finger(kFinger, true));
  // The next touch is the screen's again.
  CHECK(press.down(true, false) == TouchPress::Kind::kPassed);
  CHECK_FALSE(press.drop_finger_down(kFinger + 1));
}

TEST_CASE("TouchPress: a touch at a side edge presses when lifted, unless cancelled", "[touch]") {
  TouchPress press;
  SECTION("A tap") {
    CHECK(press.down(true, true) == TouchPress::Kind::kHeld);
    CHECK(press.up(false));
  }
  SECTION("The system's back gesture") {
    CHECK(press.down(true, true) == TouchPress::Kind::kHeld);
    CHECK(press.drop_finger_down(kFinger));
    CHECK_FALSE(press.up(true));
    CHECK(press.drop_finger(kFinger, true));
  }
  CHECK(press.kind() == TouchPress::Kind::kNone);
  // Another finger, not the held touch's, is not dropped.
  CHECK_FALSE(press.drop_finger(kFinger + 1, false));
}

}  // namespace
}  // namespace sh
