// Tests of the captain's quarters: the pages and their keys, the calendar
// of a real mission, the patrol log paging and the radio log formatting.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "game/stations/cabin.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

// Installs the cabin in `rig` and returns it.
CabinStation& install_cabin(ControlRig& rig) {
  auto cabin = std::make_unique<CabinStation>(rig.screen->env());
  CabinStation& ref = *cabin;
  rig.screen->install(StationId::kCabin, std::move(cabin));
  return ref;
}

RadioMessage message(std::int32_t template_index) {
  RadioMessage m;
  m.day = 16;
  m.month = 1;
  m.year = 14;
  m.hour = 9;
  m.minute = 5;
  m.template_index = template_index;
  m.args = {14, 5, 16, 1, 0, 0, 0, 0, 0, 0};
  m.from = "ComSubPac";
  m.to = "Skate";
  m.latitude = {31, 5, 'N'};
  m.longitude = {131, 40, 'E'};
  return m;
}

}  // namespace

TEST_CASE("Radio messages fill their RMSG.DAT template", "[stations][content]") {
  SH_REQUIRE_GAME_EXE();
  const std::string bases[] = {"Truk"};
  const std::string types[] = {"Buoy", "City"};
  const RadioTextContext names{"(SS-305) Skate", bases, types};
  RadioMessage m = message(1);
  CHECK(format_radio_message(m, "CALLING \x06...PILOT DOWN...LAST REPORTED POSITION CLOSE TO \x11,\x12", names) ==
        "CALLING (SS-305) SKATE...PILOT DOWN...LAST REPORTED POSITION CLOSE TO 31-05N,131-40E");
  // \x02:\x02 the time, \x01 the day, \x04 the month, \x05 a base.
  CHECK(format_radio_message(m, "AT \x02:\x02, \x01 \x04 OF \x05", names) == "AT 14:05, 16 FEBRUARY OF TRUK");
  m.args = {-1, 1, 0, 0, 0, 0, 0, 0, 0, 0};
  CHECK(format_radio_message(m, "PHOTOGRAPH \x07 THEN \x07", names) == "PHOTOGRAPH HARBOR THEN CITY");
}

TEST_CASE("The cabin pages follow the keys and the hotspots", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  CabinStation& cabin = install_cabin(rig);
  const StationRegistry& stations = rig.screen->stations();
  rig.frames(1);
  rig.settle();

  rig.key(0x4400);  // F10: the patrol log
  CHECK(stations.current() == StationId::kCabin);
  CHECK(cabin.open_page() == 3);
  CHECK(cabin.log_page() == 1);
  rig.settle();
  rig.save("cabin_log");

  rig.key(0x7000);  // Alt-F9: the radio log
  rig.frames(1);
  CHECK(cabin.open_page() == 4);
  rig.settle();
  rig.frames(2);
  rig.save("cabin_radio");

  rig.key(0x7100);  // Alt-F10: the calendar
  rig.frames(1);
  CHECK(cabin.open_page() == 2);
  rig.settle();
  rig.save("cabin_calendar");

  rig.click(320, 200);  // anywhere above the bar: back to the cabin
  rig.frames(1);
  CHECK(cabin.open_page() == 0);
  rig.settle();
  rig.save("cabin");

  rig.click(0x80, 0xc0);  // the log book on the desk
  rig.frames(1);
  CHECK(cabin.open_page() == 3);

  rig.settle();
  rig.key(0x3b00);  // F1: the page is forgotten when leaving
  rig.frames(1);
  CHECK(stations.current() == StationId::kConn);
  CHECK(rig.screen->loop().open_station() != &cabin);
  CHECK(rig.screen->ui_state().cabin_page == 0);
}

TEST_CASE("The conn's cabin door opens the cabin", "[stations]") {
  SH_REQUIRE_INSTALL(install);
  ControlRig rig(install);
  CabinStation& cabin = install_cabin(rig);
  rig.frames(1);
  rig.settle();
  rig.click(250, 200);
  for (int i = 0; i < 40 && rig.screen->stations().current() != StationId::kCabin; ++i) {
    rig.frames(1);
  }
  CHECK(rig.screen->stations().current() == StationId::kCabin);
  rig.frames(1);
  CHECK(cabin.open_page() == 0);
}

