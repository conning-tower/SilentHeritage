// The views' own random generator (`Features::draw_random`): with it, the
// simulation after a stretch of play is the same whatever station is shown;
// without it (the original), the views' random details draw from the
// game's generator.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "assets/scenario.h"
#include "core/features.h"
#include "core/fixed_math.h"
#include "game/mission.h"
#include "game/sim_state.h"
#include "game_data.h"
#include "golden_rig.h"

namespace sh::test {
namespace {

// HIST01 with the boat surfaced 1200 yd abeam of its first ship, facing it.
std::unique_ptr<Mission> close_to_the_convoy(const GoldenGame& game) {
  Scenario scenario = parse_scenario(game.install.read("SCEN/HIST01.SCN"), "HIST01");
  const ShipRecord& ship = scenario.ships.front();
  PlayerSubRecord& player = *scenario.player_sub;
  const double h = ship.heading * std::numbers::pi / 180;
  const double d = 1200.0 * kUnitsPerYard;
  player.x = ship.x + static_cast<std::int32_t>(d * std::cos(h));
  player.y = ship.y - static_cast<std::int32_t>(d * std::sin(h));
  player.heading = fx::heading_from_delta(ship.x - player.x, ship.y - player.y).tenths() / 10;
  MissionSetup setup;
  setup.random = WatcomRandom(3);
  return Mission::create(game.install, game.tables, std::move(scenario), setup);
}

// The game's state after the deck gun fired a few rounds at the ship and
// `frames` frames with station `key` shown: its generator and its saved
// simulation.
struct SimOutcome {
  std::uint32_t random = 0;
  std::vector<std::uint8_t> state;
  bool operator==(const SimOutcome&) const = default;
};
SimOutcome play_at(const GoldenGame& game, Features features, std::string_view key, int frames) {
  GoldenMissionRig rig(game, close_to_the_convoy(game), features);
  rig.play("alt+f4,-120,F,-40,F,-40,F,-2");
  rig.play(std::string(key) + ",-" + std::to_string(frames));
  Mission& mission = rig.mission();
  return {mission.sim().random().state(), capture_sim_state(mission, {}, {}).serialize()};
}

TEST_CASE("Draw random: the simulation does not depend on the station shown", "[draw_random][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const GoldenGame game(install);
  constexpr int kFrames = 200;
  // The deck gun keeps its shells' tracers in view; the TBT looks ahead,
  // away from them. Both show the world and are the same sensor station for
  // the simulation.
  SECTION("Faithful: the bridge's tracers draw from the game's generator") {
    CHECK(play_at(game, Features::faithful(), "alt+f4", kFrames) != play_at(game, Features::faithful(), "f3", kFrames));
  }
  SECTION("Own generator: the same game") {
    Features features;
    features.draw_random = true;
    CHECK(play_at(game, features, "alt+f4", kFrames) == play_at(game, features, "f3", kFrames));
  }
}

}  // namespace
}  // namespace sh::test
