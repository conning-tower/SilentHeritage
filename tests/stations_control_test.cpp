// Tests of the control-room stations and the mission screen host: the
// command bar's hotkeys and their sim commands, the crew message log, the
// station switching, the conn screen.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "game/command_bar.h"
#include "game/extended_config.h"
#include "game/stations/cabin.h"
#include "game/stations/chart.h"
#include "game/stations/conn.h"
#include "game/stations/gauges.h"
#include "game/stations/status.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

// FNV-1a of the screen pixels.
std::uint32_t checksum(const Image8& image) {
  std::uint32_t h = 2166136261u;
  for (const std::uint8_t p : image.pixels()) {
    h = (h ^ p) * 16777619u;
  }
  return h;
}

}  // namespace

TEST_CASE("The 67 hotkeys in record order", "[stations]") {
  const auto& table = hotkey_table();
  CHECK(table[0].key == 0x3b00);
  CHECK(table[0].action == HotkeyAction::kStationConn);
  CHECK(table[14].key == 0x0b30);
  CHECK(table[14].action == HotkeyAction::kAllStop);
  CHECK(table[40].key == 0x7d00);
  CHECK(table[40].action == HotkeyAction::kFireTube6);
  CHECK(table[66].key == 0x0f09);
  CHECK(table[66].action == HotkeyAction::kReconPhoto);
}

TEST_CASE("The digit keys order the telegraph steps", "[stations]") {
  CHECK(telegraph_for_digit('0') == 0);
  CHECK(telegraph_for_digit('1') == 1);
  CHECK(telegraph_for_digit('5') == 5);
  CHECK(telegraph_for_digit('6') == 9);  // back 1/3
  CHECK(telegraph_for_digit('9') == 6);  // back emergency
}

TEST_CASE("The crew message log keeps 20 messages", "[stations]") {
  CrewMessageLog log;
  log.reset();  // pushes message 0
  log.push(3, -1, -1, {});
  log.push(4, -1, -1, {});
  CHECK(log.head() == 3);
  CHECK(log.tail() == 0);
  // Backspace shows the newest first, then goes back and wraps to the
  // newest before the oldest slot.
  CHECK(log.repeat());
  CHECK(log.shown() == 3);
  CHECK(log.repeat());
  CHECK(log.shown() == 2);
  CHECK(log.repeat());
  CHECK(log.shown() == 1);
  CHECK(log.repeat());
  CHECK(log.shown() == 3);
  for (int i = 0; i < 20; ++i) {
    log.push(10 + i, i, -1, {});
  }
  CHECK(log.head() == 3);
  CHECK(log.tail() == 4);
  CHECK(log.entry(3).id == 29);
  CHECK(log.entry(3).arg1 == 19);
}

TEST_CASE("Crew messages are formatted with speaker and arguments", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ExeImage exe = ExeImage::load(install);
  const CrewMessages messages(exe);
  CrewMessageLog::Entry up{3, -1, -1, {}};
  CHECK(format_crew_message(messages, up) == "OOD: Up Periscope, aye.");
  // #25 "Tube n will be loaded in m minutes".
  CrewMessageLog::Entry loading{0x19, 1, 3, {}};
  CHECK(format_crew_message(messages, loading) == "OOD: Tube 1 will be loaded in 3 minutes.");
  CrewMessageLog::Entry one{0x19, 4, 1, {}};
  CHECK(format_crew_message(messages, one) == "OOD: Tube 4 will be loaded in 1 minute.");
}

TEST_CASE("The mission starts at the conn and fades in", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  ConnStation* conn = dynamic_cast<ConnStation*>(rig.screen->loop().open_station());
  REQUIRE(conn != nullptr);
  CHECK(rig.screen->env().dac.fading());
  rig.settle();
  CHECK(rig.screen->dac() == rig.screen->env().palette);
  rig.save("conn");
}

