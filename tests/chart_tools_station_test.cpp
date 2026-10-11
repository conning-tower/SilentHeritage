// Tests of the extended chart on a mission (`Features::chart_tools`): the
// toolbox and its tools, clicks on the release, the drag scroll, the wheel
// zoom, the nautical scale and the "CHRT" save section; and, with the
// switch off, the original chart left as it was.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "game/stations/chart.h"
#include "core/byte_reader.h"
#include "game/sim_state.h"
#include "game_data.h"
#include "gfx/sprite.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;
using Kind = ChartAnnotation::Kind;

// The MAP CONTROLS panel's default place, its TOOLS button and the toolbox's
// buttons (panel coordinates).
constexpr int kPanelX = 0x16;
constexpr int kPanelY = 300;
constexpr std::array<int, 2> kToolsButton{kPanelX + 0x79 + 20, kPanelY + 0x2b + 10};
constexpr std::array<int, 2> kExitButton{kPanelX + 0xa2 + 20, kPanelY + 0x46 + 6};
constexpr std::array<int, 2> kClearButton{kPanelX + 0xe2 + 20, kPanelY + 0x46 + 6};
constexpr std::array<int, 2> kUndoButton{kPanelX + 0xa7 + 10, kPanelY + 0x2b + 10};
std::array<int, 2> tool_button(ChartTool tool) {
  return {kPanelX + 0xb + static_cast<int>(tool) * 22 + 10, kPanelY + 0x2b + 10};
}

// Opens a chart station of `features` on F5, the pointer away from the
// screen edges (which scroll the chart).
ChartStation& open_chart(ControlRig& rig, bool tools) {
  rig.screen->env().ui.features.chart_tools = tools;
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

// Centres the view on world (`x`, `z`) at `zoom`, placed at 1x (the
// chart position goes through the 16.16 screen position).
void look(ChartStation& chart, std::int32_t x, std::int32_t z, std::int32_t zoom) {
  chart.view().set_zoom(0x10000);
  const std::array<std::int32_t, 2> c = chart.view().world_to_chart(x, z);
  chart.view().set_zoom(zoom << 16);
  chart.view().set_center(c[0], c[1]);
  chart.invalidate();
}

// Drags the left (or `button`) button from `from` to `to` in `steps` frames.
void drag(ControlRig& rig, std::array<int, 2> from, std::array<int, 2> to, std::uint8_t button = ui::kButtonLeft,
          int steps = 4) {
  rig.gui.mouse().update(from[0], from[1], 0, 0);
  rig.frames(1);
  rig.gui.mouse().update(from[0], from[1], button, 0);
  rig.frames(1);
  for (int i = 1; i <= steps; ++i) {
    rig.gui.mouse().update(from[0] + (to[0] - from[0]) * i / steps, from[1] + (to[1] - from[1]) * i / steps, button,
                           0);
    rig.frames(1);
  }
  rig.gui.mouse().update(to[0], to[1], 0, 0);
  rig.frames(1);
}

void right_click(ControlRig& rig, int x, int y) {
  rig.gui.mouse().update(x, y, 0, 0);
  rig.frames(1);
  rig.gui.mouse().update(x, y, ui::kButtonRight, 0);
  rig.frames(1);
  rig.gui.mouse().update(x, y, 0, 0);
  rig.frames(1);
}

geom::WorldXZ world_at(const ChartStation& chart, int x, int y) {
  const std::array<std::int32_t, 2> w = chart.view().screen_to_world(x, y);
  return {w[0], w[1]};
}

// The state of the simulation: the state lists without the screens' parts,
// and the random generator.
std::vector<std::uint8_t> sim_bytes(ControlRig& rig) {
  SimStateFile f = capture_sim_state(rig.mission(), {}, {});
  f.ui.reset();
  std::vector<std::uint8_t> bytes = f.serialize();
  const std::uint32_t seed = rig.mission().sim().random().state();
  for (int i = 0; i < 4; ++i) {
    bytes.push_back(static_cast<std::uint8_t>(seed >> (8 * i)));
  }
  return bytes;
}

std::vector<std::uint8_t> screen_bytes(const ControlRig& rig) {
  const auto p = rig.screen_image.pixels();
  return {p.begin(), p.end()};
}

bool has_tag(const std::vector<std::uint8_t>& bytes, std::string_view tag) {
  return std::search(bytes.begin(), bytes.end(), tag.begin(), tag.end()) != bytes.end();
}

}  // namespace

