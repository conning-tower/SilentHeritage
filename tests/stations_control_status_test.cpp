// Tests of the status station: the report texts, the damage page's
// compartment selection and the torpedo page's drag and drop.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <memory>

#include "game/stations/status.h"
#include "game_data.h"
#include "stations_control_fixture.h"

namespace sh {

namespace {

using test::ControlRig;

// Opens the status station's damage page (F8) on HIST01.
StatusStation& open_status(ControlRig& rig) {
  auto station = std::make_unique<StatusStation>(rig.screen->env());
  StatusStation& s = *station;
  rig.screen->install(StationId::kStatus, std::move(station));
  rig.frames(1);
  rig.settle();
  rig.key(0x4200);
  rig.frames(1);
  rig.settle();
  return s;
}

// The centre of the first hotspot of `kind` (and `index` when >= 0).
std::pair<int, int> hotspot_centre(const StatusStation& s, std::int32_t kind, std::int32_t index) {
  for (const StatusStation::Hotspot& h : s.hotspots()) {
    if (h.kind == kind && (index < 0 || h.index == index)) {
      return {(h.x0 + h.x1) / 2, (h.y0 + h.y1) / 2};
    }
  }
  FAIL("no such hotspot");
  return {0, 0};
}

}  // namespace

TEST_CASE("Status severity, repair state and repair time texts", "[stations]") {
  // status_severity_index (0x75514)
  CHECK(status_severity_index(20) == 0);
  CHECK(status_severity_index(21) == 1);
  CHECK(status_severity_index(50) == 1);
  CHECK(status_severity_index(51) == 2);
  CHECK(status_severity_index(80) == 2);
  CHECK(status_severity_index(81) == 3);
  // status_repair_state_text (0x75538): the later flags win.
  CHECK(status_repair_state(0) == 0);
  CHECK(status_repair_state(1) == 0);
  CHECK(status_repair_state(2) == 1);
  CHECK(status_repair_state(2 | 4) == 2);
  CHECK(status_repair_state(4 | 8) == 3);
}

TEST_CASE("Status texts come from SH.EXE", "[stations]") {
  SH_REQUIRE_GAME_DATA(install);
  const StatusTexts t = StatusTexts::load(ExeImage::load(install));
  // status_format_repair_time (0x7555f): rounded up to the next minute.
  CHECK(status_format_repair_time(0, t) == "1 min");
  CHECK(status_format_repair_time(59, t) == "1 min");
  CHECK(status_format_repair_time(3540, t) == "1 hour ");
  CHECK(status_format_repair_time(7260, t) == "2 hours 2 min");
  CHECK(t.light_positions[0] == std::array<int, 2>{81, 188});
  CHECK(t.light_positions[23] == std::array<int, 2>{0, 327});
  CHECK(t.torpedoes_blocked == "Torpedoes cannot be fired or loaded");
  CHECK(t.severities[3] == "critical");
  CHECK(t.leak_severities[0] == "minor leaking");
  CHECK(t.repair_states[3] == "surface repair only");
  CHECK(t.alerts[1] == "Chlorine gas detected - surface!");
  CHECK(t.compartments[7] == "Control Room");
  CHECK(t.engine_room == "Engine Room");
  CHECK(t.systems[0].name == "Bow Dive Planes");
  CHECK(t.systems[0].compartment == 1);
  CHECK(t.systems[0].help == "Depth changes will be difficult");
  CHECK(t.systems[4].help.empty());
  CHECK(t.systems[13].flags == 1);
}

TEST_CASE("The damage page selects a compartment by click", "[stations]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  StatusStation& s = open_status(rig);
  REQUIRE(rig.screen->loop().open_station() == &s);
  // 0x11bcdc is 7 at mission start and one less while the page is open:
  // the Control Room (compartment 7).
  CHECK(rig.screen->ui_state().selected_compartment == 6);
  rig.save("status_damage");
  const auto [x, y] = hotspot_centre(s, 0xb, 0);
  rig.click(x, y);
  CHECK(rig.screen->ui_state().selected_compartment == 0);
  rig.frames(2);
  rig.save("status_damage_torpedo_room");
  // Damage there: the report lists it and the board frames the room.
  SystemState& planes = rig.mission().player().systems()[0];
  planes.severity = 60;
  planes.flags = 1 | 4;
  planes.repair_time = 3600;
  CompartmentState& room = rig.mission().player_combat().damage.compartments()[1];
  room.flags = CompartmentState::kBreached | CompartmentState::kFire | CompartmentState::kRepairing;
  room.leak = 30;
  room.repair_timer = 600;
  const std::uint8_t before = rig.screen->screen().at(470, 180);
  rig.frames(30);
  rig.save("status_damage_report");
  CHECK(rig.screen->screen().at(470, 180) != before);
  // Back at the conn the selection is stored as at the start.
  rig.key(0x3b00);
  CHECK(rig.screen->ui_state().selected_compartment == 1);
}

TEST_CASE("A torpedo is dragged from a rack into an empty tube", "[stations]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  StatusStation& s = open_status(rig);
  rig.key(0x4200);  // F8 again: the torpedo page
  rig.frames(1);
  rig.settle();
  REQUIRE(rig.screen->ui_state().status_page == 1);
  rig.save("status_torpedo");
  TorpedoTubes& tubes = rig.mission().player_combat().tubes;
  tubes.auto_load = false;
  const auto& rack = tubes.bow_rack();
  std::int32_t slot = -1;
  for (std::size_t i = 0; i < rack.size(); ++i) {
    if (rack[i] > 0) {
      slot = static_cast<std::int32_t>(i);
      break;
    }
  }
  REQUIRE(slot >= 0);
  const std::int32_t type = rack[static_cast<std::size_t>(slot)];
  const std::int32_t loaded = tubes.content(0);
  tubes.clear(0);
  rig.frames(1);

  const auto [rx, ry] = hotspot_centre(s, 11, slot);
  rig.click(rx, ry);
  CHECK(s.dragged() == type);
  CHECK(s.drag_source() == -1);
  CHECK(rack[static_cast<std::size_t>(slot)] == 0);
  CHECK(tubes.moved_by_hand);
  rig.save("status_torpedo_drag");

  const auto [tx, ty] = hotspot_centre(s, 10, 0);
  rig.click(tx, ty);
  CHECK(s.dragged() == 0);
  // The tube is loading the torpedo (a negative content).
  CHECK(tubes.content(0) == -type);
  CHECK(loaded != 0);
}

TEST_CASE("A loaded torpedo goes back into its own tube at once", "[stations]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  StatusStation& s = open_status(rig);
  rig.key(0x4200);
  rig.frames(1);
  rig.settle();
  TorpedoTubes& tubes = rig.mission().player_combat().tubes;
  const std::int32_t type = tubes.content(0);
  REQUIRE(type > 0);
  const auto [tx, ty] = hotspot_centre(s, 10, 0);
  rig.click(tx, ty);
  CHECK(s.dragged() == type);
  CHECK(s.drag_source() == 0);
  CHECK(tubes.content(0) == 0);
  rig.click(tx, ty);
  CHECK(s.dragged() == 0);
  CHECK(tubes.content(0) == type);
}

}  // namespace sh