TEST_CASE("Golden picture of the conn", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  // The conning tower above the command bar: CONN.PCX with the lowered
  // periscope and the closed cabin door.
  Image8 picture(kScreenWidth, 0x1cb);
  picture.blit(rig.screen->screen(), 0, 0);
  CHECK(checksum(picture) == 3635469441u);
}

TEST_CASE("The telegraph, helm and planes keys order the boat", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  PlayerSub& sub = rig.mission().player();
  rig.key(0x0534);  // '4': ahead full
  CHECK(sub.telegraph() == 4);
  rig.key(0x0837);  // '7': back two thirds
  CHECK(sub.telegraph() == 8);
  rig.key(0x0b30);  // '0': all stop
  CHECK(sub.telegraph() == 0);
  rig.key(0x4d00);  // Right: rudder one step right
  rig.key(0x4d00);
  CHECK(sub.helm() == 2);
  rig.key(0x4b00);  // Left
  CHECK(sub.helm() == 1);
  rig.key(0x4cf0);  // keypad 5: straight and level
  CHECK(sub.helm() == 0);
  CHECK(sub.dive().planes == 0);
  CHECK(sub.dive().ordered_depth_ft == -1);
  // The planes are checked in the frame of the key: the depth keeping
  // moves them in the next simulated frames.
  rig.key(0x5000, 1);  // Down: planes on dive
  CHECK(sub.dive().planes == -60);
  CHECK(rig.screen->bar().current_message() == "Planesman: Planes on dive, sir.");
  rig.frames(1);
  rig.key(0x4800, 1);  // Up: planes on rise
  CHECK(sub.dive().planes == 60);
  rig.frames(1);
  rig.key(0x1372, 1);  // 'r': radar depth
  CHECK(sub.dive().ordered_depth_ft == sub.sub().radar_depth_ft);
  CHECK(sub.dive().planes == -60);
  rig.frames(1);
  rig.key(0x1970);  // 'p': periscope depth
  CHECK(sub.dive().ordered_depth_ft == sub.sub().periscope_depth_ft);
  rig.key(0x2e63, 1);  // 'c': crash dive
  CHECK(sub.telegraph() == 5);
  CHECK(sub.dive().planes == -80);
  CHECK(sub.dive().ordered_depth_ft == 200);
}

TEST_CASE("The time compression keys", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  const GameClock& clock = rig.mission().sim().clock();
  rig.key(0x0d2b);  // '+'
  CHECK(clock.requested_compression() == 2);
  rig.key(0x4e2b);  // keypad '+' (any scan code)
  CHECK(clock.requested_compression() == 4);
  rig.key(0x0c2d);  // '-'
  CHECK(clock.requested_compression() == 2);
  rig.key(0x1c0d);  // Enter: back to 1
  CHECK(clock.requested_compression() == 1);
}

TEST_CASE("Alt-P raises the periscope and the conn shows it", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  PlayerSub& sub = rig.mission().player();
  REQUIRE(rig.screen->bar().can_raise_periscope());
  REQUIRE(!sub.periscope_raised());
  rig.key(0x1900);
  CHECK(sub.periscope_raised());
  CHECK(rig.screen->bar().current_message() == "OOD: Up Periscope, aye.");
  auto* conn = dynamic_cast<ConnStation*>(rig.screen->loop().open_station());
  REQUIRE(conn != nullptr);
  CHECK(conn->animation() == 1);
  // 11 px every 5 ticks from 10 to 370.
  rig.frames(40);
  CHECK(conn->animation() == 0);
  CHECK(conn->shaft() == kPeriscopeUp);
  rig.save("conn_periscope_up");
  // Clicking the lever lowers it.
  rig.click(300, 300);
  CHECK(!sub.periscope_raised());
  CHECK(conn->animation() == 2);
}

TEST_CASE("Backspace repeats the previous crew message", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  rig.key(0x4800);  // planes on rise
  rig.key(0x5000);  // planes on dive
  ControlBar& bar = rig.screen->bar();
  CHECK(bar.current_message() == "Planesman: Planes on dive, sir.");
  rig.key(0x0e08);
  CHECK(bar.current_message() == "Planesman: Planes on dive, sir.");
  rig.key(0x0e08);
  CHECK(bar.current_message() == "Planesman: Planes on rise, sir.");
  CHECK(bar.message_state() == 1);
}