TEST_CASE("TOOLS opens the chart toolbox and Esc, a right click or EXIT close it", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  rig.click(kToolsButton[0], kToolsButton[1]);
  rig.frames(2);
  CHECK(chart.panel() == ChartPanel::kTools);
  CHECK(chart.click_mode() == ChartClickMode::kAnnotate);
  rig.save("charttools_toolbox");
  rig.key(ui::kKeyEsc);
  CHECK(chart.panel() == ChartPanel::kMap);
  CHECK(chart.click_mode() == ChartClickMode::kSelectTarget);

  rig.click(kToolsButton[0], kToolsButton[1]);
  rig.frames(2);
  REQUIRE(chart.panel() == ChartPanel::kTools);
  right_click(rig, 400, 100);
  CHECK(chart.panel() == ChartPanel::kMap);

  rig.click(kToolsButton[0], kToolsButton[1]);
  rig.frames(2);
  REQUIRE(chart.panel() == ChartPanel::kTools);
  rig.click(kExitButton[0], kExitButton[1]);
  rig.frames(2);
  CHECK(chart.panel() == ChartPanel::kMap);
  CHECK(chart.click_mode() == ChartClickMode::kSelectTarget);
}

TEST_CASE("The extended chart selects a target on the release", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  REQUIRE(!rig.mission().ships().empty());
  Ship* ship = rig.mission().ships().front();
  look(chart, ship->position().x, ship->position().z, 64);
  rig.frames(2);
  const ChartPoint p = chart.view().world_to_screen(ship->position().x, ship->position().z);
  rig.gui.mouse().update(p.x, p.y, 0, 0);
  rig.frames(1);
  rig.gui.mouse().update(p.x, p.y, ui::kButtonLeft, 0);
  rig.frames(1);
  CHECK(rig.mission().player_combat().tdc.target() == nullptr);
  rig.gui.mouse().update(p.x, p.y, 0, 0);
  rig.frames(1);
  CHECK(rig.mission().player_combat().tdc.target() == ship);
}

TEST_CASE("Dragging the chart scrolls it and stops following the boat", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  const fx::Vec3 sub = rig.mission().player().position();
  look(chart, sub.x, sub.z, 8);
  rig.screen->env().options.chart_follow_sub = 1;
  rig.frames(2);
  // The point grabbed stays under the pointer.
  const auto grab = [&](std::array<int, 2> from, std::array<int, 2> to) {
    const geom::WorldXZ grabbed = world_at(chart, from[0], from[1]);
    drag(rig, from, to);
    const ChartPoint p = chart.view().world_to_screen(grabbed.x, grabbed.z);
    CHECK(std::abs(p.x - to[0]) <= 1);
    CHECK(std::abs(p.y - to[1]) <= 1);
  };
  grab({300, 200}, {340, 230});
  CHECK(rig.screen->env().options.chart_follow_sub == 0);
  CHECK(rig.mission().player_combat().tdc.target() == nullptr);

  // In plotting mode a drag scrolls too and adds no waypoint.
  chart.start_plotting();
  rig.frames(2);
  grab({340, 230}, {300, 200});
  CHECK(rig.mission().player().route().count == 0);
  rig.click(300, 200);
  CHECK(rig.mission().player().route().count == 1);
}

