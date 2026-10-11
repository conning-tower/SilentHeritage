#include "app/panel_status.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include "sim/player_sub.h"
#include "sim/torpedo_tubes.h"

namespace sh {

namespace {

// The planes of a crash dive (`order_dive`).
constexpr std::int32_t kCrashDivePlanes = -80;

}  // namespace

std::optional<PanelCommand> depth_order_in_effect(const PlayerSub& sub) {
  const DiveState& dive = sub.dive();
  if (dive.mode == DiveState::Mode::kBlow) {
    return PanelCommand::kBlowBallast;
  }
  if (dive.planes == kCrashDivePlanes) {
    return PanelCommand::kCrashDive;
  }
  if (dive.ordered_depth_ft >= 0) {
    if (dive.ordered_depth_ft == sub.sub().periscope_depth_ft) {
      return PanelCommand::kPeriscopeDepth;
    }
    if (dive.ordered_depth_ft == sub.sub().radar_depth_ft) {
      return PanelCommand::kRadarDepth;
    }
    return std::nullopt;
  }
  if (dive.planes > 0 || dive.depth_ft <= sub.sub().draft_ft) {
    return PanelCommand::kSurface;
  }
  return std::nullopt;
}

PanelStatus command_panel_status(const PlayerSub& sub, const TorpedoTubes& tubes) {
  PanelStatus status;
  status.depth_order = depth_order_in_effect(sub);
  const auto count = static_cast<std::size_t>(std::max(tubes.count(), 0));
  for (std::size_t t = 0; t < kPanelTubeCount && t < count; ++t) {
    TubeStatus& s = status.tubes[t];
    const std::int32_t content = tubes.content(t);
    if (tubes.damage(t).flags != 0) {
      s.state = TubeState::kDamaged;
    } else if (content > 0) {
      s.state = TubeState::kLoaded;
    } else if (content < 0) {
      s.state = TubeState::kReloading;
      const std::int32_t timer = tubes.timer(t);
      const std::int64_t left = std::max<std::int64_t>(timer, 0);
      s.reload_seconds = timer == TorpedoTubes::kNoTimer ? 0 : static_cast<std::int32_t>((left + 0xffff) >> 16);
    } else {
      s.state = TubeState::kEmpty;
    }
  }
  return status;
}

}  // namespace sh