TEST_CASE("Esc and END MISSION: Abort, then YES, ends the mission", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  rig.key(0x011b);  // Esc: the END MISSION? menu
  CHECK(rig.mission().paused());
  rig.frames(2);
  rig.save("end_mission_menu");
  rig.key(0x1e61);  // 'a': Abort
  rig.frames(2);
  CHECK(rig.mission().end_code() == 0);
  rig.save("abort_yes_no");
  rig.key(0x1579);  // 'y': YES
  CHECK(rig.mission().end_code() == 1);
  CHECK(!rig.frames(2));
  CHECK(rig.screen->finish() == 1);
}

TEST_CASE("Android's Back on the conn opens and closes END MISSION", "[stations][back]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  rig.gui.mouse().post_back(0);
  rig.frames(2);
  CHECK(rig.mission().paused());
  rig.gui.mouse().post_back(0);
  rig.frames(2);
  CHECK(!rig.mission().paused());
  CHECK(rig.mission().end_code() == 0);
  CHECK(rig.frames(2));
}

TEST_CASE("Alt-Q, NO keeps the mission going", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  const VgaPalette before = rig.screen->dac();
  rig.key(0x1000);  // Alt-Q
  rig.frames(1);
  CHECK(rig.screen->dac() != before);  // greyed
  rig.key(0x316e);  // 'n'
  rig.frames(1);
  CHECK(rig.mission().end_code() == 0);
  CHECK(rig.screen->dac() == before);
  CHECK(!rig.mission().paused());
  CHECK(rig.frames(2));
}

TEST_CASE("The 1MC menu rigs for red and the conn reopens with CONN.PAL", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  const VgaPalette white = rig.screen->env().palette;
  rig.key(0x326d);  // 'm': 1MC
  rig.frames(2);
  rig.save("menu_1mc");
  // The selection starts on Cancel; Up skips the disabled items (on the
  // surface: silent running, debris, pilot) to "Rig For Red". Then Enter.
  rig.key(0x4800);
  rig.key(0x1c0d);
  rig.frames(2);
  CHECK(rig.screen->ui_state().rig_for_red);
  rig.settle();
  CHECK(rig.screen->env().palette != white);
  CHECK(rig.screen->dac() == rig.screen->env().palette);
  CHECK(rig.screen->bar().current_message() == "OOD: Rig for red, aye sir!");
  rig.save("conn_red");
}

TEST_CASE("GAME OVER fades to blue and ends the mission", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  rig.mission().trigger_game_over(GameOverReason::kOxygen);
  int frames = 0;
  while (rig.frames(1) && frames < 500) {
    ++frames;
  }
  CHECK(frames > 10);
  CHECK(rig.screen->finish() == kEndGameOverBase + 2);
  // Every colour but the GUI ones is the blue (0, 0, 30).
  const VgaPalette& dac = rig.screen->dac();
  CHECK(dac[3 * 200] == 0);
  CHECK(dac[3 * 200 + 2] == 30);
  rig.save("game_over");
}

TEST_CASE("The mouse hidden by GAME OVER is shown again when the mission ends", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.gui.mouse().show();  // as the front end leaves it
  rig.frames(1);
  rig.settle();
  REQUIRE(rig.gui.mouse().shown());
  rig.mission().trigger_game_over(GameOverReason::kOxygen);
  for (int frames = 0; rig.frames(1) && frames < 500; ++frames) {
  }
  CHECK_FALSE(rig.gui.mouse().shown());
  rig.screen->finish();
  CHECK(rig.gui.mouse().shown());
}

}  // namespace sh