TEST_CASE("The wheel zooms about the pointer", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  rig.gui.mouse().update(400, 150, 0, 0);
  rig.frames(1);
  const std::array<std::int32_t, 2> before = chart.view().screen_to_chart(400, 150);
  rig.gui.mouse().post_wheel(1, 0);
  rig.frames(2);
  CHECK(chart.view().zoom() == 2 << 16);
  std::array<std::int32_t, 2> after = chart.view().screen_to_chart(400, 150);
  CHECK(std::abs(after[0] - before[0]) <= chart.view().step());
  CHECK(std::abs(after[1] - before[1]) <= chart.view().step());
  rig.gui.mouse().post_wheel(2, 0);
  rig.frames(2);
  CHECK(chart.view().zoom() == 8 << 16);
  after = chart.view().screen_to_chart(400, 150);
  CHECK(std::abs(after[0] - before[0]) <= chart.view().step());
  rig.gui.mouse().post_wheel(-3, 0);
  rig.frames(2);
  CHECK(chart.view().zoom() == 1 << 16);
  rig.gui.mouse().post_wheel(-1, 0);
  rig.frames(2);
  CHECK(chart.view().zoom() == 1 << 16);
  // Not over the panel.
  rig.gui.mouse().update(kToolsButton[0], kToolsButton[1], 0, 0);
  rig.frames(1);
  rig.gui.mouse().post_wheel(1, 0);
  rig.frames(2);
  CHECK(chart.view().zoom() == 1 << 16);
  // The zoom box grows and shrinks with it.
  rig.gui.mouse().update(400, 150, 0, 0);
  rig.frames(1);
  rig.key(0x2c5a);  // Z
  REQUIRE(chart.zoom_box() == 2);
  rig.gui.mouse().post_wheel(2, 0);
  rig.frames(2);
  CHECK(chart.zoom_box() == 8);
  rig.gui.mouse().post_wheel(-1, 0);
  rig.frames(2);
  CHECK(chart.zoom_box() == 4);
  CHECK(chart.view().zoom() == 1 << 16);
}

TEST_CASE("The chart tools draw, erase and undo", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  const fx::Vec3 sub = rig.mission().player().position();
  look(chart, sub.x, sub.z, 8);
  rig.click(kToolsButton[0], kToolsButton[1]);
  rig.frames(2);
  REQUIRE(chart.panel() == ChartPanel::kTools);
  const auto& items = chart.annotations().items();

  // A mark where clicked, no target.
  rig.click(tool_button(ChartTool::kMark)[0], tool_button(ChartTool::kMark)[1]);
  CHECK(chart.tool() == ChartTool::kMark);
  rig.click(450, 100);
  REQUIRE(items.size() == 1);
  CHECK(items[0].kind == Kind::kMark);
  CHECK(items[0].points[0] == world_at(chart, 450, 100));
  CHECK(rig.mission().player_combat().tdc.target() == nullptr);

  // A pencil stroke along the drag.
  chart.select_tool(ChartTool::kPencil);
  drag(rig, {100, 50}, {200, 90}, ui::kButtonLeft, 10);
  REQUIRE(items.size() == 2);
  CHECK(items[1].kind == Kind::kStroke);
  CHECK(items[1].points.size() >= 5);
  CHECK(items[1].points.front() == world_at(chart, 100, 50));
  CHECK(items[1].points.back() == world_at(chart, 200, 90));

  // A circle from its centre.
  chart.select_tool(ChartTool::kCircle);
  drag(rig, {400, 250}, {440, 250});
  REQUIRE(items.size() == 3);
  CHECK(items[2].kind == Kind::kCircle);
  CHECK(items[2].points[0] == world_at(chart, 400, 250));
  CHECK(items[2].points[1] == world_at(chart, 440, 250));

  // A line of three points, ended by a right click.
  chart.select_tool(ChartTool::kLine);
  rig.click(350, 60);
  rig.click(420, 60);
  REQUIRE(chart.draft());
  rig.click(420, 120);
  right_click(rig, 500, 200);
  CHECK(chart.panel() == ChartPanel::kTools);
  REQUIRE(items.size() == 4);
  CHECK(items[3].kind == Kind::kLine);
  CHECK(items[3].points.size() == 3);
  // Or by a click on its last point.
  rig.click(350, 170);
  rig.click(420, 170);
  rig.click(421, 171);
  REQUIRE(items.size() == 5);
  CHECK(items[4].points.size() == 2);

  // The ruler measures without drawing anything lasting.
  chart.select_tool(ChartTool::kRuler);
  drag(rig, {320, 229}, {420, 229});
  CHECK(items.size() == 5);
  REQUIRE(chart.ruler());
  CHECK((*chart.ruler())[1] == world_at(chart, 420, 229));
  rig.save("charttools_8x");
  rig.click(500, 350);
  CHECK_FALSE(chart.ruler());

  // The eraser frames, then removes, what is under it.
  chart.select_tool(ChartTool::kEraser);
  rig.gui.mouse().update(450, 100, 0, 0);
  rig.frames(2);
  rig.save("charttools_eraser");
  rig.click(450, 100);
  REQUIRE(items.size() == 4);
  CHECK(items[0].kind == Kind::kStroke);
  rig.click(kUndoButton[0], kUndoButton[1]);
  REQUIRE(items.size() == 5);
  CHECK(items[0].kind == Kind::kMark);

  // The tools need the toolbox: outside it a click selects as before.
  rig.key(ui::kKeyEsc);
  rig.click(450, 100);
  CHECK(items.size() == 5);
}

