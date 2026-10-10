#include "game/headless.h"

#include <utility>

namespace sh {

HeadlessRunner::HeadlessRunner(Mission& mission, std::int32_t ticks_per_frame)
    : mission_(mission), ticks_per_frame_(ticks_per_frame) {}

std::unique_ptr<Mission> HeadlessRunner::load(const Install& install, const GameTables& tables,
                                              std::string_view dos_path, MissionSetup setup) {
  return Mission::load(install, tables, dos_path, std::move(setup));
}

std::int64_t HeadlessRunner::run(std::int32_t seconds) {
  const std::int64_t target = static_cast<std::int64_t>(seconds) * 100;
  std::int64_t elapsed = 0;
  while (elapsed < target && mission_.end_code() == 0 && !mission_.paused()) {
    MissionEvents events = mission_.simulate_frame(ticks_per_frame_, first_frame_);
    first_frame_ = false;
    elapsed += mission_.sim().clock().frame_ticks();
    crew_messages_.insert(crew_messages_.end(), events.crew_messages.begin(), events.crew_messages.end());
    if (const std::optional<GameOverReason> reason = mission_.game_over()) {
      mission_.set_end_code(static_cast<std::int32_t>(*reason) + kEndGameOverBase);
    }
  }
  return elapsed;
}

}  // namespace sh
