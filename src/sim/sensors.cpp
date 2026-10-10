#include "sim/sensors.h"

#include "sim/ai_ship.h"
#include "sim/player_sub.h"
#include "sim/simulation.h"
#include "sim/sonar.h"

namespace sh {

namespace {

constexpr std::int32_t kLostContactTime = 0x3c0000;  // 60 s
// The game-over codes 0x11a894 of the end (GameOverReason in game/mission.h).
constexpr std::int32_t kGameOverContactBroken = 7;  // "Contact with enemy broken."
constexpr std::int32_t kGameOverMissionOver = 9;    // "Mission over."

}  // namespace

void Sensors::update_player_reports(SimContext& ctx, PlayerSub& player, Fixed16 dt) {
  quiet_time_ += dt.raw();
  reports_.report(ctx, player, radar_, env_, dt.raw());
  // player_update (0x5bf89): a minute without contact ends a single
  // mission whose scenario asks for it.
  if (quiet_time_ > kLostContactTime && (ctx.env.options & sensor_option::kContactGroups) == 0 &&
      ctx.env.end_on_lost_contact) {
    ctx.events.game_over = reports_.group_activity() ? kGameOverMissionOver : kGameOverContactBroken;
  }
}

void Sensors::update_player_radar(SimContext& ctx, PlayerSub& player, Fixed16 dt) {
  radar_.update(ctx, player, env_, dt.raw());
}

void Sensors::forget(const Vehicle& removed) {
  if (env_.tdc_target == &removed) {
    env_.tdc_target = nullptr;
  }
  if (env_.gun_target == &removed) {
    env_.gun_target = nullptr;
  }
  reports_.forget(removed);
  if (env_.contact_groups != nullptr) {
    env_.contact_groups->forget(removed);
  }
}

PlayerLookoutState player_lookout_state(const PlayerSub& player, std::int32_t crew) {
  const SubClassParams& sub = player.sub();
  PlayerLookoutState s;
  s.position = player.position();
  s.depth_ft = player.depth_ft();
  s.periscope_raised = player.periscope_raised();
  s.periscope_eye_ft = sub.periscope_height_ft;
  s.bridge_eye_ft = sub.bridge_height_ft;
  s.radar_depth_ft = sub.radar_depth_ft;
  s.awash_depth_ft = sub.awash_depth_ft;
  s.crew = crew;
  return s;
}

Sensors* context_sensors(SimContext& ctx) { return ctx.sim != nullptr ? &ctx.sim->sensors() : nullptr; }

std::int32_t init_thermal_layer(WatcomRandom& random, std::int32_t parameter) {
  // init_thermal_layer (0x4fba9)
  if (parameter == 0) {
    return 10000;
  }
  return parameter * 100 + random.random_int(100);
}

std::int32_t PlayerSub::noise_level() const { return player_noise_level(*this); }

std::int32_t PlayerSub::visual_height() const {
  // The box top of player_update_collision_box (0x5c247).
  std::int32_t top_ft = sub_.bridge_height_ft;
  if (sub_.bridge_height_ft <= dive_.depth_ft) {
    top_ft = periscope_raised_ ? sub_.periscope_height_ft : sub_.radar_depth_ft + 4;
  }
  return motion_.position.y + top_ft * kUnitsPerFoot;
}

}  // namespace sh