TEST_CASE("CLEAR asks before erasing the chart", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  rig.click(kToolsButton[0], kToolsButton[1]);
  rig.frames(2);
  chart.select_tool(ChartTool::kMark);
  rig.click(450, 100);
  rig.click(470, 100);
  REQUIRE(chart.annotations().items().size() == 2);
  rig.click(kClearButton[0], kClearButton[1]);
  rig.frames(5);
  CHECK(rig.screen->env().modals.modal_active());
  rig.save("charttools_clear");
  rig.key(0x314e);  // N
  rig.frames(5);
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  CHECK(chart.annotations().items().size() == 2);
  rig.click(kClearButton[0], kClearButton[1]);
  rig.frames(5);
  rig.key(0x1559);  // Y
  rig.frames(5);
  CHECK(chart.annotations().empty());
  // UNDO brings them back.
  chart.undo_annotation();
  CHECK(chart.annotations().items().size() == 2);
}

TEST_CASE("The note tool asks for the note", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  rig.click(kToolsButton[0], kToolsButton[1]);
  rig.frames(2);
  chart.select_tool(ChartTool::kNote);
  chart.select_ink(ChartInk::kRed);
  // The entry edits from the release of the click that placed the note;
  // the keys typed reach its editing loop through the pump.
  std::deque<std::uint16_t> pending{0x011b};  // Esc
  bool shot = false;
  rig.gui.set_pump([&rig, &pending, &shot] {
    ++rig.tick;
    if (!shot && pending.size() == 3) {
      rig.save("charttools_note");
      shot = true;
    }
    if (!pending.empty()) {
      rig.gui.mouse().post_key(pending.front(), 0);
      pending.pop_front();
    }
  });
  rig.click(450, 100);
  rig.frames(5);
  CHECK(pending.empty());
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  CHECK(chart.annotations().empty());
  CHECK(chart.panel() == ChartPanel::kTools);

  pending = {0x1e41, 0x3042, 0x1c0d};  // A, B, Enter
  rig.click(450, 100);
  rig.frames(5);
  CHECK(shot);
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  REQUIRE(chart.annotations().items().size() == 1);
  const ChartAnnotation& note = chart.annotations().items()[0];
  CHECK(note.kind == Kind::kNote);
  CHECK(note.text == "AB");
  CHECK(note.ink == ChartInk::kRed);
  CHECK(note.points[0] == world_at(chart, 450, 100));
  rig.gui.set_pump({});
}