TEST_CASE("The calendar of HIST01", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  Mission& mission = rig.mission();
  const GameClock& clock = mission.sim().clock();
  // 16 February 1944, a Wednesday (the header of HIST01.SCN).
  REQUIRE(clock.day() == 16);
  REQUIRE(clock.month() == 1);
  const MissionConditions& c = mission.sim().conditions();
  const std::vector<std::string> lines = calendar_lines(clock, mission.sky(), c.sea_state, c.weather);
  REQUIRE(lines.size() == 10);
  CHECK(lines[0] == "Wednesday");
  CHECK(lines[1] == "16");
  CHECK(lines[2] == "February  1944");
  // Winter in the Pacific at the zone time of the boat's longitude.
  const RiseSetTimes& sun = mission.sky().sun_times();
  CHECK(sun.rise_hour >= 5);
  CHECK(sun.rise_hour <= 8);
  CHECK(sun.set_hour >= 16);
  CHECK(sun.set_hour <= 19);
  CHECK(lines[3].starts_with("SUNRISE - 0"));
  CHECK(lines[7].starts_with("MOON - "));
  CHECK(lines[8].starts_with("SEA - "));
  CHECK(lines[9].starts_with("WEATHER - "));
  UNSCOPED_INFO("calendar: " << lines[3] << " / " << lines[4] << " / " << lines[5] << " / " << lines[6] << " / "
                             << lines[7]);
}

TEST_CASE("The patrol log pages its entries nine at a time", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  CabinStation& cabin = install_cabin(rig);
  Mission& mission = rig.mission();
  const ShipClass& fuso = *mission.tables().find_ship("FUSO");
  const PlayerSub& sub = mission.player();
  const PatrolLogEntry e =
      sinking_log_entry(fuso, mission.sim().clock(), mission.world(), sub.position().x, sub.position().z);
  CHECK(e.name == "FUSO");
  CHECK(e.type == 3);  // Battleship
  CHECK(e.tonnage == static_cast<std::int32_t>(fuso.displacement_tons));
  CHECK(e.day == 16);
  for (int i = 0; i < 9; ++i) {
    cabin.logs().patrol.push_back(e);
  }
  rig.frames(1);
  rig.settle();
  rig.key(0x4400);
  rig.frames(1);
  // Nine entries fill page 1; the totals go on an empty page 2.
  CHECK(cabin.log_pages() == 2);
  rig.settle();
  rig.save("cabin_log_entries");
  rig.click(0x200, 0xc0);  // the right page: next
  rig.frames(1);
  CHECK(cabin.log_page() == 2);
  rig.click(0x80, 0xc0);  // the left page: back
  rig.click(0x80, 0xc0);
  rig.frames(1);
  CHECK(cabin.log_page() == 0);  // the cover
  rig.click(0x80, 0xc0);  // back again: the cabin
  rig.frames(1);
  CHECK(cabin.open_page() == 0);
}

TEST_CASE("The radio log opens at the first unread message", "[stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  CabinStation& cabin = install_cabin(rig);
  for (int i = 0; i < 3; ++i) {
    RadioMessage m = message(1);
    m.unread = i == 2;
    cabin.logs().radio.push_back(m);
  }
  rig.frames(1);
  rig.settle();
  rig.key(0x7000);
  rig.frames(1);
  CHECK(cabin.open_page() == 4);
  CHECK(cabin.radio_pages() == 2);
  CHECK(cabin.radio_page() == 2);
  CHECK(cabin.radio_text(0).starts_with("CALLING (SS-"));
  CHECK(cabin.radio_text(1).empty());
  CHECK(!cabin.logs().radio[2].unread);
  rig.settle();
  rig.frames(2);
  rig.save("cabin_radio_message");
}

}  // namespace sh
