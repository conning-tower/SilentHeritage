// A headless editor for the dialog tests: the editor's GUI on a scripted
// host (clicks, keys and checks run between frames), a scenario on the
// shipped data, and screenshots into SH_SCREENSHOT_DIR when it is set.
#pragma once

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

#include "assets/pcx.h"
#include "core/install.h"
#include "editor/data.h"
#include "editor/editor_ui.h"
#include "editor/options.h"
#include "editor/scenario.h"
#include "game/frontend_host.h"
#include "gfx/sprite.h"
#include "platform/screenshot.h"
#include "ui/event.h"

namespace sh::editor::test {

// The editor's data, loaded once per test run.
inline const EditorData& editor_data(const Install& install) {
  static const EditorData data = EditorData::load(install);
  return data;
}

// A host whose input is a script: each step runs a few frames after the
// previous one (so that the dialog has drawn), with the GUI to post input
// into. Running out of frames throws (a dialog that never closes).
class ScriptedHost final : public FrontendHost {
 public:
  using Step = std::function<void(ui::Gui&)>;
  std::deque<Step> script;
  int frames = 0;
  // The frames a script may take.
  int frame_limit = 20'000;
  // The window's screen width (`screen_width`, the wide-screen layouts).
  int width = kScreenWidth;

  std::uint32_t ticks() override { return tick_; }
  void present(const Image8&, const VgaPalette&) override {
    ++frames;
    tick_ += 2;
    if (frames > frame_limit) {
      throw std::runtime_error("the script did not close the dialog");
    }
  }
  void poll_input(ui::Gui& gui) override {
    if (++idle_ < 4 || script.empty()) {
      return;
    }
    idle_ = 0;
    const Step step = std::move(script.front());
    script.pop_front();
    step(gui);
  }
  int screen_width() override { return width; }
  VideoSound* video_sound() override { return nullptr; }
  void play_sfx(std::int32_t, std::int32_t, std::int32_t) override {}
  void apply_sound_options(const GameOptions&) override {}
  void report(std::string_view) override {}

 private:
  std::uint32_t tick_ = 0;
  int idle_ = 0;
};

// The editor started with all options on, a new scenario and the clock at
// 1000, its screen black with the start-up palette (M8.PCX, the SBCHART
// colours) greyed as the menu dispatcher does.
struct EditorFixture {
  explicit EditorFixture(const Install& install)
      : install(install),
        data(editor_data(install)),
        scenario(install, data, options, [] { return 1000; }),
        ui(install, data.se, host) {
    ui.palette() = load_pcx(install, "ART/M8.PCX").palette;
    sprite_apply_palette(ui.sbchart().frame(0), ui.palette());
    ui.dac() = ui.palette();
    ui.palette_grey_out(50, 255);
  }

  // Script steps.
  // A left click: the press and the release in two steps, as a player's
  // (an edit loop ending on the press posts it again for the dialog).
  void click(int x, int y) {
    host.script.emplace_back([x, y](ui::Gui& gui) { gui.mouse().update(x, y, ui::kButtonLeft, 0); });
    host.script.emplace_back([x, y](ui::Gui& gui) { gui.mouse().update(x, y, 0, 0); });
  }
  void key(std::uint16_t bios_word) {
    host.script.emplace_back([bios_word](ui::Gui& gui) { gui.mouse().post_key(bios_word, 0); });
  }
  void then(std::function<void()> fn) {
    host.script.emplace_back([fn = std::move(fn)](ui::Gui&) { fn(); });
  }
  // Saves the screen as SH_SCREENSHOT_DIR/<name>.bmp when the step runs.
  void screenshot(const std::string& name) {
    then([this, name] {
      const char* dir = std::getenv("SH_SCREENSHOT_DIR");
      if (dir != nullptr && *dir != '\0') {
        save_bmp(std::filesystem::path(dir) / (name + ".bmp"), ui.screen(), to_palette(ui.dac()));
      }
    });
  }

  const Install& install;
  const EditorData& data;
  EditorOptions options;
  Scenario scenario;
  ScriptedHost host;
  EditorUi ui;
};

}  // namespace sh::editor::test
