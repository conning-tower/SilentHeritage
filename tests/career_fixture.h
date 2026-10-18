// The career screens' test fixture: a front end over an install whose
// GUI gets scripted clicks and keys, and whose mission runner returns a
// given patrol result.
#pragma once

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

#include "assets/exe_image.h"
#include "core/install.h"
#include "game/career_ui.h"
#include "game/frontend.h"
#include "game/patrol.h"
#include "game/shpath.h"
#include "platform/screenshot.h"
#include "ui/dialog.h"

namespace sh::test {

// A scratch directory removed at the end of the test.
class CareerTempDir {
 public:
  CareerTempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_career_menus_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~CareerTempDir() { std::filesystem::remove_all(path_); }
  CareerTempDir(const CareerTempDir&) = delete;
  CareerTempDir& operator=(const CareerTempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// Feeds scripted clicks and keys to the GUI, one every 40 polls.
class ScriptedHost final : public FrontendHost, public MissionRunner {
 public:
  struct Step {
    std::uint16_t key = 0;  // a key, or a click at (x, y)
    int x = 0;
    int y = 0;
    bool back = false;  // Android's Back instead
    // Instead: saves the last frame shown as <shot>.bmp in SH_SCREENSHOT_DIR
    // (when set).
    std::string shot{};
    // The click's press or release alone (with neither, both).
    bool down = false;
    bool up = false;
    // Instead: passes the last frame shown to `probe`.
    bool probing = false;
  };
  static Step click(int x, int y) { return {0, x, y}; }
  static Step press(int x, int y) { return {0, x, y, false, {}, true}; }
  static Step release(int x, int y) { return {0, x, y, false, {}, false, true}; }
  static Step probe_frame() { return {0, 0, 0, false, {}, false, false, true}; }
  static Step key(std::uint16_t k) { return {k, 0, 0}; }
  static Step back() { return {0, 0, 0, true}; }
  static Step screenshot(std::string name) { return {0, 0, 0, false, std::move(name)}; }

  std::deque<Step> script;
  // What the `probe_frame` steps do with the last frame shown.
  std::function<void(const Image8&)> probe;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8& image, const VgaPalette& palette) override {
    if (shots_ != nullptr || probe) {
      last_frame_ = image;
      last_palette_ = palette;
    }
    tick_ += 2;
    if (++frames_ > 50'000) {
      throw std::runtime_error("the script did not end the screen");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 40 || script.empty()) {
      return;
    }
    idle_ = 0;
    const Step s = script.front();
    script.pop_front();
    if (s.probing) {
      probe(last_frame_);
      idle_ = 39;
    } else if (!s.shot.empty()) {
      if (shots_ != nullptr && *shots_ != '\0') {
        save_bmp(std::filesystem::path(shots_) / (s.shot + ".bmp"), last_frame_, to_palette(last_palette_));
      }
      idle_ = 39;
    } else if (s.back) {
      gui.mouse().post_back(0);
    } else if (s.key != 0) {
      gui.mouse().post_key(s.key, 0);
    } else {
      if (!s.up) {
        gui.mouse().update(s.x, s.y, ui::kButtonLeft, 0);
      }
      if (!s.down) {
        gui.mouse().update(s.x, s.y, 0, 0);
      }
    }
  }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view) override {}
  std::int32_t run(const MissionRequest&) override { return 1; }
  MissionOutcome outcome() const override { return {}; }
  // A patrol "played" by returning `patrol_result` with `patrol_end`; the
  // request is kept.
  std::int32_t run_patrol(const PatrolRequest& request, PatrolResult& result) override {
    patrol_request = request;
    result = patrol_result;
    return patrol_end;
  }

  std::int32_t patrol_end = 1;
  PatrolResult patrol_result;
  std::optional<PatrolRequest> patrol_request;

 private:
  const char* shots_ = std::getenv("SH_SCREENSHOT_DIR");
  Image8 last_frame_;
  VgaPalette last_palette_{};
  std::uint32_t tick_ = 0;
  int idle_ = 0;
  int frames_ = 0;
};

// A front end over the install with the scripted host, for the screens.
struct Fixture {
  explicit Fixture(const Install& install, bool extended_config = false)
      : paths(ShPaths::load(install)),
        exe(ExeImage::load(install)),
        saves(dir.path(), &install, paths.dir(GameDir::kSave)),
        front_end({.install = install, .paths = paths, .exe = exe, .saves = saves, .extended_config = extended_config},
                  host, host, 1234),
        ui(front_end) {}

  CareerTempDir dir;
  ShPaths paths;
  ExeImage exe;
  SaveDir saves;
  ScriptedHost host;
  FrontEnd front_end;
  CareerUi ui;
};

// The centre of a button `w` wide at (x, y) of a titled dialog at (dx, dy).
inline ScriptedHost::Step button(int dx, int dy, int x, int y, int w) { return ScriptedHost::click(dx + x + w / 2, dy + y + 17); }

}  // namespace sh::test