namespace sh {

TEST_CASE("The time compression button opens its box", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  // The compression button is left of the conn's 1MC button: x 495..567.
  rig.click(530, 470);
  rig.frames(2);
  CHECK(rig.mission().paused());
  rig.save("time_compression_box");
  rig.key(0x0d2b);  // '+' in the box
  rig.key(0x0d2b);
  CHECK(rig.mission().sim().clock().requested_compression() == 4);
  rig.key(0x011b);  // Esc closes it
  rig.frames(2);
  CHECK(!rig.mission().paused());
}

TEST_CASE("Alt-1 fires tube 1", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  const PlayerCombat& combat = rig.mission().player_combat();
  REQUIRE(combat.tubes.content(0) > 0);
  rig.key(0x7800, 1);
  CHECK(combat.tubes.content(0) <= 0);
  CHECK(rig.screen->bar().current_message() == "OOD: Fire 1!");
  // Fired again while it reloads: "Tube 1 will be loaded in n minutes".
  rig.frames(1);
  rig.key(0x7800, 1);
  CHECK(rig.screen->bar().current_message().rfind("OOD: Tube 1 will be loaded in ", 0) == 0);
}

TEST_CASE("Station keys select installed stations only", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  REQUIRE(rig.screen->stations().get(StationId::kTdc) == nullptr);
  rig.key(0x4000);  // F6: no TDC station in this screen
  CHECK(rig.screen->stations().current() == StationId::kConn);
  // F2 raises the periscope on the way to the scope.
  rig.key(0x3c00);
  CHECK(rig.mission().player().periscope_raised());
}

}  // namespace sh

