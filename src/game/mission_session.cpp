#include "game/mission_session.h"

#include <array>
#include <utility>

#include "game/extended_config.h"
#include "game/stations/bridge.h"
#include "game/stations/deckgun.h"
#include "game/stations/radar.h"
#include "game/stations/scope.h"
#include "game/stations/tbt.h"
#include "game/stations/tdc.h"

namespace sh {

namespace {

constexpr std::array<StationId, 6> kAttackStations = {StationId::kBridge, StationId::kScope,  StationId::kTbt,
                                                      StationId::kDeckGun, StationId::kTdc, StationId::kRadar};

}  // namespace

WorldViewOptions world_view_options(const DetailOptions& detail) {
  const std::uint32_t bits = detail.detail_bits;
  return {.detail = {.clouds = (bits & DetailOptions::kClouds) != 0,
                     .waves = (bits & DetailOptions::kWaves) != 0,
                     .land_texture = (bits & DetailOptions::kLandTexture) != 0,
                     .visibility = static_cast<std::int32_t>((bits & DetailOptions::kVisibilityMask) >>
                                                             DetailOptions::kVisibilityShift)},
          .torpedo_wakes = detail.torpedo_wakes != 0,
          .ship_wakes = detail.ship_wakes != 0};
}

MissionSession::MissionSession(MissionScreenSetup setup, std::unique_ptr<Mission> mission, const SaveDir& saves)
    : detail_(setup.detail) {
  const Install& install = setup.install;
  GameOptions& options = setup.options;
  screen_ = std::make_unique<MissionScreen>(setup, std::move(mission));
  MissionScreen& screen = *screen_;
  StationEnv& env = screen.env();
  world_ = std::make_unique<WorldViewAdapter>(install, screen.mission());
  world_->options() = world_view_options(detail_);
  services_ = std::make_unique<AttackServices>(install, screen.mission(), env.gui, env.palette, env.stations,
                                               screen.attack_host(), *world_, env.assets.cursors);
  services_->features = env.ui.features;
  screen.install(StationId::kBridge, std::make_unique<BridgeStation>(*services_));
  screen.install(StationId::kScope, std::make_unique<ScopeStation>(*services_));
  screen.install(StationId::kTbt, std::make_unique<TbtStation>(*services_));
  screen.install(StationId::kDeckGun, std::make_unique<DeckGunStation>(*services_));
  screen.install(StationId::kTdc, std::make_unique<TdcStation>(*services_));
  screen.install(StationId::kRadar, std::make_unique<RadarStation>(*services_));
  // goption_save / gdetail_save after the GAME OPTIONS / DETAIL LEVEL menus.
  screen.bar().on_options_changed = [&saves, &options] { options.save(saves); };
  if (setup.extended_config) {
    // Not in the original: the EXTENDED page's improvements, kept for the
    // next missions and the front end.
    screen.bar().on_features_changed = [&saves](const Features& features) { save_features(saves, features); };
  }
  screen.bar().on_detail_changed = [this, &saves] {
    detail_.save(saves);
    world_->options() = world_view_options(detail_);
  };
}

MissionSession::~MissionSession() {
  for (const StationId id : kAttackStations) {
    screen_->install(id, nullptr);
  }
}

}  // namespace sh
