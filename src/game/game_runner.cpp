#include "game/game_runner.h"

#include <span>
#include <utility>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/scenario.h"
#include "core/error.h"
#include "game/mission.h"
#include "game/mission_session.h"
#include "game/scengen.h"
#include "game/screen_shake.h"
#include "game/stations/cabin.h"
#include "ui/dialog.h"

namespace sh {

namespace {

// run_mission's end code for EXIT WITHOUT SAVING: back to the menu, no
// results.
constexpr std::int32_t kEndAbort = 1;

// Puts a GUI's pump back when it goes out of scope.
struct PumpRestorer {
  ui::Gui& gui;
  std::function<void()> pump;
  ~PumpRestorer() { gui.set_pump(pump); }
};

// Puts the GUI's cursor back when it goes out of scope (the mission's
// cursors die with its screen).
struct CursorRestorer {
  ui::Mouse& mouse;
  const Shp* sprites;
  std::size_t frame;
  ~CursorRestorer() {
    if (sprites != nullptr) {
      mouse.set_cursor(*sprites, frame);
    }
  }
};

// Sets a pointer for the life of the scope.
template <class T>
struct ScopedPointer {
  ScopedPointer(T*& target, T* value) : target_(target) { target_ = value; }
  ~ScopedPointer() { target_ = nullptr; }
  ScopedPointer(const ScopedPointer&) = delete;
  ScopedPointer& operator=(const ScopedPointer&) = delete;

 private:
  T*& target_;
};

// Clears a flag when the scope ends (also by an exception).
struct FlagClearer {
  explicit FlagClearer(bool& flag) : flag_(flag) {}
  ~FlagClearer() { flag_ = false; }
  FlagClearer(const FlagClearer&) = delete;
  FlagClearer& operator=(const FlagClearer&) = delete;

