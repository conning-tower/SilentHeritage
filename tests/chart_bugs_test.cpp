// Regression tests of user-reported chart bugs: the TIME COMPRESSION box's
// value drawn over a black box, no contact group on the zoomed-out chart of
// a single mission, and the telegraph's hub wiped while it is pressed.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

#include "game/patrol_contacts.h"
#include "game/stations/chart.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

ChartStation& open_chart(ControlRig& rig) {
  auto chart = std::make_unique<ChartStation>(rig.screen->env());
  ChartStation& ref = *chart;
  rig.screen->install(StationId::kChart, std::move(chart));
  rig.gui.mouse().update(320, 200, 0, 0);
  rig.frames(1);
  rig.settle();
  rig.key(0x3f00);  // F5
  rig.settle();
  rig.frames(2);
  return ref;
}

// Returns a hash of the screen pixels in [x0, x1] x [y0, y1].
std::uint64_t region_hash(const Image8& img, int x0, int y0, int x1, int y1) {
  std::uint64_t h = 1469598103934665603ull;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      h = (h ^ img.at(x, y)) * 1099511628211ull;
    }
  }
  return h;
}

}  // namespace

TEST_CASE("The TIME COMPRESSION value is not drawn over a black box", "[chartbugs]") {
  // Dialog 0x11fbf8: the value's text field has no fill (+0x30 = -1) and
  // uses the GUI colours (+0x2c = -1).
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  open_chart(rig);
  rig.screen->bar().menu_time_compression();
  rig.frames(3);
  const Image8& screen = rig.screen->screen();
  int black = 0;
  for (int y = 0xc3 + 0x34; y <= 0xc3 + 0x42; ++y) {
    for (int x = 0x97 + 100; x <= 0x97 + 0xee; ++x) {
      black += screen.at(x, y) == 0 ? 1 : 0;
    }
  }
  CHECK(black == 0);
}

TEST_CASE("A single mission's units form one route group shown on the zoomed-out chart", "[chartbugs]") {
  // init_spawn_scenario_units / convoy_group_from_scenario.
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ContactGroup* g = rig.mission().scenario_group();
  REQUIRE(g != nullptr);
  CHECK(g->kind == ContactGroupKind::kRoute);
  CHECK(g->materialized);
  CHECK(g->size == -1);
  std::size_t members = 0;
  for (const auto& v : rig.mission().sim().vehicles()) {
    if (dynamic_cast<const Ship*>(v.get()) != nullptr) {
      ++members;
    }
  }
  CHECK(members > 0);
  CHECK(g->vehicles.size() >= members);
  ChartStation& chart = open_chart(rig);
  REQUIRE(chart.view().zoom_int() <= 7);
  rig.frames(10);
  // The chart refreshed the contact from the nearest ship and drew it.
  CHECK(g->report.valid);
  CHECK(g->report.symbol_frame != 0);
}

TEST_CASE("A held telegraph keeps its hub while no overlay reaches the panel", "[chartbugs]") {
  // chart_view_redraw_overlays repaints the open panel only when an overlay
  // meets it; a repaint does not draw a pressed control.
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  // The NAVIGATION panel on the right, off the boat's marker.
  constexpr int kX = 0x160;
  constexpr int kY = 0x59;
  chart.restore_panels({ChartPanel::kNavigation, {0x16, 300, kX, kY, 0x16, 300}});
  rig.frames(3);
  REQUIRE(chart.panel() == ChartPanel::kNavigation);
  // The telegraph is at (18..128, 161..271) in the panel.
  const int tx = kX + 40;
  const int ty = kY + 193;
  rig.gui.mouse().update(tx, ty, 0, 0);
  rig.frames(1);
  rig.gui.mouse().update(tx, ty, ui::kButtonLeft, 0);
  rig.frames(1);
  const std::uint64_t pressed = region_hash(rig.screen->screen(), kX + 18, kY + 161, kX + 128, kY + 271);
  // Several overlay redraws (every 0x19 ticks) while held, then released.
  for (int i = 0; i < 15; ++i) {
    rig.frames(1);
    CHECK(region_hash(rig.screen->screen(), kX + 18, kY + 161, kX + 128, kY + 271) == pressed);
  }
  rig.gui.mouse().update(tx, ty, 0, 0);
  rig.frames(10);
  CHECK(region_hash(rig.screen->screen(), kX + 18, kY + 161, kX + 128, kY + 271) == pressed);
}

}  // namespace sh