TEST_CASE("Chart annotations are kept in the CHRT section of the state", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  SimStateFile saved;
  std::vector<ChartAnnotation> drawn;
  {
    ControlRig rig(install);
    ChartStation& chart = open_chart(rig, true);
    // None, no section.
    CHECK(capture_sim_state(rig.mission(), {}, {.screen = rig.screen.get()}).chart_extension.empty());
    rig.click(kToolsButton[0], kToolsButton[1]);
    rig.frames(2);
    chart.select_tool(ChartTool::kMark);
    rig.click(450, 100);
    chart.select_tool(ChartTool::kCircle);
    drag(rig, {400, 250}, {440, 250});
    drawn = chart.annotations().items();
    saved = capture_sim_state(rig.mission(), {}, {.screen = rig.screen.get()});
  }
  REQUIRE(drawn.size() == 2);
  CHECK(ChartAnnotations::decode(saved.chart_extension) == drawn);
  const std::vector<std::uint8_t> bytes = saved.serialize();
  CHECK(has_tag(bytes, "CHRT"));
  ByteReader in(bytes, "state");
  const SimStateFile parsed = SimStateFile::parse(in, 7);
  CHECK(parsed.chart_extension == saved.chart_extension);
  CHECK(parsed.serialize() == bytes);

  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  restore_session_state(parsed, {.screen = rig.screen.get()});
  CHECK(chart.annotations().items() == drawn);
  CHECK_FALSE(chart.annotations().can_undo());
}

TEST_CASE("The chart tools leave the simulation alone", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig used(install);
  ControlRig idle(install);
  ChartStation& chart = open_chart(used, true);
  open_chart(idle, true);
  used.click(kToolsButton[0], kToolsButton[1]);
  chart.select_tool(ChartTool::kPencil);
  drag(used, {100, 50}, {200, 90});
  chart.select_tool(ChartTool::kRuler);
  drag(used, {320, 229}, {420, 229});
  chart.select_tool(ChartTool::kMark);
  used.click(450, 100);
  chart.select_tool(ChartTool::kEraser);
  used.click(450, 100);
  chart.undo_annotation();
  used.key(ui::kKeyEsc);
  drag(used, {300, 200}, {340, 230});
  used.gui.mouse().update(400, 150, 0, 0);
  used.gui.mouse().post_wheel(2, 0);
  used.frames(2);
  used.gui.mouse().post_wheel(-1, 0);
  used.frames(2);
  // The same number of frames on both.
  const auto frames_of = [](const ControlRig& r) { return r.tick; };
  REQUIRE(frames_of(used) > frames_of(idle));
  idle.frames(static_cast<int>((frames_of(used) - frames_of(idle)) / 5));
  REQUIRE(frames_of(used) == frames_of(idle));
  CHECK(sim_bytes(used) == sim_bytes(idle));
}

TEST_CASE("The extended chart's scale is in nautical miles", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  ChartStation& chart = open_chart(rig, true);
  rig.save("charttools_scale_1x");
  // The bar: 80 nm at 1x, drawn up to its length.
  const ChartScale s = chart_nautical_scale(chart.view().zoom_int());
  CHECK(s == ChartScale{.length = 236, .value = 80, .yards = false});
  const int bar_x = kPanelX + 0x12;
  const int bar_y = kPanelY + 0x6f + 5;
  CHECK(rig.screen_image.at(bar_x + s.length, bar_y) == 2);
  CHECK(rig.screen_image.at(bar_x + s.length + 2, bar_y) == 6);
}