 private:
  bool& flag_;
};

// Ends the front end's resuming when the scope ends (also by an
// exception).
struct ResumingEnder {
  FrontEnd& front_end;
  ~ResumingEnder() { front_end.end_resuming(); }
};

}  // namespace

GameRunner::GameRunner(GameRunnerServices services, FrontendHost& host)
    : services_(std::move(services)), host_(host) {}

GameRunner::~GameRunner() = default;

MissionSetup GameRunner::setup_for(std::uint32_t options, const GameOptions& game_options,
                                   const WatcomRandom& random) const {
  MissionSetup setup;
  setup.options = options;
  setup.speech = game_options.speech != 0;
  setup.auto_sj = game_options.auto_sj_radar != 0;
  setup.auto_sd = game_options.auto_sd_radar != 0;
  setup.random = random;
  setup.previous_map_lat_south = previous_lat_south_;
  setup.previous_player_x = previous_x_;
  setup.previous_player_z = previous_z_;
  return setup;
}

std::int32_t GameRunner::run(const MissionRequest& request) {
  const ResumingEnder resumed{*front_end_};
  outcome_ = MissionOutcome{};
  if (!tables_) {
    tables_ = std::make_unique<GameTables>(GameTables::load(services_.install));
  }
  MissionSetup setup = setup_for(request.options, request.game_options, request.random);
  if (request.resume != nullptr) {
    // The mission of the resume snapshot, restored like a saved one.
    const ResumeSnapshot& resume = *request.resume;
    saved_slot_ = resume.slot;
    std::unique_ptr<Mission> mission;
    try {
      mission = restore_mission(services_.install, *tables_, resume.mission.value(), std::move(setup));
    } catch (const DataError& e) {
      host_.report(e.what());
      return kEndAbort;
    }
    return play(request, std::move(mission), nullptr, &resume.mission->state);
  }
  if (request.saved_mission >= 0) {
    // continue_mission_loop: the mission of MISS<n>.SAV, restored; a
    // damaged save goes back to the menu.
    const std::optional<MissionSaveFile> save = load_mission_save(services_.saves, request.saved_mission);
    if (!save) {
      host_.report(mission_save_file_name(request.saved_mission) + ": not a saved mission");
      return kEndAbort;
    }
    saved_slot_ = {save->header.number, save->header.name};
    std::unique_ptr<Mission> mission;
    try {
      mission = restore_mission(services_.install, *tables_, *save, std::move(setup));
    } catch (const DataError& e) {
      host_.report(e.what());
      return kEndAbort;
    }
    return play(request, std::move(mission), nullptr, &save->state);
  }
  saved_slot_ = SavedMissionSlot{};
  if (request.type == MissionType::kScenario) {
    // Mission::load, with CUSTOM\ read through the player's directory.
    const std::vector<std::uint8_t> data =
        read_scenario_file(services_.install, services_.paths, &services_.custom, request.scenario_path);
    return play(request, Mission::create(services_.install, *tables_, parse_scenario(data, request.scenario_path),
                                         std::move(setup)));
  }
  // init_simulation runs the generator after its first two draws (the
  // restore seed and the fog density), then goes on from its state.
  WatcomRandom random = request.random;
  const MissionStartDraws draws = MissionStartDraws::draw(random);
  GeneratedMission generated = generate_single_mission(services_.install, *tables_, request.type, request.generator,
                                                       request.options, random);
  setup.random = random;
  std::unique_ptr<Mission> mission =
      Mission::create(services_.install, *tables_, std::move(generated), draws, std::move(setup));
  return play(request, std::move(mission));
}

std::int32_t GameRunner::run_patrol(const PatrolRequest& request, PatrolResult& result) {
  // run_mission with the patrol built by career_build_patrol_scenario.
  const ResumingEnder resumed{*front_end_};
  outcome_ = MissionOutcome{};
  if (!tables_) {
    tables_ = std::make_unique<GameTables>(GameTables::load(services_.install));
  }
  MissionSetup setup = setup_for(request.options, request.game_options, request.random);
  Patrol patrol(services_.install, *tables_, *request.career, request.plan);
  MissionRequest mission_request;
  mission_request.game_options = request.game_options;
  std::unique_ptr<Mission> mission;
  try {
    mission = patrol.create_mission(std::move(setup));
  } catch (const DataError& e) {
    // A damaged roster state: back to the base menu, the career as saved.
    host_.report(e.what());
    return kEndAbort;
  }
  const std::int32_t end = play(mission_request, std::move(mission), &patrol, patrol.resumed_state());
  result.end = patrol_end_;
  result.outcome = outcome_;
  return end;
}

void GameRunner::present(const MissionScreen& screen) {
  if (screen.display_start() == 0) {
    host_.present(screen.screen(), screen.dac());
  } else {
    host_.present(displayed_frame(screen.screen(), screen.display_start()), screen.dac());
  }
}

std::int32_t GameRunner::play(const MissionRequest& request, std::unique_ptr<Mission> mission, Patrol* patrol,
                              const SimStateFile* restored) {
  ui::Gui& gui = front_end_->gui();
  if (restored == nullptr) {
    // The end of init_simulation: the briefing of a new mission (0x11bcfc,
    // cleared for a saved mission and a career resumed on patrol).
    front_end_->mission_briefing_screen(*mission);
  }
  GameOptions options = request.game_options;
  DetailOptions detail = DetailOptions::load(services_.saves);
  const CursorRestorer front_end_cursor{gui.mouse(), gui.mouse().cursor_sprites(), gui.mouse().cursor_frame()};
  MissionSession session({.install = services_.install,
                          .paths = services_.paths,
                          .exe = services_.exe,
                          .tables = *tables_,
                          .gui = gui,
                          .sound = services_.sound,
                          .options = options,
                          .detail = detail,
                          .sound_lock = services_.sound_lock},
                         std::move(mission), services_.saves);
  MissionScreen& screen = session.screen();
  // The blocking loops of the in-game dialogs (the SAVE MISSION name
  // entry) show the mission's screen with its palette.
  const PumpRestorer front_end_pump{gui, gui.pump_function()};
  gui.set_pump([this, &gui, &screen] {
    gui.mouse().draw(gui.screen());
    present(screen);
    gui.mouse().erase(gui.screen());
    in_play_ = false;
    host_.poll_input(gui);
  });
  if (restored != nullptr) {
    restore_session_state(*restored, {.screen = &screen, .attack = &session.services()});
  }
  // The GAME OPTIONS menu's SAVE MISSION / SAVE CAREER.
  const SaveDir& saves = services_.saves;
  if (patrol != nullptr) {
    screen.bar().on_save = [&session, patrol, &saves] { cmd_save_career(session, *patrol, saves); };
  } else {
    screen.bar().on_save = [this, &session, &saves] {
      cmd_save_mission(session, services_.install, saves, saved_slot_);
    };
  }
  if (patrol != nullptr) {
    patrol->set_shows_world([&screen] {
      const Station* station = screen.stations().get(screen.stations().current());
      return station != nullptr && station->shows_world();
    });
  }
  {
    const ScopedPointer<MissionSession> in_play(session_, &session);
    const ScopedPointer<Patrol> patrol_in_play(patrol_, patrol);
    const FlagClearer loop_left(in_play_);
    // Restored: the game runs again, the mission in play its snapshot.
    front_end_->end_resuming();
    std::uint32_t last = host_.ticks();
    while (true) {
      in_play_ = screen.in_play();
      host_.poll_input(gui);
      const std::uint32_t now = host_.ticks();
      if (now == last && services_.wait_tick) {
        // The game timer has not ticked: the original's frames last at
        // least one tick.
        services_.wait_tick();
        continue;
      }
      if (!screen.frame(static_cast<std::int32_t>(now - last))) {
        break;
      }
      last = now;
      present(screen);
    }
  }
  // The mission ended in play: a snapshot written meanwhile is stale.
  delete_resume_snapshot(services_.saves);
  std::int32_t end = screen.finish();
  if (patrol != nullptr) {
    // mission_shutdown's wp_free_patrol_data: the fuel home.
    patrol_end_ = patrol->finish(screen.mission());
    end = patrol_end_.end_code;
  }
  // What the results screen reads: the boat, the ships afloat and the
  // patrol log the captain's quarters keep.
  auto* cabin = dynamic_cast<CabinStation*>(screen.stations().get(StationId::kCabin));
  outcome_ = mission_outcome(screen.mission(), end,
                             cabin != nullptr ? std::span<const PatrolLogEntry>(cabin->logs().patrol)
                                              : std::span<const PatrolLogEntry>());
  if (patrol != nullptr) {
    outcome_.fuel = patrol_end_.fuel;
  }
  // mission_shutdown (0x506cc) reseeds the game's generator; the next
  // mission starts from this map and boat position.
  const Mission& ended = screen.mission();
  front_end_->random().srand(ended.restore_seed());
  previous_lat_south_ = ended.world().bounds ? ended.world().bounds->lat_south : Fixed16();
  previous_x_ = ended.player().position().x;
  previous_z_ = ended.player().position().z;
  return end;
}

std::optional<ResumeSnapshot> GameRunner::snapshot() {
  if (session_ == nullptr) {
    if (front_end_->launch_resume_pending()) {
      return std::nullopt;
    }
    if (const ResumeSnapshot* resuming = front_end_->resuming()) {
      return *resuming;
    }
  }
  ResumeSnapshot s;
  if (session_ != nullptr) {
    Mission& mission = session_->screen().mission();
    const SessionStateParts parts{.screen = &session_->screen(), .attack = &session_->services()};
    s = patrol_ != nullptr
            ? patrol_resume_snapshot(mission, *patrol_, parts)
            : mission_resume_snapshot(services_.install, mission, parts, saved_slot_, front_end_->location());
  } else {
    s.location = front_end_->location();
  }
  s.random = front_end_->random().state();
  return s;
}

bool GameRunner::suspend() {
  const std::optional<ResumeSnapshot> s = snapshot();
  return s && write_resume_snapshot(services_.saves, *s);
}

}  // namespace sh