namespace sh {

TEST_CASE("The station keys switch between the control-room stations", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(1);
  rig.settle();
  const auto open = [&rig] { return rig.screen->loop().open_station(); };
  rig.key(0x3f00);  // F5
  rig.settle();
  CHECK(dynamic_cast<ChartStation*>(open()) != nullptr);
  rig.save("switch_chart");
  rig.key(0x4100);  // F7
  rig.settle();
  CHECK(dynamic_cast<GaugesStation*>(open()) != nullptr);
  rig.save("switch_gauges");
  rig.key(0x4200);  // F8: damage page
  rig.settle();
  CHECK(dynamic_cast<StatusStation*>(open()) != nullptr);
  CHECK(rig.screen->ui_state().status_page == 0);
  rig.key(0x4200);  // F8 again: the torpedo page
  rig.settle();
  CHECK(rig.screen->ui_state().status_page == 1);
  rig.save("switch_status_torpedoes");
  rig.key(0x4400);  // F10: the patrol log
  rig.settle();
  CHECK(dynamic_cast<CabinStation*>(open()) != nullptr);
  CHECK(rig.screen->ui_state().cabin_page == 3);
  rig.key(0x7100);  // Alt-F10: the calendar, in place
  rig.settle();
  CHECK(rig.screen->ui_state().cabin_page == 2);
  rig.save("switch_calendar");
  rig.key(0x3b00);  // F1
  rig.settle();
  CHECK(dynamic_cast<ConnStation*>(open()) != nullptr);
  CHECK(rig.screen->stations().current() == StationId::kConn);
}

TEST_CASE("Extended: GAME OPTIONS' EXTENDED page switches the interface at once", "[stations][extended]") {
  SH_REQUIRE_INSTALL(install);
  for (const bool extended : {false, true}) {
    ControlRig rig(install);
    rig.frames(2);
    rig.settle();
    std::vector<Features> saved;
    if (extended) {
      rig.screen->bar().on_features_changed = [&saved](const Features& f) { saved.push_back(f); };
      rig.screen->bar().on_orders = [](const ui::Rect&) {};
    }
    Features& features = rig.screen->env().ui.features;
    // Alt-O, then the EXTENDED button beside SAVE (the dialog at (0x9d, 0x1c)).
    rig.key(0x1800);
    REQUIRE_FALSE(rig.screen->in_play());
    rig.click(0x9d + 0xab + 60, 0x1c + 0x171 + 12);
    if (!extended) {
      // The original's dialog: nothing there.
      CHECK_FALSE(rig.screen->in_play());
      CHECK(features == Features{});
      continue;
    }
    // The page (rows 0x17 apart from 0x32): WIDE SCREEN, the command
    // bar's next layout, both at once.
    const int h = 0x50 + static_cast<int>(kFeatureOptions.size()) * 0x17;
    const int y0 = (kScreenHeight - h) / 2;
    rig.click(0x9d + 0x36 + 8, y0 + 0x32 + 8);
    CHECK(features.wide_screen);
    rig.click(0x9d + 0x36 + 60, y0 + 0x32 + 0x17 + 8);
    CHECK(features.command_bar == CommandBarStyle::kBarOrders);
    CHECK(saved.empty());
    rig.key(0x011b);
    rig.frames(2);
    CHECK(rig.screen->in_play());
    REQUIRE(saved.size() == 1);
    CHECK(saved[0].wide_screen);
    CHECK(saved[0].command_bar == CommandBarStyle::kBarOrders);
  }
}

TEST_CASE("Extended: the in-mission EXTENDED page sets the command bar and the window at once",
          "[stations][extended]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  rig.frames(2);
  rig.settle();
  std::vector<Features> saved;
  rig.screen->bar().on_features_changed = [&saved](const Features& f) { saved.push_back(f); };
  std::vector<DisplaySettings> applied;
  std::optional<DisplayOptions> display =
      DisplayOptions{.settings = {.fullscreen = false, .smoothing = true}, .fullscreen_fixed = false};
  rig.screen->bar().display_options = [&display] { return display; };
  rig.screen->bar().on_display_changed = [&applied](const DisplaySettings& s) { applied.push_back(s); };
  Features& features = rig.screen->env().ui.features;
  features.command_bar = CommandBarStyle::kOriginal;
  const auto open_page = [&rig] {
    rig.key(0x1800);
    REQUIRE_FALSE(rig.screen->in_play());
    rig.click(0x9d + 0xab + 60, 0x1c + 0x171 + 12);
  };
  // The page: the improvements' rows 0x17 apart from 0x32, the command bar
  // second; the window's two rows 0x18 below them.
  const int h = 0x50 + static_cast<int>(kFeatureOptions.size()) * 0x17 + 0x18 + 2 * 0x17;
  const int y0 = (kScreenHeight - h) / 2;
  const int display_y = y0 + 0x32 + static_cast<int>(kFeatureOptions.size()) * 0x17 + 0x18;
  open_page();
  // No touch panel: the extended bar after the original.
  rig.click(0x9d + 0x36 + 60, y0 + 0x32 + 0x17 + 8);
  CHECK(features.command_bar == CommandBarStyle::kBarOrdersTime);
  rig.click(0x9d + 0x36 + 8, display_y + 8);
  REQUIRE(applied.size() == 1);
  CHECK(applied[0] == DisplaySettings{.fullscreen = true, .smoothing = true});
  rig.click(0x9d + 0x36 + 8, display_y + 0x17 + 8);
  REQUIRE(applied.size() == 2);
  CHECK(applied[1] == DisplaySettings{.fullscreen = true, .smoothing = false});
  rig.key(0x011b);
  rig.frames(2);
  CHECK(rig.screen->in_play());
  REQUIRE(saved.size() == 1);
  CHECK(saved[0].command_bar == CommandBarStyle::kBarOrdersTime);

  // Android and --sharp: both greyed, clicks change nothing.
  display = DisplayOptions{
      .settings = {.fullscreen = true, .smoothing = true}, .fullscreen_fixed = true, .smoothing_fixed = true};
  open_page();
  rig.click(0x9d + 0x36 + 8, display_y + 8);
  rig.click(0x9d + 0x36 + 8, display_y + 0x17 + 8);
  CHECK(applied.size() == 2);
  rig.key(0x011b);
  rig.frames(2);
  CHECK(rig.screen->in_play());
}

}  // namespace sh
