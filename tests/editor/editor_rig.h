// The whole editor program on the shipped data, headless: its SAVE and
// CUSTOM directories in a scratch directory and its input a script of steps
// (clicks, keys, checks) run between frames by a `ScriptedHost`. Points can
// be computed when their step runs (panel controls, map positions), and
// SH_SCREENSHOT_DIR receives the screenshots asked for.
#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "core/install.h"
#include "editor/editor.h"
#include "editor_ui_fixture.h"
#include "game/shpath.h"
#include "platform/screenshot.h"
#include "temp_dir.h"
#include "ui/dialog.h"

namespace sh::editor::test {

// The bar's titles ('?' at 5, Files at 55, Edit at 128) and the items of
// the popup under a title.
inline constexpr int kTitleY = 13;
inline constexpr int kHelpX = 5;
inline constexpr int kFilesX = 55;
inline constexpr int kEditX = 128;
constexpr int menu_item_y(int n) { return 27 + 9 + n * 24 + 10; }

// The YES and NO keys of the yes / no boxes.
inline constexpr std::uint16_t kKeyYes = 0x1559;
inline constexpr std::uint16_t kKeyNo = 0x314e;

struct EditorRig {
  // `features`: the improvements the editor runs with; `screen_width`: the
  // window's (the wide-screen layouts).
  EditorRig(const Install& game, bool started_by_game, const Features& features = {},
            int screen_width = kScreenWidth)
      : paths(ShPaths::load(game)),
        saves(dir.path() / "SAVE", &game, paths.dir(GameDir::kSave)),
        custom(dir.path() / "CUSTOM", &game, paths.dir(GameDir::kCustom)),
        editor(EditorServices{.install = game, .paths = paths, .saves = saves, .custom = custom, .features = features},
               host, started_by_game, [] { return 1000; }) {
    host.width = screen_width;
    step(wait_running());
  }

  void step(ScriptedHost::Step s) { host.script.push_back(std::move(s)); }
  // A step waiting (taking the next slots) until the chart's loop runs:
  // the steps before would run during the title box.
  ScriptedHost::Step wait_running() {
    return [this](ui::Gui&) {
      if (!editor.running()) {
        host.script.push_front(wait_running());
      }
    };
  }
  void press(int x, int y, std::uint8_t button) {
    step([x, y, button](ui::Gui& gui) { gui.mouse().update(x, y, button, 0); });
    step([x, y](ui::Gui& gui) { gui.mouse().update(x, y, 0, 0); });
  }
  void click(int x, int y) { press(x, y, ui::kButtonLeft); }
  // A left click at the screen point `where` gives when the click starts:
  // the pointer moves there first (as a player's would: the chart ignores
  // presses while its panel thinks the pointer is over it), then the press
  // and the release.
  void click_at(std::function<std::array<int, 2>()> where) {
    auto at = std::make_shared<std::array<int, 2>>();
    step([at, where = std::move(where)](ui::Gui& gui) {
      *at = where();
      gui.mouse().update((*at)[0], (*at)[1], 0, 0);
    });
    step([at](ui::Gui& gui) { gui.mouse().update((*at)[0], (*at)[1], ui::kButtonLeft, 0); });
    step([at](ui::Gui& gui) { gui.mouse().update((*at)[0], (*at)[1], 0, 0); });
  }
  // A left click at (x, y) of the chart's open panel.
  void click_panel(int x, int y) {
    click_at([this, x, y] {
      const ui::Dialog* d = editor.chart().panel_dialog();
      REQUIRE(d != nullptr);
      return std::array<int, 2>{d->x() + x, d->y() + y};
    });
  }
  void key(std::uint16_t word) {
    step([word](ui::Gui& gui) { gui.mouse().post_key(word, 0); });
  }
  void type(std::string_view text) {
    for (const char c : text) {
      key(static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
    }
  }
  // Right click (the menu bar), then the title at `title_x` and its item
  // `n`.
  void menu(int title_x, int n) {
    press(320, 200, ui::kButtonRight);
    click(title_x + 15, kTitleY);
    click(title_x + 100, menu_item_y(n));
  }
  void then(std::function<void()> fn) {
    step([fn = std::move(fn)](ui::Gui&) { fn(); });
  }
  void screenshot(const std::string& name) {
    then([this, name] {
      const char* out = std::getenv("SH_SCREENSHOT_DIR");
      if (out != nullptr && *out != '\0') {
        save_bmp(std::filesystem::path(out) / (name + ".bmp"), editor.ui().screen(), to_palette(editor.ui().dac()));
      }
    });
  }

  TempDir dir;
  ShPaths paths;
  SaveDir saves;
  SaveDir custom;
  ScriptedHost host;
  Editor editor;
};

}  // namespace sh::editor::test
