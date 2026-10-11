// A mission in play with every station for the golden traces
// (golden_trace.h): driven by `--keys` scripts (app/command_line.h), one
// input event per frame as the program feeds them, with checkpoints that
// add the game's state to a `GoldenTrace`.
//
//   GoldenMissionRig rig(install, Mission::load(...), Features::faithful());
//   rig.play("f2,-40,space,-300");
//   rig.checkpoint("scope");
//   check_golden("real/hist01", rig.trace());
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "app/command_line.h"
#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "core/features.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/mission_save.h"
#include "game/mission_session.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game/sim_state.h"
#include "golden_trace.h"
#include "platform/bios_keys.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh::test {

// Returns the name of station `id` in the traces.
inline std::string_view station_name(StationId id) {
  constexpr std::string_view kNames[kStationCount] = {"chart",  "conn",  "bridge", "deckgun", "gauges", "scope",
                                                      "radar",  "status", "tbt",   "tdc",     "cabin"};
  return kNames[static_cast<std::size_t>(id)];
}

// A scratch directory removed with the object.
class GoldenTempDir {
 public:
  GoldenTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_golden_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~GoldenTempDir() { std::filesystem::remove_all(path_); }
  GoldenTempDir(const GoldenTempDir&) = delete;
  GoldenTempDir& operator=(const GoldenTempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
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
  GoldenMissionRig(const GoldenGame& game, std::unique_ptr<Mission> mission, Features features = Features::faithful())
      : game_(game), features_(features), resources_(ui::GuiResources::load(game.install)), saves_(dir_.path(), &game.install) {
    session_ = std::make_unique<MissionSession>(MissionScreenSetup{.install = game.install,
                                                                   .paths = game.paths,
                                                                   .exe = game.exe,
                                                                   .tables = game.tables,
                                                                   .gui = gui_,
                                                                   .options = options_,
                                                                   .detail = detail_,
                                                                   .features = features},
                                                std::move(mission), saves_);
    message_head_ = screen().bar().log().head();
  }

  MissionScreen& screen() { return session_->screen(); }
  Mission& mission() { return screen().mission(); }
  const GoldenGame& game() const { return game_; }
  Features features() const { return features_; }
  GoldenTrace& trace() { return trace_; }
  bool over() const { return over_; }

  // Runs `n` frames, or fewer when the mission ends.
  void frames(int n) {
    for (int i = 0; i < n && !over_; ++i) {
      frame();
    }
  }

  // Plays `keys` in the `--keys` syntax ("f2,-40,alt+1,click:320:200"):
  // each event is fed before a frame of its own.
  void play(std::string_view keys) {
    const std::string_view args[] = {"--keys", keys};
    for (const InputEvent& e : parse_command_line(args).keys) {
      if (over_) {
        return;
      }
      feed(e);
      frame();
    }
  }

  // Adds the state at checkpoint `name`: the simulation's blocks, the
  // generator, the crew messages since the previous checkpoint, the UI
  // state and the screen.
  void checkpoint(std::string_view name) {
    const GameClock& c = mission().sim().clock();
    trace_.add(name, "sim.time",
               std::format("day {} {:02}:{:02}:{:02}.{:02} x{}/{}/{}", c.day(), c.hour(), c.minute(), c.second(),
                           c.hundredths(), c.effective_compression(), c.requested_compression(),
                           c.compression_cap()));
    const SimStateFile sim = capture_sim_state(mission(), {}, {});
    trace_.add_hash(name, "sim.player", image_bytes(sim.player));
    if (sim.tdc) {
      trace_.add_hash(name, "sim.tdc", image_bytes(*sim.tdc));
    }
    if (sim.deck_gun) {
      trace_.add_hash(name, "sim.deckgun", image_bytes(*sim.deck_gun));
    }
    for (std::size_t i = 0; i < sim.vehicles.size(); ++i) {
      const SimStateFile::Unit& u = sim.vehicles[i];
      trace_.add(name, std::format("sim.vehicle.{}", i),
                 std::format("{} {}", static_cast<std::int32_t>(u.cls), hex64(fnv1a64(image_bytes(u.image)))));
    }
    trace_.add_hash(name, "sim.extension", sim.sim_extension);
    trace_.add_random(name, mission().sim().random());
    trace_.add(name, "msgs", messages_.empty() ? std::string("-") : messages_);
    messages_.clear();
    const SimStateFile ui = capture_sim_state(mission(), {}, {.screen = &screen(), .attack = &session_->services()});
    trace_.add_hash(name, "ui.state", ui.serialize());
    trace_.add_frame(name, station_name(screen().stations().current()), screen().screen(), screen().dac());
  }

  // Adds the hash of the MISSn.SAV of the mission at checkpoint `name`;
  // returns the save.
  MissionSaveFile save_checkpoint(std::string_view name) {
    MissionSaveFile save = capture_mission_save(game_.install, mission(), 1, "GOLDEN",
                                                {.screen = &screen(), .attack = &session_->services()});
    trace_.add_hash(name, "save", save.serialize());
    return save;
  }

 private:
  static std::span<const std::uint8_t> image_bytes(const StateImage& image) { return image.bytes(0, image.size()); }

  void frame() {
    tick_ += 5;
    if (!screen().frame(5)) {
      over_ = true;
    }
    // The crew messages pushed into the bar's ring this frame.
    const CrewMessageLog& log = screen().bar().log();
    while (message_head_ != log.head()) {
      message_head_ = (message_head_ + 1) % CrewMessageLog::kSlots;
      const CrewMessageLog::Entry& e = log.entry(message_head_);
      messages_ += std::format("{}{}({},{})", messages_.empty() ? "" : ",", e.id, e.arg1, e.arg2);
    }
  }

  // Feeds `e` to the GUI as the program does (app/game_app.cpp).
  void feed(const InputEvent& e) {
    switch (e.type) {
      case EventType::kKeyDown:
      case EventType::kText:
        if (const std::optional<std::uint16_t> word = bios_key_word(e)) {
          gui_.mouse().post_key(*word, bios_shift_state(e.mods));
        }
        break;
      case EventType::kMouseMove:
      case EventType::kMouseDown:
      case EventType::kMouseUp: {
        const std::uint8_t bit = e.button == MouseButton::kLeft    ? ui::kButtonLeft
                                 : e.button == MouseButton::kRight ? ui::kButtonRight
                                                                    : 0;
        if (e.type == EventType::kMouseDown) {
          buttons_ |= bit;
        } else if (e.type == EventType::kMouseUp) {
          buttons_ &= static_cast<std::uint8_t>(~bit);
        }
        gui_.mouse().update(e.x, e.y, buttons_, 0);
        break;
      }
      default:
        break;
    }
  }

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
  std::size_t message_head_ = 0;
  std::string messages_;
  std::uint8_t buttons_ = 0;
  bool over_ = false;
};

}  // namespace sh::test
