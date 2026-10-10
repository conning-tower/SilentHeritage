// Running a mission without stations, input or sound: the simulation part
// of the mission loop frame after frame, for tests and command-line tools.
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "core/install.h"
#include "game/mission.h"
#include "sim/context.h"

namespace sh {

class HeadlessRunner {
 public:
  // Runs `mission` (which must outlive the runner) in frames of
  // `ticks_per_frame` 100 Hz timer ticks (5: 20 frames a second).
  explicit HeadlessRunner(Mission& mission, std::int32_t ticks_per_frame = 5);

  // Loads the `.SCN` at `dos_path` with `setup` (`Mission::load`).
  static std::unique_ptr<Mission> load(const Install& install, const GameTables& tables, std::string_view dos_path,
                                       MissionSetup setup = {});

  // Runs frames until `seconds` more of game time have passed or the
  // mission ended (a game over sets the end code as the loop would).
  // Returns the game time run, in hundredths of a second.
  std::int64_t run(std::int32_t seconds);

  // The crew reports raised so far, in order.
  const std::vector<CrewReport>& crew_messages() const { return crew_messages_; }

 private:
  Mission& mission_;
  std::int32_t ticks_per_frame_;
  bool first_frame_ = true;
  std::vector<CrewReport> crew_messages_;
};

}  // namespace sh
