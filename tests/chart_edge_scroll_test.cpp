// The chart's edge scroll zones (`chart_edge_scroll_edges`): the wider
// margin over the map, the original's single row and column elsewhere, and
// the command bar's icons out of every zone; only the very edges in
// fullscreen and over the extended chart's bar, none on touch screens.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>

#include "game/stations/chart.h"
#include "game/stations/chart_panels.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

constexpr int kMargin = kChartEdgeScrollMargin;

ChartStation& open_chart(ControlRig& rig) {
  auto chart = std::make_unique<ChartStation>(rig.screen->env());
  ChartStation& ref = *chart;
  rig.screen->install(StationId::kChart, std::move(chart));
  // Away from the screen edges, which scroll the chart.
  rig.gui.mouse().update(320, 200, 0, 0);
  rig.frames(1);
  rig.settle();
  rig.key(0x3f00);  // F5
  rig.settle();
  rig.frames(2);
  return ref;
}

TEST_CASE("Chart edge scroll: the margin, and the original's over a panel", "[edgescroll]") {
  CHECK(chart_edge_scroll_edges(kMargin - 1, 200, false) == ChartEdges{.left = true});
  CHECK_FALSE(chart_edge_scroll_edges(kMargin, 200, false).any());
  CHECK(chart_edge_scroll_edges(kScreenWidth - kMargin, 200, false) == ChartEdges{.right = true});
  CHECK_FALSE(chart_edge_scroll_edges(kScreenWidth - 1 - kMargin, 200, false).any());
  CHECK(chart_edge_scroll_edges(300, kMargin - 1, false) == ChartEdges{.top = true});
  CHECK_FALSE(chart_edge_scroll_edges(300, kMargin, false).any());
  CHECK(chart_edge_scroll_edges(300, kScreenHeight - kMargin, false) == ChartEdges{.bottom = true});
  CHECK_FALSE(chart_edge_scroll_edges(300, kScreenHeight - 1 - kMargin, false).any());
  CHECK(chart_edge_scroll_edges(0, kScreenHeight - 1, false) == ChartEdges{.left = true, .bottom = true});
  // Over a panel: the original's x < 1, x > 0x27e, y < 1 and y > 0x1de.
  CHECK(chart_edge_scroll_edges(0, 200, true) == ChartEdges{.left = true});
  CHECK_FALSE(chart_edge_scroll_edges(1, 200, true).any());
  CHECK(chart_edge_scroll_edges(0x27f, 200, true) == ChartEdges{.right = true});
  CHECK_FALSE(chart_edge_scroll_edges(0x27e, 200, true).any());
  CHECK(chart_edge_scroll_edges(300, 0, true) == ChartEdges{.top = true});
  CHECK_FALSE(chart_edge_scroll_edges(300, 1, true).any());
  CHECK(chart_edge_scroll_edges(300, 0x1df, true) == ChartEdges{.bottom = true});
  CHECK_FALSE(chart_edge_scroll_edges(300, 0x1de, true).any());
}

TEST_CASE("Chart edge scroll: the chart scrolls at the margin; its bar has no button", "[edgescroll]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig);
  chart.view().set_zoom(8 << 16);
  chart.center_button();
  rig.frames(2);
  const std::int32_t cx = chart.view().center_x();
  const std::int32_t cy = chart.view().center_y();
  // Just beyond the margin: nothing.
  rig.gui.mouse().update(kMargin, 200, 0, 0);
  rig.frames(2);
  rig.gui.mouse().update(300, kScreenHeight - 1 - kMargin, 0, 0);
  rig.frames(2);
  CHECK(chart.view().center_x() == cx);
  CHECK(chart.view().center_y() == cy);
  // At the margin: west, the pointer moved back 30 pixels.
  rig.gui.mouse().update(kMargin - 1, 200, 0, 0);
  rig.frames(1);
  CHECK(chart.view().center_x() < cx);
  CHECK(rig.gui.mouse().x() == kMargin - 1 + 30);
  // On the bottom margin (the bar under the map): south.
  rig.gui.mouse().update(300, kScreenHeight - kMargin, 0, 0);
  rig.frames(1);
  CHECK(chart.view().center_y() > cy);
  CHECK(rig.gui.mouse().y() == kScreenHeight - kMargin - 30);

  // The chart's command bar (`control_bar_open` with no button, clock nor
  // time compression) has key-only hotspots: no click on it can be taken
  // for a scroll. The only clickable areas are the map and the open panel.
  const ui::HotspotTable& hotspots = rig.gui.hotspots();
  for (int i = 0; i < static_cast<int>(hotspots.size()); ++i) {
    const ui::Hotspot& h = hotspots[i];
    INFO("hotspot " << i << ": " << h.x0 << "," << h.y0 << ".." << h.x1 << "," << h.y1);
    CHECK((h.x0 < 0 || h.y1 <= 0x1ca));
  }
}

