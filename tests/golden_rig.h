// The rigs of the golden traces (golden_trace.h): a mission in play with
// every station (`GoldenMissionRig`), driven by `--keys` scripts
// (app/command_line.h) one input event per frame as the program feeds
// them, with checkpoints that add the game's state to a `GoldenTrace`.
// The whole program's rig (front end, career, editor) is in
// golden_game_rig.h.
//
//   GoldenMissionRig rig(game, Mission::load(...), Features::faithful());
//   rig.play("f2,-40,space,-300");
//   rig.checkpoint("scope");
//   check_golden("real/hist01", rig.trace());
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "core/features.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/mission_save.h"
#include "game/mission_session.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "golden_trace.h"
#include "platform/display.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh::test {

// Returns the name of station `id` in the traces.
std::string_view station_name(StationId id);

// Returns the input events of `keys` in the `--keys` syntax.
std::vector<InputEvent> parse_keys(std::string_view keys);

// A scratch directory removed with the object.
class GoldenTempDir {
 public:
  GoldenTempDir();
  ~GoldenTempDir();
  GoldenTempDir(const GoldenTempDir&) = delete;
  GoldenTempDir& operator=(const GoldenTempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// Feeds input events to a GUI the way the program does (app/game_app.cpp),
// keeping the mouse buttons held.
class GoldenInput {
 public:
  void feed(ui::Gui& gui, const InputEvent& e);

 private:
  std::uint8_t buttons_ = 0;
};

// Follows a mission session's crew messages frame by frame and adds its
// state at checkpoints.
class MissionTracer {
 public:
  // Starts following `session` (its messages from now on).
  void attach(MissionSession& session);
  // Takes the crew messages pushed since the last call; call after every
  // frame.
  void after_frame(MissionSession& session);
  // Adds the state at checkpoint `name`: the clock, the simulation's
  // blocks, the generator, the crew messages since the previous
  // checkpoint, the UI state and, with `frame`, the screen.
  void checkpoint(GoldenTrace& trace, std::string_view name, MissionSession& session, bool frame);

 private:
  std::size_t message_head_ = 0;
  std::string messages_;
};

// The tables and resources of the game (for building missions before the rig).
struct GoldenGame {
  explicit GoldenGame(const Install& game)
      : install(game), paths(ShPaths::load(game)), exe(ExeImage::load(game)), tables(GameTables::load(game)) {}

  const Install& install;
  ShPaths paths;
  ExeImage exe;
  GameTables tables;
};

class GoldenMissionRig {
 public:
  // Plays `mission` (built on `game`'s tables) with `features`, without
  // sound, at 5 ticks a frame.
  // `screen_width` is the window's for the wide-screen layouts.
  GoldenMissionRig(const GoldenGame& game, std::unique_ptr<Mission> mission, Features features = Features::faithful(),
                   int screen_width = kScreenWidth);

  MissionScreen& screen() { return session_->screen(); }
  Mission& mission() { return screen().mission(); }
  const GoldenGame& game() const { return game_; }
  Features features() const { return features_; }
  GoldenTrace& trace() { return trace_; }
  bool over() const { return over_; }

  // Runs `n` frames, or fewer when the mission ends.
  void frames(int n);
  // Plays `keys` in the `--keys` syntax ("f2,-40,alt+1,click:320:200"):
  // each event is fed before a frame of its own.
  void play(std::string_view keys);
  // Adds the state at checkpoint `name` (`MissionTracer::checkpoint`).
  void checkpoint(std::string_view name) { tracer_.checkpoint(trace_, name, *session_, true); }
  // Adds the hash of the MISSn.SAV of the mission at checkpoint `name`;
  // returns the save.
  MissionSaveFile save_checkpoint(std::string_view name);

 private:
  void frame();

  const GoldenGame& game_;
  Features features_;
  ui::GuiResources resources_;
  ui::DefaultSkin skin_{resources_};
  Image8 screen_image_{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick_ = 0;
  ui::Gui gui_{screen_image_, [this] { return tick_; }, skin_};
  GoldenTempDir dir_;
  SaveDir saves_;
  GameOptions options_;
  DetailOptions detail_;
  std::unique_ptr<MissionSession> session_;
  GoldenTrace trace_;
  GoldenInput input_;
  MissionTracer tracer_;
  bool over_ = false;
};

}  // namespace sh::test
