// The whole program for the golden traces (golden_trace.h): the front end,
// the mission runner and the scenario editor on a host without SDL, driven
// by a script, as `run_game` puts them together.
//
// A script is a comma-separated list of steps, taken one per input poll:
//
//   <key>        one step of the `--keys` syntax (app/command_line.h): a
//                key, "-N" (N polls without input), "click:X:Y"...
//   @<name>      a checkpoint (`checkpoint`)
//   until:mission  waits until a mission is in play
//   until:menu     waits until no mission is in play
//   until:editor   waits until the scenario editor's chart runs
//   call:<name>  calls the hook `name` (`hooks`)
//
// The run ends after the last step (or when the front end returns).
//
//   GoldenGameRig rig(install, Features::classic());
//   rig.run("-60,@menu,click:145,190,-40,@single");
//   check_golden("real/frontend", rig.trace());
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "assets/exe_image.h"
#include "core/features.h"
#include "core/install.h"
#include "editor/editor.h"
#include "game/frontend.h"
#include "game/frontend_host.h"
#include "game/game_runner.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "golden_rig.h"
#include "golden_trace.h"

namespace sh::test {

class GoldenGameRig final : public FrontendHost, public EditorRunner {
 public:
  // The program on `install` with `features`, its SAVE and CUSTOM
  // directories empty in a scratch directory, the generator seeded with
  // `seed`, without the intro.
  GoldenGameRig(const Install& install, Features features = Features::classic(), std::uint32_t seed = 1);
  ~GoldenGameRig() override;

  // Runs the program on `script` until its end. Fails the test when a
  // step waits for more than 20000 polls.
  void run(std::string_view script);

  // Adds the state at checkpoint `name`: the front end's place and
  // generator, the mission in play (`MissionTracer::checkpoint`), the last
  // picture shown and every file of the SAVE and CUSTOM directories.
  void checkpoint(std::string_view name);

  GoldenTrace& trace() { return trace_; }
  // The hooks of the `call:` steps.
  std::map<std::string, std::function<void(GoldenGameRig&)>, std::less<>> hooks;

  FrontEnd& front_end() { return *front_end_; }
  GameRunner& runner() { return *runner_; }
  // The editor while it runs, else null.
  editor::Editor* editor() { return editor_; }
  const std::filesystem::path& dir() const { return dir_.path(); }

  // FrontendHost
  std::uint32_t ticks() override { return tick_; }
  void present(const Image8& screen, const VgaPalette& dac) override;
  void poll_input(ui::Gui& gui) override;
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view message) override;
  // EditorRunner
  EditorExit run_editor() override;

 private:
  struct Stop {};

  // Returns whether the step is an input step (taking a poll).
  bool take_step(ui::Gui& gui);

  const Install& install_;
  GoldenTempDir dir_;
  ShPaths paths_;
  ExeImage exe_;
  SaveDir saves_;
  SaveDir custom_;
  std::unique_ptr<GameRunner> runner_;
  std::unique_ptr<FrontEnd> front_end_;
  editor::Editor* editor_ = nullptr;

  std::deque<std::string> steps_;
  std::deque<InputEvent> events_;
  GoldenInput input_;
  int waited_ = 0;
  std::uint32_t tick_ = 0;
  std::uint64_t presents_ = 0;
  Image8 shown_{kScreenWidth, kScreenHeight, 0};
  VgaPalette shown_dac_{};
  MissionSession* traced_session_ = nullptr;
  MissionTracer tracer_;
  GoldenTrace trace_;
};

}  // namespace sh::test