TEST_CASE("The nautical bar is drawn as CHART.SHP's at full length", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  const Shp sprites = Shp::load(install, paths.path(GameDir::kArt, "CHART.SHP"));
  const SpriteFrame& bar = sprites.frame(0x12);
  Image8 original(bar.width() + 4, bar.height() + 4, 6);
  Image8 drawn(bar.width() + 4, bar.height() + 4, 6);
  draw_sprite_clipped(Viewport::full(original), bar, 0, 0);
  draw_chart_scale_bar(Viewport::full(drawn), 0, 0, 256);
  CHECK(std::ranges::equal(original.pixels(), drawn.pixels()));
}

// --- With the switch off ---------------------------------------------------------

TEST_CASE("The original chart ignores the wheel, drags and the TOOLS spot", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig scripted(install);
  ControlRig plain(install);
  ChartStation& chart = open_chart(scripted, false);
  open_chart(plain, false);
  const std::int32_t zoom = chart.view().zoom();
  const std::int32_t cx = chart.view().center_x();
  // The wheel, a drag from (300, 200) and a click where TOOLS would be...
  scripted.gui.mouse().update(400, 150, 0, 0);
  scripted.gui.mouse().post_wheel(2, 0);
  scripted.frames(2);
  drag(scripted, {300, 200}, {340, 230});
  scripted.click(kToolsButton[0], kToolsButton[1]);
  // ... and the same pointer path, a click instead of the drag.
  plain.gui.mouse().update(400, 150, 0, 0);
  plain.frames(2);
  plain.gui.mouse().update(300, 200, 0, 0);
  plain.frames(1);
  plain.gui.mouse().update(300, 200, ui::kButtonLeft, 0);
  plain.frames(1);
  for (int i = 1; i <= 4; ++i) {
    plain.gui.mouse().update(300 + 40 * i / 4, 200 + 30 * i / 4, ui::kButtonLeft, 0);
    plain.frames(1);
  }
  plain.gui.mouse().update(340, 230, 0, 0);
  plain.frames(1);
  plain.click(kToolsButton[0], kToolsButton[1]);
  CHECK(chart.view().zoom() == zoom);
  CHECK(chart.view().center_x() == cx);
  CHECK(chart.panel() == ChartPanel::kMap);
  CHECK(chart.click_mode() == ChartClickMode::kSelectTarget);
  CHECK(screen_bytes(scripted) == screen_bytes(plain));
  CHECK(sim_bytes(scripted) == sim_bytes(plain));
  const std::vector<std::uint8_t> save =
      capture_sim_state(scripted.mission(), {}, {.screen = scripted.screen.get()}).serialize();
  CHECK(save == capture_sim_state(plain.mission(), {}, {.screen = plain.screen.get()}).serialize());
  CHECK_FALSE(has_tag(save, "CHRT"));
}

TEST_CASE("The original chart keeps its panel and scale", "[charttools]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  open_chart(rig, false);
  // No TOOLS button: the panel's grey where it would be.
  std::vector<std::uint8_t> spot;
  for (int y = kPanelY + 0x2b; y < kPanelY + 0x2b + kChartToolButtonH; ++y) {
    for (int x = kPanelX + 0x79; x < kPanelX + 0x79 + kChartToolsButtonW; ++x) {
      spot.push_back(rig.screen_image.at(x, y));
    }
  }
  CHECK(std::ranges::all_of(spot, [&spot](std::uint8_t c) { return c == spot.front(); }));
  // CHART.SHP's scale bar, unchanged.
  const Shp sprites = Shp::load(install, rig.paths.path(GameDir::kArt, "CHART.SHP"));
  const SpriteFrame& bar = sprites.frame(0x12);
  Image8 expected = rig.screen_image;
  draw_sprite_clipped(Viewport::full(expected), bar, kPanelX + 0x12, kPanelY + 0x6f);
  CHECK(screen_bytes(rig) == std::vector<std::uint8_t>(expected.pixels().begin(), expected.pixels().end()));
}

}  // namespace sh
