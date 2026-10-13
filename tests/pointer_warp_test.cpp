// The judgement of the pointer's warps from the motions after them: honoured
// on the desktops that move the pointer, refused on a Wayland compositor
// that leaves it where it was.
#include <catch2/catch_test_macros.hpp>

#include "platform/pointer_warp.h"

namespace sh {
namespace {

// A warp from the screen's left edge 60 window points inward, as the
// charts' edge scroll asks for at 2x.
constexpr WindowPoint kEdge{0, 400};
constexpr WindowPoint kInside{60, 400};

// Warps from the edge inward, with SDL's echo at the target, then reports
// `at` (where the user's next motion lands) `reports` times.
void warp_then_report(WarpCheck& check, WindowPoint at, int reports = 1) {
  check.warped(kEdge, kInside);
  check.reported(kInside);
  for (int i = 0; i < reports; ++i) {
    check.reported(at);
  }
}

TEST_CASE("WarpCheck: honoured warps, the motions going on from the target", "[pointer]") {
  WarpCheck check;
  for (int i = 0; i < 10; ++i) {
    warp_then_report(check, {62, 401}, 3);
    // Pushed back toward the edge, past the halfway point: the warp is
    // already judged.
    check.reported({10, 400});
    check.reported({0, 400});
  }
  CHECK_FALSE(check.refused());
}

TEST_CASE("WarpCheck: refused warps, the motions going on from the start", "[pointer]") {
  WarpCheck check;
  SECTION("Judged at the fourth report") {
    for (int i = 0; i < WarpCheck::kRefusals - 1; ++i) {
      warp_then_report(check, {2, 398}, WarpCheck::kReports);
      CHECK_FALSE(check.refused());
    }
    warp_then_report(check, {0, 396}, WarpCheck::kReports - 1);
    CHECK_FALSE(check.refused());
    check.reported({0, 395});
    CHECK(check.refused());
  }
  SECTION("Judged at the next warp, after a single report") {
    for (int i = 0; i < WarpCheck::kRefusals; ++i) {
      warp_then_report(check, {1, 400});
      CHECK_FALSE(check.refused());
    }
    check.warped(kEdge, kInside);
    CHECK(check.refused());
    // For good: an honoured warp does not undo it.
    warp_then_report(check, kInside, 2);
    check.reported({61, 400});
    CHECK(check.refused());
  }
}

TEST_CASE("WarpCheck: a fast motion against an honoured warp is not enough", "[pointer]") {
  WarpCheck check;
  // A refusal run broken by an honoured warp starts again.
  for (int i = 0; i < 5; ++i) {
    warp_then_report(check, {5, 400}, WarpCheck::kReports);
    warp_then_report(check, {50, 410});
  }
  CHECK_FALSE(check.refused());
  for (int i = 0; i < WarpCheck::kRefusals - 1; ++i) {
    warp_then_report(check, {5, 400}, WarpCheck::kReports);
  }
  CHECK_FALSE(check.refused());
}

TEST_CASE("WarpCheck: what proves nothing", "[pointer]") {
  WarpCheck check;
  SECTION("No motion after the warps, or only SDL's echo") {
    for (int i = 0; i < 10; ++i) {
      check.warped(kEdge, kInside);
      check.reported({60.5f, 400.5f});
    }
    CHECK_FALSE(check.refused());
  }
  SECTION("Warps too short to tell") {
    for (int i = 0; i < 10; ++i) {
      check.warped({100, 100}, {103, 100});
      check.reported({100, 100});
      check.reported({99, 100});
    }
    CHECK_FALSE(check.refused());
  }
  SECTION("The pointer leaving the window and coming back anywhere") {
    for (int i = 0; i < 10; ++i) {
      check.warped(kEdge, kInside);
      check.left();
      check.reported({1, 400});
    }
    CHECK_FALSE(check.refused());
  }
}

}  // namespace
}  // namespace sh