TEST_CASE("Chart edge scroll: only the very edges in fullscreen", "[edgescroll]") {
  using ui::PointerEdges;
  // In a window, the band.
  CHECK(chart_edge_scroll_edges(3, 200, false, PointerEdges::kBand) == ChartEdges{.left = true});
  // In fullscreen, where the pointer is held in the picture, the edge
  // rows and columns themselves.
  for (const auto& [x, y, edges] :
       {std::tuple{0, 200, ChartEdges{.left = true}}, std::tuple{kScreenWidth - 1, 200, ChartEdges{.right = true}},
        std::tuple{300, 0, ChartEdges{.top = true}}, std::tuple{300, kScreenHeight - 1, ChartEdges{.bottom = true}}}) {
    INFO(x << "," << y);
    CHECK(chart_edge_scroll_edges(x, y, false, PointerEdges::kExact) == edges);
  }
  CHECK_FALSE(chart_edge_scroll_edges(1, 200, false, PointerEdges::kExact).any());
  CHECK_FALSE(chart_edge_scroll_edges(kScreenWidth - 2, 200, false, PointerEdges::kExact).any());
  CHECK_FALSE(chart_edge_scroll_edges(300, 1, false, PointerEdges::kExact).any());
  CHECK_FALSE(chart_edge_scroll_edges(300, kScreenHeight - 2, false, PointerEdges::kExact).any());
}

TEST_CASE("Chart edge scroll: none on touch screens", "[edgescroll]") {
  for (const auto& [x, y] : {std::pair{0, 0}, {0, 200}, {kScreenWidth - 1, kScreenHeight - 1}, {300, 0}}) {
    CHECK_FALSE(chart_edge_scroll_edges(x, y, false, ui::PointerEdges::kNone).any());
    CHECK_FALSE(chart_edge_scroll_edges(x, y, true, ui::PointerEdges::kNone).any());
  }
}

TEST_CASE("Chart edge scroll: over a command bar, only the screen's last row", "[edgescroll]") {
  constexpr int kBarTop = 0x1cb;
  // Over the bar's buttons, nothing...
  CHECK_FALSE(chart_edge_scroll_edges(300, kScreenHeight - kMargin, false, ui::PointerEdges::kBand, kBarTop).any());
  CHECK_FALSE(chart_edge_scroll_edges(kScreenWidth - 3, 470, false, ui::PointerEdges::kBand, kBarTop).any());
  CHECK_FALSE(chart_edge_scroll_edges(3, 470, false, ui::PointerEdges::kBand, kBarTop).any());
  // ... but the screen's edges.
  CHECK(chart_edge_scroll_edges(300, kScreenHeight - 1, false, ui::PointerEdges::kBand, kBarTop) ==
        ChartEdges{.bottom = true});
  CHECK(chart_edge_scroll_edges(0, 470, false, ui::PointerEdges::kBand, kBarTop) == ChartEdges{.left = true});
  // The map above the bar keeps the band.
  CHECK(chart_edge_scroll_edges(3, 200, false, ui::PointerEdges::kBand, kBarTop) == ChartEdges{.left = true});
}

TEST_CASE("Chart edge scroll: the extended chart over its bar, and no scroll on a touch screen",
          "[edgescroll]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const bool touch : {false, true}) {
    ControlRig rig(install);
    rig.screen->env().ui.features.chart_tools = !touch;
    if (touch) {
      rig.gui.mouse().set_edges(ui::PointerEdges::kNone);
    }
    ChartStation& chart = open_chart(rig);
    chart.view().set_zoom(8 << 16);
    chart.center_button();
    rig.frames(2);
    const std::int32_t cx = chart.view().center_x();
    const std::int32_t cy = chart.view().center_y();
    rig.gui.mouse().update(300, kScreenHeight - kMargin, 0, 0);
    rig.frames(2);
    CHECK(chart.view().center_y() == cy);
    rig.gui.mouse().update(300, kScreenHeight - 1, 0, 0);
    rig.frames(1);
    CHECK((chart.view().center_y() > cy) == !touch);
    rig.gui.mouse().update(0, 200, 0, 0);
    rig.frames(1);
    CHECK((chart.view().center_x() < cx) == !touch);
  }
}

}  // namespace
}  // namespace sh
