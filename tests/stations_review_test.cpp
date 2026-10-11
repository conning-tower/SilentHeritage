// Station regressions: the deck gun's bar gun switch, the
// full redraws of the deck gun and radar, the periscope going down on the
// conn after the periscope station.
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "game/mission_screen.h"
#include "game/stations/attack_services.h"
#include "game/stations/conn.h"
#include "game/stations/deckgun.h"
#include "game/stations/scope.h"
#include "game/world_view_adapter.h"
#include "game_data.h"
#include "sim/player_combat.h"
#include "sim/player_sub.h"
#include "stations_control_fixture.h"

namespace sh {

namespace {

// A mission screen with the attack services and `station` installed at `id`.
struct AttackRig {
  AttackRig(const Install& install, const char* scenario)
      : rig(install, scenario),
        world(install, rig.screen->mission()),
        services(install, rig.screen->mission(), rig.screen->env().gui, rig.screen->env().palette,
                 rig.screen->env().stations, rig.screen->attack_host(), world, rig.screen->env().assets.cursors) {}

  MissionScreen& screen() { return *rig.screen; }

  test::ControlRig rig;
  WorldViewAdapter world;
  AttackServices services;
};

}  // namespace

TEST_CASE("The deck gun's bar button switches guns after the bar returns", "[simreview][stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  AttackRig r(install, "SCEN/HIST60.SCN");
  MissionScreen& screen = r.screen();
  REQUIRE(screen.mission().player_class().deck_gun_count == 2);
  screen.install(StationId::kDeckGun, std::make_unique<DeckGunStation>(r.services));
  screen.stations().select(StationId::kDeckGun);
  r.rig.frames(5);
  r.rig.settle();
  REQUIRE(screen.stations().current() == StationId::kDeckGun);
  REQUIRE(screen.mission().player_combat().deck_gun.selected() == 0);
  // The bar's second button (the aft gun): the switch rebuilds the bar, so
  // it waits for the station's update (the bar's dialog was still running).
  r.rig.click(0x213, 0x1d5);
  r.rig.frames(2);
  CHECK(screen.mission().player_combat().deck_gun.selected() == 1);
  r.rig.click(0x213, 0x1d5);
  r.rig.frames(2);
  CHECK(screen.mission().player_combat().deck_gun.selected() == 0);
  screen.install(StationId::kDeckGun, nullptr);
}

TEST_CASE("The deck gun station repaints its panel on a full redraw", "[simreview][stations]") {
  SH_REQUIRE_INSTALL(install);
  AttackRig r(install, "SCEN/HIST60.SCN");
  MissionScreen& screen = r.screen();
  screen.install(StationId::kDeckGun, std::make_unique<DeckGunStation>(r.services));
  screen.stations().select(StationId::kDeckGun);
  r.rig.frames(5);
  r.rig.settle();
  REQUIRE(screen.stations().current() == StationId::kDeckGun);
  // Scribble over the manual switch (0x17e, 0x159), then ask for a full
  // redraw (0x11a898).
  Image8& image = r.rig.screen_image;
  auto scribbled = [&] {
    std::int32_t n = 0;
    for (int y = 0x159; y <= 0x18b; ++y) {
      for (int x = 0x17e; x <= 0x1b6; ++x) {
        n += image.at(x, y) == 0xfe ? 1 : 0;
      }
    }
    return n;
  };
  for (int y = 0x159; y <= 0x18b; ++y) {
    for (int x = 0x17e; x <= 0x1b6; ++x) {
      image.set(x, y, 0xfe);
    }
  }
  const std::int32_t before = scribbled();
  screen.env().ui.redraw = true;
  r.rig.frames(2);
  CHECK(scribbled() < before);
  screen.install(StationId::kDeckGun, nullptr);
}

TEST_CASE("Lowering the periscope at its station animates it on the conn", "[simreview][stations]") {
  SH_REQUIRE_INSTALL(install);
  AttackRig r(install, "SCEN/HIST01.SCN");
  MissionScreen& screen = r.screen();
  screen.install(StationId::kScope, std::make_unique<ScopeStation>(r.services));
  r.rig.frames(2);
  {
    SimContext ctx = screen.mission().sim().context();
    screen.mission().player().toggle_periscope(ctx);
  }
  REQUIRE(screen.mission().player().periscope_raised());
  screen.stations().select(StationId::kScope);
  r.rig.frames(5);
  r.rig.settle();
  REQUIRE(screen.stations().current() == StationId::kScope);
  {
    SimContext ctx = screen.mission().sim().context();
    screen.mission().player().toggle_periscope(ctx);
  }
  r.rig.frames(2);
  REQUIRE(screen.stations().current() == StationId::kConn);
  const auto& conn = dynamic_cast<const ConnStation&>(*screen.stations().get(StationId::kConn));
  // conn_periscope_start_anim(2) from scope_station_update, kept by the
  // conn's init: the shaft comes down from the top.
  CHECK(conn.animation() == 2);
  CHECK(conn.shaft() > kPeriscopeDown);
  r.rig.frames(60);
  CHECK(conn.animation() == 0);
  CHECK(conn.shaft() == kPeriscopeDown);
  screen.install(StationId::kScope, nullptr);
}

}  // namespace sh
