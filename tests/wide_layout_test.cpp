// The wide-screen layouts in a mission (`Features::wide_screen`): which
// stations open at the window's width, the side fill of the others, the
// command bar end to end, the menus centred, the chart on the map, the panel
// ends of the bridge and the TBT (`make_panel_ends`) and the wide bridge's
// deck.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "assets/world.h"
#include "core/features.h"
#include "core/side_fill.h"
#include "game/mission.h"
#include "game/stations/bridge.h"
#include "game/stations/chart_view.h"
#include "game/stations/panel_ends.h"
#include "game_data.h"
#include "golden_rig.h"
#include "synthetic/synthetic_install.h"
#include "ui/dialog.h"

namespace sh::test {
namespace {

constexpr int kWide = 854;

// The install of SH_DATA, else the synthetic one: the layouts hold on
// either.
const Install& data_or_synthetic() {
  static const std::optional<Install> data = game_install();
  return data ? *data : synthetic_install();
}
constexpr int kBarTop = 0x1cb;

std::unique_ptr<Mission> hist01(const GoldenGame& game) {
  MissionSetup setup;
  setup.random = WatcomRandom(7);
  return Mission::load(game.install, game.tables, "SCEN/HIST01.SCN", setup);
}

Features wide_screen() {
  Features f;
  f.wide_screen = true;
  return f;
}

// Returns the `w` x `h` block of `image` at (`x`, `y`).
Image8 block(const Image8& image, int x, int y, int w, int h) {
  Image8 out(w, h);
  for (int j = 0; j < h; ++j) {
    for (int i = 0; i < w; ++i) {
      out.set(i, j, image.at(x + i, y + j));
    }
  }
  return out;
}

bool same(const Image8& a, const Image8& b) {
  return a.width() == b.width() && a.height() == b.height() &&
         std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin());
}

TEST_CASE("Wide layout: the chart and the bridge take the window's width, the others are framed", "[wide]") {
  const Install& install = data_or_synthetic();
  const GoldenGame game(install);
  GoldenMissionRig rig(game, hist01(game), wide_screen(), kWide);
  const MissionUiState& ui = rig.screen().ui_state();
  rig.play("-60,f5,-30");
  CHECK(rig.screen().screen().width() == kWide);
  rig.play("f6,-30");
  CHECK(rig.screen().screen().width() == kScreenWidth);
  CHECK(ui.side_fill.kind == SideFill::Kind::kWood);
  rig.play("f1,-30");
  CHECK(rig.screen().screen().width() == kScreenWidth);
  CHECK(ui.side_fill.kind == SideFill::Kind::kBrass);
  rig.play("f4,-40");
  CHECK(rig.screen().screen().width() == kWide);
  // The TBT: its own picture continued past the borders, in its palette.
  rig.play("f3,-40");
  CHECK(rig.screen().screen().width() == kScreenWidth);
  REQUIRE(ui.side_fill.kind == SideFill::Kind::kPicture);
  CHECK(ui.side_fill.left.width() == (kMaxScreenWidth - kScreenWidth) / 2);
  CHECK(ui.side_fill.right.width() == (kMaxScreenWidth - kScreenWidth) / 2);
  CHECK(ui.side_fill.left.height() == kScreenHeight);
}

TEST_CASE("Wide layout: without the improvement every station stays 640 wide, with no side fill", "[wide]") {
  const Install& install = data_or_synthetic();
  const GoldenGame game(install);
  GoldenMissionRig rig(game, hist01(game), Features::faithful(), kWide);
  for (const char* key : {"f5", "f4", "f3", "f6"}) {
    rig.play(std::string("-30,") + key + ",-30");
    CHECK(rig.screen().screen().width() == kScreenWidth);
  }
  CHECK(rig.screen().ui_state().side_fill_changes == 0);
}

TEST_CASE("Wide layout: the bridge's command bar runs end to end, its art unscaled", "[wide]") {
  const Install& install = data_or_synthetic();
  const GoldenGame game(install);
  GoldenMissionRig narrow(game, hist01(game), Features::faithful());
  GoldenMissionRig wide(game, hist01(game), wide_screen(), kWide);
  narrow.play("-60,f4,-40");
  wide.play("-60,f4,-40");
  const Image8& n = narrow.screen().screen();
  const Image8& w = wide.screen().screen();
  constexpr int kButtons = 4 * 0x49;
  // The station buttons at the right edge, the clock box at the left.
  CHECK(same(block(w, kWide - kButtons, kBarTop, kButtons, 0x14),
             block(n, kScreenWidth - kButtons, kBarTop, kButtons, 0x14)));
  CHECK(same(block(w, 0, kBarTop, 0x48, 0x14), block(n, 0, kBarTop, 0x48, 0x14)));
}

TEST_CASE("Wide layout: the menus are centred on a wide screen", "[wide]") {
  const Install& install = data_or_synthetic();
  const GoldenGame game(install);
  GoldenMissionRig rig(game, hist01(game), wide_screen(), kWide);
  rig.play("-60,f5,-30");
  StationEnv& env = rig.screen().env();
  auto dialog = std::make_unique<ui::Dialog>(env.gui, 100, 120, 200, 100, ui::kDialogFramed | ui::kDialogCancel);
  dialog->emplace<ui::Area>(0, 0, 10, 10, nullptr, nullptr, 0x0d);
  ui::Dialog& shown = *dialog;
  int result = 0;
  env.modals.run_modal(std::move(dialog), ModalFrame::kMenu, [&](int r) { result = r; });
  rig.frames(2);
  CHECK(shown.x() == 100 + (kWide - kScreenWidth) / 2);
  CHECK(shown.y() == 120);
  rig.play("esc,-3");
  CHECK(result != 0);
}

TEST_CASE("Wide screen: the widest chart stays on the map, or centres a narrower map", "[wide]") {
  SH_REQUIRE_GAME_DATA(install);
  for (int n = 1; n <= 15; ++n) {
    const std::string name = std::format("WORLD-{}.WLD", n);
    const World world = parse_world(install.read("DATA/" + name), name);
    ChartView narrow(world, 0, 0, kScreenWidth - 1, 0x1ca);
    ChartView wide(world, 0, 0, kScreenWidth - 1, 0x1ca);
    wide.set_extent(kMaxScreenWidth - 1, 0x1ca);
    const std::int32_t map_w = (world.chart.width - 1) * 0x10000;
    for (const auto [x, y] : {std::pair{0, 0}, std::pair{1 << 30, 1 << 30}}) {
      narrow.set_center(x, y);
      wide.set_center(x, y);
      INFO(name << " centre " << x);
      if (map_w >= 2 * wide.half_width()) {
        // The map's edge at the view's edge.
        CHECK(wide.center_x() - wide.half_width() >= 0);
        CHECK(wide.center_x() + wide.half_width() <= map_w);
      } else {
        // A map narrower than the view: centred in it.
        CHECK(wide.center_x() == map_w / 2);
      }
      // The original's view fits every map.
      CHECK(narrow.center_x() - narrow.half_width() >= 0);
      CHECK(narrow.center_x() + narrow.half_width() <= map_w);
    }
  }
}

// A synthetic panel: bands in rows 0..9 (columns alternating 1, 2), plate
// colour 3 dithered with 4 below, the edge gauges' mounts drawn in colours
// 10 + column % 7 within radius 30 of (20, 50) and (620, 50), both cut by
// the borders.
Image8 synthetic_panel() {
  Image8 panel(kScreenWidth, 100);
  for (int y = 0; y < 100; ++y) {
    for (int x = 0; x < kScreenWidth; ++x) {
      std::uint8_t c = y < 10 ? static_cast<std::uint8_t>(1 + (x & 1)) : static_cast<std::uint8_t>(3 + ((x + y) & 1));
      for (const int cx : {20, 620}) {
        if ((x - cx) * (x - cx) + (y - 50) * (y - 50) <= 30 * 30) {
          c = static_cast<std::uint8_t>(10 + x % 7);
        }
      }
      panel.set(x, y, c);
    }
  }
  return panel;
}

TEST_CASE("Panel ends: bands, the mounts' cut parts from the other gauge, the plate verbatim", "[wide]") {
  const Image8 panel = synthetic_panel();
  const PanelEndsLayout layout{.band_rows = 10,
                               .centre_x2 = 40,
                               .centre_y = 50,
                               .gauge_shift = 600,
                               .mount_radius = 30,
                               .left_plate = {0, 60, 3, 99},
                               .right_plate = {636, 60, 639, 99}};
  const PanelEnds ends = make_panel_ends(panel, panel, 50, 60, layout);
  REQUIRE(ends.left.width() == 50);
  REQUIRE(ends.right.width() == 60);
  // Left of the panel, column `x` < 0 is ends.left column 50 + x.
  const auto left = [&](int x, int y) { return ends.left.at(50 + x, y); };
  const auto right = [&](int x, int y) { return ends.right.at(x - kScreenWidth, y); };
  for (int x = -50; x < 0; ++x) {
    // The bands keep their dither.
    CHECK(left(x, 5) == panel.at(x & 1, 5));
    // The plate keeps its dither phase.
    CHECK(left(x, 80) == 3 + ((x + 80) & 1));
  }
  // The left gauge's cut side: the right gauge's same side.
  CHECK(left(-5, 50) == panel.at(595, 50));
  CHECK(left(-5, 50) == 10 + 595 % 7);
  // Beyond the mount: plate.
  CHECK(left(-40, 50) == 3 + ((-40 + 50) & 1));
  // The right gauge's cut side from the left gauge's.
  CHECK(right(645, 50) == panel.at(45, 50));
  CHECK(right(645, 50) == 10 + 45 % 7);
  CHECK(right(699, 50) == 3 + ((699 + 50) & 1));
}

TEST_CASE("Wide bridge: the deck at every bearing, blended between its frames", "[wide]") {
  // Stepped: the frame of each 15 degrees, as the original's.
  WideDeckView v = bridge_wide_deck_view(Angle10(200), kWide, DeckTransition::kStepped);
  CHECK(v.frame == 1);
  CHECK(v.weight == 0);
  CHECK(v.mirror);
  // Looking 70 degrees to port: the side frame of 60 degrees, a third of
  // the way to the next, the deck's axis held near the screen's edge.
  v = bridge_wide_deck_view(Angle10(3600 - 700), kWide, DeckTransition::kWarpDither);
  CHECK(v.frame == 4);
  CHECK(v.next == 5);
  CHECK(v.weight == 10 * kDeckLevels / 15);
  CHECK_FALSE(v.mirror);
  CHECK(v.x == kWide - 1 - kWideDeckPin);
  // The stern's frames looking aft.
  v = bridge_wide_deck_view(Angle10(1800 + 300), kWide, DeckTransition::kWarp);
  CHECK(v.frame == 9 + 2);
  CHECK(v.weight == 0);
  // 130 degrees to starboard: the stern 50 degrees off, to port of it.
  v = bridge_wide_deck_view(Angle10(1300), kWide, DeckTransition::kWarpDither);
  CHECK(v.frame == 9 + 3);
  CHECK_FALSE(v.mirror);
}

}  // namespace
}  // namespace sh::test
