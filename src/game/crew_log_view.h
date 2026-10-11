// The crew log's dialog (`Features::crew_log`, not in the original): an
// in-game menu over the station, the simulation paused, listing the
// mission's crew messages (game/crew_log.h) with their game times.
#pragma once

#include <cstdint>
#include <functional>

#include "game/crew_log.h"
#include "game/station_env.h"

namespace sh {

// The key that opens the log from the command bar and closes it: `L`, of
// either case and on any keyboard layout (unused by the original's keys).
inline constexpr std::uint32_t kCrewLogKey = 0xff4c;

// Opens the log of `log`, which must outlive it: a scrolling list of the
// messages, oldest first, shown from the newest. The arrows, the track,
// dragging the lines and Up / Down / PgUp / PgDn / Home / End scroll it;
// Done, `L`, Esc, a right click or Back close it. `done` runs once it is
// closed.
void open_crew_log(StationEnv& env, const CrewLog& log, std::function<void()> done);

}  // namespace sh
