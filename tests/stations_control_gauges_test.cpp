// Tests of the gauges station and the shared navigation panel: the dial
// and instrument formulas, the panel controls ordering the boat, the page
// switch, and screenshots.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>

#include "game/stations/gauges.h"
#include "game/stations/nav_panel.h"
#include "game_data.h"
#include "gfx/sprite.h"
#include "stations_control_fixture.h"

namespace sh {

using test::ControlRig;

TEST_CASE("Gauge needle angles follow the original formulas", "[stations]") {
  // 296 deg + charge x (127 + 1 deg) x 100 / 600 / 100.
  CHECK(battery_needle_angle(0) == 2960);
  CHECK(battery_needle_angle(300) == 3593);
  CHECK(battery_needle_angle(600) == 626);  // 4226 folded
  NeedleDial depth;
  depth.arc_start = 190;
  depth.arc_end = 170;
  depth.max = 600;
  // Span 341 deg: 568 hundredths of a tenth per foot.
  CHECK(needle_angle(depth, 0) == 1900);
  CHECK(needle_angle(depth, 300) == 4);
  CHECK(needle_angle(depth, 600) == 1708);
  CHECK(needle_value_at(depth, 2468) == 100);
  // Beyond the arc: 0 on the left, the maximum on the right.
  CHECK(needle_value_at(depth, 1800) == 0);
  CHECK(needle_value_at(depth, 1750) == 600);
  NeedleDial speed;
  speed.arc_start = 236;
  speed.arc_end = 159;
  speed.max = 250;
  CHECK(needle_angle(speed, 100) == 3496);
  CHECK(needle_angle(speed, -20) == 2133);
}

TEST_CASE("Gauge instrument values", "[stations]") {
  CHECK(fathometer_feet(-1600) == 100);
  CHECK(fathometer_feet(-17) == 1);
  CHECK(fathometer_feet(-16 * 800) == 700);
  CHECK(fathometer_feet(40) == 0);
  CHECK(fuel_needle(117000, 117000) == 512);
  CHECK(fuel_needle(58500, 117000) == 256);
  CHECK(air_needle(25) == 128);
  SubClassParams sub;
  sub.ballast_neutral = 2000;
  sub.ballast_max = 2010;
  CHECK(buoyancy_needle(0, sub) == 512);
  CHECK(buoyancy_needle(4000, sub) == 256);
  CHECK(buoyancy_needle(100000, sub) == 0);
  CHECK(oxygen_bar_offset(1000, 1000) == 0);
  CHECK(oxygen_bar_offset(500, 1000) == 45);
  CHECK(telegraph_frame(TelegraphDial{0, 0, {}, 7, 6}, 10) == 8);
}

TEST_CASE("The helm strip and the pitch bubble read SH.EXE's tables", "[stations]") {
  SH_REQUIRE_GAME_EXE();
  CHECK(rudder_step_at(0) == std::nullopt);
  CHECK(rudder_step_at(1) == -7);
  CHECK(rudder_step_at(80) == 0);
  CHECK(rudder_step_at(160) == 7);
  CHECK(rudder_step_at(161) == std::nullopt);
  const std::optional<BubblePlace> level = pitch_bubble(0);
  REQUIRE(level);
  CHECK(level->dx == 0);
  const std::optional<BubblePlace> down = pitch_bubble(3550);  // -5 deg
  REQUIRE(down);
  CHECK(down->dx == -23);
  CHECK(down->dy == 1);
  CHECK(down->angle == 150);
  CHECK(!pitch_bubble(1000));
}

TEST_CASE("The bathythermograph trace cools through the layer", "[stations]") {
  // 65.00 deg at the surface: x = 64 x 12 / 10 + 31.
  const std::vector<TracePoint> shallow = bathythermograph_trace(10, 10000);
  REQUIRE(!shallow.empty());
  CHECK(shallow.front() == TracePoint{107, 12});
  CHECK(shallow.back().y == 12 + 10 / 5);
  CHECK(bathythermograph_trace(-50, 10000).empty());
  // Through a layer at 100 ft: 65.00 - 0.21 - 1.00 - 1.00 - 42.00 - 1.00 -
  // 1.00 - 0.36 = 18.43 deg at 200 ft.
  const std::vector<TracePoint> deep = bathythermograph_trace(200, 100);
  CHECK(deep.back().x == 18 * 12 / 10 + 31);
  CHECK(deep.back().y == 12 + 200 / 5);
}

TEST_CASE("The gauges station and its panel order the boat", "[stations]") {
  SH_REQUIRE_GAME_DATA(install);
  MissionSetup setup;
  setup.options = 0x200000;  // the bathythermograph page
  ControlRig rig(install, "SCEN/HIST01.SCN", setup);
  rig.screen->install(StationId::kGauges,
                      std::make_unique<GaugesStation>(rig.screen->env(), rig.screen->dive_recorder()));
  rig.frames(1);
  rig.settle();
  rig.key(0x4100);  // F7
  auto* gauges = dynamic_cast<GaugesStation*>(rig.screen->loop().open_station());
  REQUIRE(gauges != nullptr);
  rig.settle();
  rig.frames(2);
  rig.save("gauges_engines");
  PlayerSub& player = rig.mission().player();

  // The depth dial: a click straight above the pivot orders ~300 ft.
  const NavTemplate& t = gauges->panel()->layout();
  rig.click(t.depth.cx, t.depth.cy - 40);
  rig.frames(2);
  CHECK(player.dive().ordered_depth_ft >= 280);
  CHECK(player.dive().ordered_depth_ft <= 320);

  // The helm strip: the right end is 35 deg right.
  rig.click(t.rudder.rect.x0 + 155, (t.rudder.rect.y0 + t.rudder.rect.y1) / 2);
  CHECK(player.helm() == 7);
  CHECK(!player.motion().steering);

  // The compass: a click east of the card orders course 090 and course mode.
  rig.click(t.compass.cx + 40, t.compass.cy - 1);
  CHECK(player.motion().steering);
  CHECK(player.motion().ordered_heading.tenths() >= 850);
  CHECK(player.motion().ordered_heading.tenths() <= 950);

  // The telegraph: a pixel of the click map coloured for ahead flank (5).
  const Shp sprites = Shp::load(install, "ART/GAUGES.SHP");
  const SpriteFrame& map = sprites.frame(static_cast<std::size_t>(t.telegraph.mask_frame));
  const DecodedFrame decoded = map.decode();
  bool clicked = false;
  for (int y = 0; y < decoded.image.height() && !clicked; ++y) {
    for (int x = 0; x < decoded.image.width() && !clicked; ++x) {
      if (decoded.opaque.at(x, y) != 0 && decoded.image.at(x, y) == 6) {
        rig.click(t.telegraph.rect.x0 + map.x0 + x, t.telegraph.rect.y0 + map.y0 + y);
        clicked = true;
      }
    }
  }
  REQUIRE(clicked);
  CHECK(player.telegraph() == 5);
  rig.frames(2);
  rig.save("gauges_ordered");

  // The bar's second button switches to the bathythermograph.
  rig.click(0x280 - 0x48 - 0x49 + 30, 0x1cb + 8);
  rig.frames(2);
  CHECK(rig.screen->ui_state().gauges_page == 1);
  rig.save("gauges_bathythermograph");
}

TEST_CASE("The nav panel opens on the bridge and chart templates", "[stations]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  StationEnv& env = rig.screen->env();
  // The bridge's lower part, as the bridge station draws it.
  const Shp nav = Shp::load(install, "ART/BRDGNAV.SHP");
  draw_sprite_clipped(Viewport::full(env.screen()), nav.frame(0), 0, 200);
  std::unique_ptr<NavPanel> panel = make_nav_panel(env, NavPanelSite::kBridge);
  panel->open();
  panel->process();
  panel->update(false);
  panel->process();
  CHECK(panel->layout().sprites == "BRDGNAV.SHP");
  CHECK(!panel->layout().pitch);
  rig.save("navpanel_bridge");
  panel->close();

  const NavTemplate chart = nav_template(env.exe, NavPanelSite::kChart);
  CHECK(!chart.test_depth_at);
  CHECK(chart.sprites == "CHART.SHP");
  const NavTemplate scope = nav_template(env.exe, NavPanelSite::kScope);
  CHECK(scope.pitch);
}

TEST_CASE("The nav panel dials redraw only their needles and counters", "[stations][needles]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  StationEnv& env = rig.screen->env();
  const Shp nav = Shp::load(install, "ART/BRDGNAV.SHP");
  Image8 picture(kScreenWidth, kScreenHeight, 0);
  draw_sprite_clipped(Viewport::full(picture), nav.frame(0), 0, 200);
  const NavTemplate t = nav_template(env.exe, NavPanelSite::kBridge);
  // gauges_panel_open clears the templates' cap frame (0 in SH.EXE).
  CHECK(t.depth.cap_frame == -1);
  CHECK(t.speed.cap_frame == -1);
  const GlyphColourTable& colours = t.digit_colours;
  CHECK(colours[0xe5] == 0xd5);
  CHECK(colours[0xc0] == 0xa0);
  CHECK(colours[0xff] == 0xff);
  CHECK(nav_digit_colours(NavPanelSite::kScope)[0xc0] == 0xdf);
  CHECK(nav_digit_colours(NavPanelSite::kChart)[0xff] == 0);

  const NeedleDial& dial = t.depth;
  const auto render = [&](Image8& out, std::int32_t value, std::optional<std::int32_t> ordered) {
    draw_needle_dial(Viewport::full(out), picture, dial, nav, env.assets.digits, colours, value, ordered, value);
  };
  // The needles reach this far from the pivot.
  int reach = 0;
  for (const std::int32_t f : {dial.needle_frame, dial.marker_frame}) {
    const SpriteFrame& frame = nav.frame(static_cast<std::size_t>(f));
    reach = std::max({reach, std::abs(frame.x0), std::abs(frame.x1), std::abs(frame.y0), std::abs(frame.y1)});
  }
  const int digits_w = 3 * 12;  // DIGITS.FNT cells are narrower than this
  const auto in_digits = [&](int x, int y) {
    return x >= dial.digits_x && x < dial.digits_x + digits_w && y >= dial.digits_y &&
           y < dial.digits_y + env.assets.digits.height();
  };

  // A dial drawn again with other values equals a fresh drawing of them.
  Image8 redrawn = picture;
  render(redrawn, 50, 300);
  render(redrawn, 420, std::nullopt);
  Image8 fresh = picture;
  render(fresh, 420, std::nullopt);
  int digit_pixels = 0;
  for (int y = 0; y < kScreenHeight; ++y) {
    for (int x = 0; x < kScreenWidth; ++x) {
      REQUIRE(redrawn.at(x, y) == fresh.at(x, y));
      if (fresh.at(x, y) == picture.at(x, y)) {
        continue;
      }
      // Only the needle's sweep and the counter change.
      const int dx = x - dial.cx;
      const int dy = y - dial.cy;
      const bool in_sweep = dx * dx + dy * dy <= (reach + 2) * (reach + 2);
      INFO("changed pixel at " << x << ", " << y);
      REQUIRE((in_sweep || in_digits(x, y)));
      if (in_digits(x, y)) {
        // The counter is drawn through the bridge's table: no raw glyph shade.
        const std::uint8_t c = fresh.at(x, y);
        CHECK(((c >= 0xa0 && c <= 0xa4) || (c >= 0xd0 && c <= 0xdd) || c == 0));
        digit_pixels += c >= 0xd0 && c <= 0xdd ? 1 : 0;
      }
    }
  }
  CHECK(digit_pixels > 0);

  // The panel shows its needles from its first frame.
  Image8& screen = env.screen();
  screen = picture;
  std::unique_ptr<NavPanel> panel = make_nav_panel(env, NavPanelSite::kBridge);
  panel->open();
  panel->process();
  Image8 expected = picture;
  const PlayerSub& player = rig.mission().player();
  draw_needle_dial(Viewport::full(expected), picture, t.speed, nav, env.assets.digits, colours,
                   player.motion().speed_tenths, std::nullopt, player.motion().log_miles);
  for (int y = t.speed.rect.y0; y <= t.speed.rect.y1; ++y) {
    for (int x = t.speed.rect.x0; x <= t.speed.rect.x1; ++x) {
      REQUIRE(screen.at(x, y) == expected.at(x, y));
    }
  }
  panel->close();
}

}  // namespace sh
