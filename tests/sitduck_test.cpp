// User report: in CUSTOM/SITDUCK.SCN the crew calls the anchored battleship
// (FUSO, 94,580 yd east of the boat) as soon as the mission starts.
// `player_report_contacts` (0x5d713) does so only without the LIMITED
// VISIBILITY realism option (options bit 7): every contact with a height
// counts as seen at any range; with it, the lookouts' range for the FUSO
// (21,140 yd from the bridge, partly cloudy) is far too short.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

#include "assets/dat_tables.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game_data.h"
#include "sim/contacts.h"
#include "sim/sensors.h"
#include "sim/sensor_environment.h"

namespace sh {

namespace {

constexpr std::int32_t kMsgShipSpotted = 56;  // "Ship spotted, bearing \t."
constexpr std::int32_t kFusoRangeYd = 94580;  // After the first second.

std::unique_ptr<Mission> sitduck(const Install& install, const GameTables& tables, std::uint32_t options) {
  MissionSetup setup;
  setup.options = options;
  return HeadlessRunner::load(install, tables, "CUSTOM/SITDUCK.SCN", setup);
}

}  // namespace

TEST_CASE("SITDUCK: without limited visibility the battleship is called at once", "[sitduck][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = sitduck(install, tables, 0);
  HeadlessRunner runner(*mission);
  runner.run(1);

  REQUIRE_FALSE(runner.crew_messages().empty());
  // Dead ahead: the boat starts heading 072, the FUSO bears 072.6.
  CHECK(runner.crew_messages().front() == CrewReport(kMsgShipSpotted, 0));
  const std::vector<PlayerContact>& contacts = mission->sim().sensors().reports().contacts();
  REQUIRE(contacts.size() == 4);
  CHECK(contacts.front().kind == ContactKind::kShip);
  CHECK(contacts.front().range_yd == kFusoRangeYd);
  CHECK(contacts.front().sighting_range_yd == 21140);
}

TEST_CASE("SITDUCK: with limited visibility nothing is in sight at the start", "[sitduck][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  std::unique_ptr<Mission> mission = sitduck(install, tables, sensor_option::kLimitedVisibility);
  HeadlessRunner runner(*mission);
  runner.run(10);

  CHECK(runner.crew_messages().empty());
  CHECK(mission->sim().sensors().reports().contacts().empty());
}

}  // namespace sh
