// The screens of DOSBox captures of SE.EXE (not in the repository, named
// below by their capture time) rebuilt in the same state: the start with
// the player's TOOLBOX, the menu bar, ENVIRONMENT, OPTIONS and SELECT SHIP
// TYPE, started by the game and from DOS. Checks what the captures show
// beyond pixels: the menu closes the panel and clears the status bar,
// nothing is drawn in the colours that stay black from DOS, and
// ENVIRONMENT's rising and setting times are the capture's. The second
// session (213237-214221) rebuilds the ship and aircraft panels, the range
// circles and TOOLBOX indicators (seen, in radar range), SELECT A CITY,
// PLOT COURSE, ORDERS and the picture dialogs, TORPEDOES and EQUIPMENT.
// The third (232213-232346) the enemy sub, airstrike and photo recon
// panels.
// SH_SCREENSHOT_DIR receives `dosbox_<capture time>_<screen>.bmp`.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor/class_sheets.h"
#include "editor/environment.h"
#include "editor_rig.h"
#include "game_data.h"

namespace sh::editor {
namespace {

using test::EditorRig;
using test::kEditX;
using test::kFilesX;

// Where the captures' pointer rests: over the TOOLBOX (status bar cleared)
// and over the map (status bar showing the pointer's position).
constexpr std::array<int, 2> kOverToolbox = {285, 195};
constexpr std::array<int, 2> kOverMap = {45, 190};
// The left of the TOOLBOX (screen, panel at (0, 121)), clear of the
// dialogs.
constexpr int kToolboxY0 = 121;
constexpr int kToolboxX1 = 100;
constexpr int kToolboxY1 = 121 + 0x152 - 1;
// The GUI face colour (`gui_init`).
constexpr std::uint8_t kGuiFace = 6;
constexpr int kStatusY = 459;
// ADD SHIP (TOOLBOX coordinates).
constexpr std::array<int, 2> kAddShip = {82 + 20, 71 + 10};
constexpr int kEditEnvironment = 3;
// Where the player sub was dragged before ENVIRONMENT, in pixels from its
// start (its icon in the later captures 100525 / 100614, the view scrolled
// by 100 / 150 lines): about 134 08'E 32 15'N, so the sky is computed at
// 134 E 32 N, a degree east of the start's 133 05'E.
constexpr std::array<int, 2> kPlayerMoved = {128, 96};
// SAVE CHANGES' discard button (dialog at (203, 142)).
constexpr std::array<int, 2> kSaveChangesDiscard = {203 + 67 + 50, 142 + 92 + 12};
constexpr int kEditOptions = 6;

void move(EditorRig& rig, std::array<int, 2> at) {
  rig.step([at](ui::Gui& gui) { gui.mouse().update(at[0], at[1], 0, 0); });
}

// The view centred on the player again (the rig's pointer starts in the
// corner and scrolls the map until it moves), the pointer at `at`.
void settle(EditorRig& rig, std::array<int, 2> at) {
  move(rig, {at[0] + 1, at[1]});
  rig.then([&rig] { rig.editor.chart().center_on_player(); });
  move(rig, at);
}

// Checks that the TOOLBOX is not on the screen and the status bar is black.
void check_panel_closed(EditorRig& rig) {
  rig.then([&rig] {
    const Image8& screen = rig.editor.ui().screen();
    int face = 0;
    for (int y = kToolboxY0; y <= kToolboxY1; ++y) {
      for (int x = 0; x <= kToolboxX1; ++x) {
        face += screen.at(x, y) == kGuiFace ? 1 : 0;
      }
    }
    CHECK(face == 0);
    int lit = 0;
    for (int y = kStatusY; y < screen.height(); ++y) {
      for (int x = 0; x < screen.width(); ++x) {
        lit += screen.at(x, y) != 0 ? 1 : 0;
      }
    }
    CHECK(lit == 0);
  });
}

// Checks that the screen has no pixel in the colours only M8.PCX sets
// (86-127 and 255), black when SE is started from DOS.
void check_no_m8_colours(EditorRig& rig) {
  rig.then([&rig] {
    const Image8& screen = rig.editor.ui().screen();
    int m8 = 0;
    for (int y = 0; y < screen.height(); ++y) {
      for (int x = 0; x < screen.width(); ++x) {
        const std::uint8_t c = screen.at(x, y);
        m8 += (c >= 86 && c <= 127) || c == 255 ? 1 : 0;
      }
    }
    CHECK(m8 == 0);
  });
}

// The second session's controls: TOOLBOX (dialog coordinates) and OPTIONS
// (screen; the dialog at (157, 52)).
constexpr std::array<int, 2> kPlayerClass = {108 + 50, 150 + 10};
constexpr std::array<int, 2> kPlayerName = {11 + 100, 281 + 10};
constexpr std::array<int, 2> kAircraftType = {108 + 50, 150 + 10};
constexpr std::array<int, 2> kShipOrders = {108 + 50, 173 + 10};
constexpr std::array<int, 2> kAirstrike = {172 + 20, 71 + 10};
constexpr std::array<int, 2> kPhotoRecon = {217 + 20, 71 + 10};
constexpr std::array<int, 2> kOptionsLimitSubs = {220, 154};
constexpr std::array<int, 2> kOptionsLimitAircraft = {220, 177};
constexpr std::array<int, 2> kOptionsOk = {407, 384};
constexpr int kEditTorpedoes = 4;
// SELECT SHIP TYPE (screen): the first row and SELECT; SELECT A CITY's
// SELECT (dialog).
constexpr std::array<int, 2> kShipRow0 = {137 + 15 + 100, 10 + 292 + 9};
constexpr std::array<int, 2> kShipSelect = {137 + 252 + 50, 10 + 400 + 12};
constexpr std::array<int, 2> kCitySelect = {110 + 40, 45 + 10};
// The third session's view: the start's scrolled down (the map moved down).
constexpr int kMissionScroll = 150;
constexpr int kEditEquipment = 5;

// Drags with the left button from `from` to `to`.
void drag(EditorRig& rig, std::array<int, 2> from, std::array<int, 2> to) {
  rig.step([from](ui::Gui& gui) { gui.mouse().update(from[0], from[1], 0, 0); });
  rig.step([from](ui::Gui& gui) { gui.mouse().update(from[0], from[1], ui::kButtonLeft, 0); });
  rig.step([to](ui::Gui& gui) { gui.mouse().update(to[0], to[1], ui::kButtonLeft, 0); });
  rig.step([to](ui::Gui& gui) { gui.mouse().update(to[0], to[1], 0, 0); });
}

// Zooms and scrolls the view so that the world point `p` shows at screen
// pixel `at` (the captures' views, found from their objects and grid
// lines), the map and the panel redrawn.
void show_at(ChartScreen& chart, std::int32_t zoom, WorldPoint p, std::array<int, 2> at) {
  ChartView& v = chart.view();
  v.set_zoom(zoom << 16);
  const std::array<std::int32_t, 2> c = v.world_to_chart(p.x, p.y);
  v.set_center(c[0], c[1]);
  const ChartPoint s = v.world_to_screen(p.x, p.y);
  v.set_center(v.center_x() + (s.x - at[0]) * v.step(), v.center_y() + (s.y - at[1]) * v.step());
  chart.reopen_panel();
}

// Moves `object` to screen pixel `at` (as the captures' drags did).
void put(EditorRig& rig, EditorObject& object, std::array<int, 2> at) {
  Scenario& s = rig.editor.scenario();
  CHECK(s.move_object(object, rig.editor.chart().screen_to_world(at[0], at[1])));
  s.update_all_visibility();
  rig.editor.chart().request_overlay_redraw();
}

// The two TOOLBOX indicators, seen (eye) and in radar range, as drawn:
// whether their boxes hold anything but the panel face.
std::array<bool, 2> indicators_lit(EditorRig& rig) {
  const ui::Dialog* d = rig.editor.chart().panel_dialog();
  REQUIRE(d != nullptr);
  const Image8& screen = rig.editor.ui().screen();
  std::array<bool, 2> lit{};
  for (std::size_t i = 0; i < 2; ++i) {
    const int x0 = d->x() + (i == 0 ? 0xd5 : 0xfc);
    for (int y = d->y() + 0x133; y <= d->y() + 0x144; ++y) {
      for (int x = x0; x <= x0 + 0x20; ++x) {
        lit[i] = lit[i] || screen.at(x, y) != kGuiFace;
      }
    }
  }
  return lit;
}

}  // namespace

TEST_CASE("The DOSBox captures started by the game", "[editor][screens]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorRig rig(install, true);
  rig.host.frame_limit = 100'000;
  rig.then([&] {
    ScnHeader& h = rig.editor.scenario().header;
    h.weather = Weather::kMostlyCloudy;
    h.sea_state = SeaState::kHeavy;
    h.gradient = 1;
  });
  settle(rig, kOverToolbox);
  rig.screenshot("dosbox_100334_toolbox");

  rig.then([&rig] {
    Scenario& s = rig.editor.scenario();
    const std::optional<std::array<std::int32_t, 2>> at =
        rig.editor.chart().world_to_screen(s.player().position(s.object_env()));
    REQUIRE(at.has_value());
    CHECK(s.move_object(s.player(), rig.editor.chart().screen_to_world((*at)[0] + kPlayerMoved[0],
                                                                       (*at)[1] + kPlayerMoved[1])));
  });
  rig.press(320, 200, ui::kButtonRight);
  check_panel_closed(rig);
  rig.key(ui::kKeyEsc);
  rig.menu(kEditX, kEditEnvironment);
  check_panel_closed(rig);
  rig.then([&rig] {
    const Scenario& s = rig.editor.scenario();
    const WorldPoint p = s.player().position(s.object_env());
    const EnvironmentSky sky =
        compute_environment_sky(EnvironmentForm::from_header(s.header), s.data(), s.world().world(), p.x, p.y);
    const SeExe& se = s.data().se;
    CHECK(rise_set_text(se, RiseSet::kSunrise, sky) == "Sunrise - 06:43");
    CHECK(rise_set_text(se, RiseSet::kSunset, sky) == "Sunset   - 16:52");
    CHECK(rise_set_text(se, RiseSet::kMoonrise, sky) == "Moonrise - 20:31");
    CHECK(rise_set_text(se, RiseSet::kMoonset, sky) == "Moonset   - 09:33");
  });
  rig.screenshot("dosbox_100452_environment");
  rig.key(ui::kKeyEsc);
  rig.menu(kEditX, kEditOptions);
  rig.screenshot("dosbox_100525_options");
  rig.key(ui::kKeyEsc);

  rig.click_panel(kAddShip[0], kAddShip[1]);
  rig.screenshot("dosbox_100351_select_ship");
  rig.key(ui::kKeyEsc);
  rig.menu(kFilesX, 6);  // Exit
  rig.click(kSaveChangesDiscard[0], kSaveChangesDiscard[1]);  // the moved player
  CHECK(rig.editor.run() == EditorExit::kExit);
}

TEST_CASE("The DOSBox captures started from DOS", "[editor][screens]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorRig rig(install, false);
  rig.host.frame_limit = 100'000;
  // The capture's TOOLBOX was dragged by its top to (72, 96).
  rig.step([](ui::Gui& gui) { gui.mouse().update(150, 125, 0, 0); });
  rig.step([](ui::Gui& gui) { gui.mouse().update(150, 125, ui::kButtonLeft, 0); });
  rig.step([](ui::Gui& gui) { gui.mouse().update(222, 100, ui::kButtonLeft, 0); });
  rig.step([](ui::Gui& gui) { gui.mouse().update(222, 100, 0, 0); });
  settle(rig, kOverMap);
  rig.screenshot("dosbox_101707_toolbox");
  check_no_m8_colours(rig);
  rig.press(320, 200, ui::kButtonRight);
  rig.screenshot("dosbox_101712_menu_bar");
  check_panel_closed(rig);
  check_no_m8_colours(rig);
  rig.key(ui::kKeyEsc);
  rig.menu(kEditX, kEditEnvironment);
  check_no_m8_colours(rig);
  rig.key(ui::kKeyEsc);
  rig.menu(kFilesX, 5);  // Exit
  CHECK(rig.editor.run() == EditorExit::kExit);
}

// The second session (captures 213237 to 214221): the ship, aircraft and
// city panels, PLOT COURSE, the picture dialogs, TORPEDOES and EQUIPMENT,
// the range circles and the TOOLBOX indicators. Options: subs and aircraft
// limited by date (the lists' lengths), the date moved from 7 Dec 1941 to
// late 1944 before 213614 (the aircraft list without the D3A and B5N, the
// sub list without the TENCH).
TEST_CASE("The DOSBox captures of the object panels and dialogs", "[editor][screens]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorRig rig(install, true);
  rig.host.frame_limit = 100'000;
  const auto chart = [&rig]() -> ChartScreen& { return rig.editor.chart(); };
  const auto sc = [&rig]() -> Scenario& { return rig.editor.scenario(); };
  settle(rig, kOverMap);
  rig.menu(kEditX, kEditOptions);
  rig.click(kOptionsLimitSubs[0], kOptionsLimitSubs[1]);
  rig.click(kOptionsLimitAircraft[0], kOptionsLimitAircraft[1]);
  rig.click(kOptionsOk[0], kOptionsOk[1]);
  // SELECT SUB TYPE in 1941: six classes, the T CLASS on the last row.
  rig.click_panel(kPlayerClass[0], kPlayerClass[1]);
  rig.screenshot("dosbox_213237_select_sub_type");
  rig.key(ui::kKeyEsc);
  drag(rig, {150, 125}, {171, 97});  // the TOOLBOX to (21, 93)

  // A NAGATO and a Follow Path AICHI D3A, the view 50 pixels right of the
  // start.
  WorldPoint start{};
  std::array<int, 2> start_at{};
  Vessel* nagato = nullptr;
  Aircraft* val = nullptr;
  rig.then([&] {
    start = sc().player().position(sc().object_env());
    const auto at = chart().world_to_screen(start);
    REQUIRE(at.has_value());
    start_at = {(*at)[0], (*at)[1]};
    show_at(chart(), 1, start, {start_at[0] + 50, start_at[1]});
    const auto world = [&](int x, int y) { return chart().screen_to_world(x, y); };
    nagato = &sc().add_vessel(*sc().data().find_ship("NAGATO"), world(447, 304), Vessel::Kind::kShip);
    const std::vector<AircraftType>& types = sc().data().tables.aircraft;
    const auto d3a = std::ranges::find(types, "AICHI D3A", &AircraftType::long_name);
    REQUIRE(d3a != types.end());
    val = &sc().add_aircraft(static_cast<std::size_t>(d3a - types.begin()), AircraftMission::kFollowPath);
    for (const auto& [x, y] : {std::pair{458, 266}, {543, 382}, {342, 338}}) {
      Scenario::add_aircraft_point(*val, world(x, y));
    }
    sc().update_all_visibility();
    chart().select(*nagato);
    chart().request_overlay_redraw();
  });
  move(rig, {418, 424});
  rig.screenshot("dosbox_213320_toolbox_ship");
  rig.then([&] { CHECK(indicators_lit(rig) == std::array{false, false}); });

  // The aircraft selected (its path green) and hovered (the popup).
  rig.then([&] { show_at(chart(), 1, start, {start_at[0], start_at[1] - 100}); });
  rig.click_at([&] {
    const auto at = chart().world_to_screen(val->position(sc().object_env()));
    REQUIRE(at.has_value());
    return std::array<int, 2>{(*at)[0], (*at)[1]};
  });
  move(rig, {411, 165});
  rig.screenshot("dosbox_213329_toolbox_aircraft");

  // The player moved next to the ship: seen, the eye lit.
  rig.then([&] {
    show_at(chart(), 1, start, {start_at[0] + 50, start_at[1] - 100});
    put(rig, *val, {475, 222});
    put(rig, sc().player(), {447, 186});
    chart().select(*nagato);
    chart().request_overlay_redraw();
  });
  move(rig, {426, 250});
  rig.screenshot("dosbox_213415_toolbox_ship_seen");
  rig.then([&] { CHECK(indicators_lit(rig) == std::array{true, false}); });

  // Zoomed to 4x, the ship inside the circle, then just outside it.
  WorldPoint anchor{};
  rig.then([&] {
    anchor = val->position(sc().object_env());
    show_at(chart(), 4, anchor, {526, 340});
    put(rig, *val, {525, 343});
    put(rig, sc().player(), {405, 234});
    put(rig, *nagato, {400, 355});
    chart().reopen_panel();
  });
  move(rig, {395, 360});
  rig.screenshot("dosbox_213512_toolbox_ship_seen");
  rig.then([&] { CHECK(indicators_lit(rig) == std::array{true, false}); });
  rig.then([&] {
    show_at(chart(), 4, anchor, {526, 290});
    put(rig, *nagato, {400, 318});
    chart().reopen_panel();
  });
  move(rig, {404, 318});
  rig.screenshot("dosbox_213519_toolbox_ship_unseen");
  rig.then([&] { CHECK(indicators_lit(rig) == std::array{false, false}); });

  // SELECT A CITY for an airstrike, the first city preselected.
  rig.click_panel(kAirstrike[0], kAirstrike[1]);
  move(rig, {172, 322});
  rig.screenshot("dosbox_213549_select_city_bomb");
  rig.then([&] { CHECK(chart().city_selection() == 0); });
  rig.key(ui::kKeyEsc);

  // The picture dialogs in late 1944.
  rig.then([&] {
    sc().header.month = 9;
    sc().header.year = 1944 - 1930;
    chart().select(sc().player());
  });
  rig.click_panel(kPlayerName[0], kPlayerName[1]);
  rig.screenshot("dosbox_213614_select_sub_name");
  rig.key(ui::kKeyEsc);
  rig.click_panel(kPlayerClass[0], kPlayerClass[1]);
  rig.screenshot("dosbox_213623_select_sub_type");
  rig.key(ui::kKeyEsc);
  rig.then([&] { chart().select(*val); });
  rig.click_panel(kAircraftType[0], kAircraftType[1]);
  rig.screenshot("dosbox_213700_select_aircraft_type");
  rig.key(ui::kKeyEsc);
  rig.menu(kEditX, kEditTorpedoes);
  rig.screenshot("dosbox_213722_torpedoes");
  rig.key(ui::kKeyEsc);
  rig.menu(kEditX, kEditEquipment);
  rig.screenshot("dosbox_213735_equipment");
  rig.key(ui::kKeyEsc);

  // An SJ-1 radar: the second circle, black, and the radar indicator.
  rig.then([&] {
    sc().remove(*val);
    sc().header.equipment |= static_cast<std::uint32_t>(Equipment::kSj1Radar);
    show_at(chart(), 4, anchor, {526, 290});
    put(rig, sc().player(), {406, 241});
    put(rig, *nagato, {425, 390});
    chart().select(*nagato);
  });
  move(rig, {500, 390});
  rig.screenshot("dosbox_213807_toolbox_ship_radar");
  rig.then([&] { CHECK(indicators_lit(rig) == std::array{false, true}); });

  // A merchant, and SELECT A CITY for a photo recon.
  Vessel* merchant = nullptr;
  rig.then([&] {
    show_at(chart(), 4, anchor, {526, 240});
    put(rig, sc().player(), {406, 191});
    put(rig, *nagato, {425, 340});
    // The capture's second ship is unknown (a mark at 4x; ORDERS offers
    // convoy leader: a merchant or a warship) and seen as far as NAGATO
    // (its sighting circle): the first such class.
    const std::vector<ShipClass>& ships = sc().data().tables.ships;
    for (std::size_t c = 0; c < ships.size() && merchant == nullptr; ++c) {
      const ShipRole role = ships[c].role;
      if ((role != ShipRole::kMerchant && role != ShipRole::kWarship) || ships[c].ship_type == ShipType::kSubmarine ||
          c == *sc().data().find_ship("NAGATO")) {
        continue;
      }
      Vessel& v = sc().add_vessel(c, chart().screen_to_world(459, 341), Vessel::Kind::kShip);
      sc().update_all_visibility();
      if (v.visual_range() == nagato->visual_range()) {
        merchant = &v;
      } else {
        sc().remove(v);
      }
    }
    REQUIRE(merchant != nullptr);
    sc().update_all_visibility();
    chart().request_overlay_redraw();
  });
  rig.click_panel(kPhotoRecon[0], kPhotoRecon[1]);
  move(rig, {218, 300});
  rig.screenshot("dosbox_213916_select_city_photo");
  rig.key(ui::kKeyEsc);

  // The merchant made convoy leader: PLOT COURSE at its third point.
  rig.then([&] {
    show_at(chart(), 4, anchor, {526, 190});
    chart().select(*merchant);
  });
  rig.click_panel(kShipOrders[0], kShipOrders[1]);
  rig.key(ui::kKeyDown);
  rig.key(ui::kKeyEnter);
  rig.click(221, 380);
  rig.click(172, 311);
  move(rig, {122, 297});
  rig.screenshot("dosbox_214157_plot_course");
  rig.then([&] { CHECK(chart().prompt_text() == "Select waypoint #4"); });
  rig.then([&] { chart().finish_mode(); });

  // ORDERS again for the leader, over the greyed course.
  rig.then([&] {
    show_at(chart(), 4, anchor, {576, 190});
    chart().select(*merchant);
  });
  rig.click_panel(kShipOrders[0], kShipOrders[1]);
  move(rig, {238, 258});
  rig.screenshot("dosbox_214221_orders");
  rig.key(ui::kKeyEsc);
  rig.menu(kFilesX, 6);  // Exit
  rig.click(kSaveChangesDiscard[0], kSaveChangesDiscard[1]);
  CHECK(rig.editor.run() == EditorExit::kExit);
}

// The third session (232213-232346): ADD SHIP of a B TYPE submarine, the
// enemy sub's TOOLBOX, then an airstrike on Kochi and a photo recon of
// Tokushima with their panels, the view scrolled 150 lines down.
TEST_CASE("The DOSBox captures of the enemy sub and mission panels", "[editor][screens]") {
  SH_REQUIRE_GAME_DATA(install);
  EditorRig rig(install, true);
  rig.host.frame_limit = 100'000;
  const auto chart = [&rig]() -> ChartScreen& { return rig.editor.chart(); };
  const auto sc = [&rig]() -> Scenario& { return rig.editor.scenario(); };
  settle(rig, kOverMap);
  // SELECT SHIP TYPE scrolled by the keys to the capture's rows: the
  // KAIDAI TYPE on top, the B TYPE third.
  rig.click_panel(kAddShip[0], kAddShip[1]);
  rig.click(kShipRow0[0], kShipRow0[1]);
  rig.then([&] {
    const ClassList list = ship_type_list(sc().data(), rig.editor.options(), rig.editor.ui().resources().small,
                                          sc().header.month, sc().header.year, -1);
    const auto b = std::ranges::find_if(list.entries, [&](std::size_t c) {
      return sc().data().tables.ships[c].name == "B TYPE";
    });
    REQUIRE(b != list.entries.end());
    const int row = static_cast<int>(b - list.entries.begin());
    // Down to the row three below, then Up three: the list scrolls only
    // on the way down.
    for (int i = 0; i < 3; ++i) {
      rig.host.script.push_front([](ui::Gui& gui) { gui.mouse().post_key(ui::kKeyUp, 0); });
    }
    for (int i = 0; i < row + 3; ++i) {
      rig.host.script.push_front([](ui::Gui& gui) { gui.mouse().post_key(ui::kKeyDown, 0); });
    }
  });
  move(rig, {432, 348});
  rig.screenshot("dosbox_232213_select_ship_type");
  rig.click(kShipSelect[0], kShipSelect[1]);
  rig.click(418, 336);
  move(rig, {220, 415});
  rig.screenshot("dosbox_232256_toolbox_sub");
  rig.then([&] { CHECK(sc().selected()->type() == ObjectType::kSubmarine); });

  // The airstrike: SELECT A CITY's Kochi row (the ninth), SELECT.
  rig.then([&] {
    const WorldPoint p = sc().player().position(sc().object_env());
    const auto at = chart().world_to_screen(p);
    REQUIRE(at.has_value());
    show_at(chart(), 1, p, {(*at)[0], (*at)[1] + kMissionScroll});
  });
  rig.click_panel(kAirstrike[0], kAirstrike[1]);
  rig.click_panel(10 + 100, 135 + 8 * 16 + 8);
  rig.click_panel(kCitySelect[0], kCitySelect[1]);
  move(rig, {440, 385});
  rig.screenshot("dosbox_232318_toolbox_airstrike");
  // The photo recon: Tokushima picked on the map.
  rig.click_panel(kPhotoRecon[0], kPhotoRecon[1]);
  rig.click(494, 219);
  rig.click_panel(kCitySelect[0], kCitySelect[1]);
  move(rig, {275, 315});
  rig.screenshot("dosbox_232346_toolbox_photo_recon");
  rig.then([&] { CHECK(sc().selected()->type() == ObjectType::kPhotoRecon); });
  rig.menu(kFilesX, 6);  // Exit
  rig.click(kSaveChangesDiscard[0], kSaveChangesDiscard[1]);
  CHECK(rig.editor.run() == EditorExit::kExit);
}

}  // namespace sh::editor
