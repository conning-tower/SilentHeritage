// The mission layer of the game program: plays the front end's single
// missions (new, continued from MISSn.SAV or resumed from the resume
// snapshot) and career patrols with every station (`run_mission`'s
// `mission_main_loop`) on the front end's GUI, through the program's
// `FrontendHost`, and captures the game for the resume snapshot
// (game/resume.h) when the program is suspended.
//
//   GameRunner runner(services, host);
//   FrontEnd front_end(frontend_services, host, runner, seed);
//   runner.set_front_end(front_end);
//   front_end.run();
//   // From the host's input when the system suspends the program:
//   runner.suspend();
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

#include "core/features.h"
#include "game/frontend.h"
#include "game/frontend_host.h"
#include "game/frontend_results.h"
#include "game/mission_save.h"
#include "game/patrol.h"
#include "game/resume.h"

namespace sh {

class ExeImage;
class Install;
class MelLayer;
class MissionSession;
class SaveDir;
class SoundLock;
struct GameTables;

struct GameRunnerServices {
  const Install& install;
  const ShPaths& paths;
  const ExeImage& exe;
  const SaveDir& saves;
  // The CUSTOM directory the scenarios of LOAD CUSTOM MISSION are read
  // from (the player's over the install's).
  const SaveDir& custom;
  // The missions' sound layer, or null for none; `sound_lock` is held
  // around each use when an audio thread renders it.
  MelLayer* sound = nullptr;
  SoundLock* sound_lock = nullptr;
  // Waits a little for the host's clock to move when it has not ticked
  // since the last mission frame (the original's frames last at least one
  // tick); empty to run the frame at once (a headless run whose clock
  // moves when frames are shown).
  std::function<void()> wait_tick;
  // The improvements beyond the original the missions run with.
  Features features{};
};

class GameRunner final : public MissionRunner {
 public:
  // Everything `services` refers to and `host` must outlive the runner.
  GameRunner(GameRunnerServices services, FrontendHost& host);
  ~GameRunner() override;
  GameRunner(const GameRunner&) = delete;
  GameRunner& operator=(const GameRunner&) = delete;

  // The front end whose GUI the missions use; must be set before a run.
  void set_front_end(FrontEnd& front_end) { front_end_ = &front_end; }

  // MissionRunner. A request with `resume` restores the snapshot's mission
  // at its station without a briefing. The resume snapshot is deleted
  // when a mission ends. Once a mission is in play, or the run returns,
  // the front end's resuming is over (`FrontEnd::end_resuming`).
  std::int32_t run(const MissionRequest& request) override;
  MissionOutcome outcome() const override { return outcome_; }
  std::int32_t run_patrol(const PatrolRequest& request, PatrolResult& result) override;

  // The mission in play (between its restore and its end), or null.
  MissionSession* session() { return session_; }
  // Returns whether the input being polled is the mission's main loop's
  // with the mission in play (`MissionScreen::in_play`): not the front
  // end's, nor a blocking dialog's (the SAVE MISSION name entry).
  bool mission_in_play() const { return in_play_; }

  // Returns the game as a resume snapshot: the single mission or career
  // patrol in play, else the snapshot the front end is resuming (its game
  // not restored yet), else the front end's place. Returns nothing before
  // the front end has taken the launch's snapshot, which RESUME.SAV still
  // holds (`FrontEnd::launch_resume_pending`).
  std::optional<ResumeSnapshot> snapshot();
  // Writes `snapshot()` as RESUME.SAV; returns false when it cannot be
  // written or there is nothing to write.
  bool suspend();

 private:
  // Plays `mission` with all its stations until it ends (then ends
  // `patrol` when it is a career patrol's); returns its end code.
  // `restored`: the state lists the mission was restored from, whose
  // session part (logs, pages, station) is restored on the screen.
  std::int32_t play(const MissionRequest& request, std::unique_ptr<Mission> mission, Patrol* patrol = nullptr,
                    const SimStateFile* restored = nullptr);
  // Returns the set-up of a mission of `request`'s options and rules.
  MissionSetup setup_for(std::uint32_t options, const GameOptions& game_options, const WatcomRandom& random,
                         const MissionRules& rules) const;
  // Shows the mission's screen from the screen shake's display start.
  void present(const MissionScreen& screen);

  GameRunnerServices services_;
  FrontendHost& host_;
  FrontEnd* front_end_ = nullptr;
  std::unique_ptr<GameTables> tables_;
  MissionOutcome outcome_;
  PatrolEnd patrol_end_;  // the last patrol's
  // The save the single mission was continued from or saved to.
  SavedMissionSlot saved_slot_;
  // What the next mission's set-up reads of the previous one.
  Fixed16 previous_lat_south_;
  std::int32_t previous_x_ = 0;
  std::int32_t previous_z_ = 0;
  // The mission in play and its patrol (null for a single mission).
  MissionSession* session_ = nullptr;
  Patrol* patrol_ = nullptr;
  bool in_play_ = false;
};

}  // namespace sh
