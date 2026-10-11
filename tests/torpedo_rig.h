// A mission in play with every station for the torpedo type tests (not in
// the original's tests): HIST01 (a Balao, February 1944) with its load
// replaced, the extended variant's torpedo features and rules as asked, an
// injected clock, input helpers and screenshots.
#pragma once

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "assets/exe_image.h"
#include "assets/scenario.h"
#include "core/features.h"
#include "core/install.h"
#include "game/mission.h"
#include "game/mission_session.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game/torpedo_load.h"
#include "platform/screenshot.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh::test {

// The mixed load of the rigs: tubes 18-1, 18-2 and Mark 14, the bow racks
// four 18-1, four 18-2 and two Mark 14, the stern racks two 18-1 and two
// 18-2 (or, `with_mark27`, two Mark 27).
inline Scenario mixed_scenario(const Install& install, bool with_mark27) {
  Scenario s = parse_scenario(install.read("SCEN/HIST01.SCN"), "SCEN/HIST01.SCN");
  s.header.tube_torpedoes = {3, 4, 3, 4, 2, 3, 4, 4, 3, 2};
  s.header.reloads = {};
  s.header.reloads[0] = {3, 2, 4};
  s.header.reloads[1] = {4, with_mark27 ? 0 : 2, 4};
  s.header.reloads[2] = {2, 0, 2};
  s.header.reloads[3] = {with_mark27 ? static_cast<std::int32_t>(kMark27Id) : -1, with_mark27 ? 2 : 0, 0};
  return s;
}

inline MissionRules torpedo_rules(bool mark27) {
  MissionRules r;
  r.mixed_mark18 = true;
  r.mark27 = mark27;
  return r;
}

class TempSaveDir {
 public:
  TempSaveDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_torpedo_rig_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempSaveDir() { std::filesystem::remove_all(path_); }
  TempSaveDir(const TempSaveDir&) = delete;
  TempSaveDir& operator=(const TempSaveDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

struct TorpedoRig {
  // HIST01 with `scenario`'s load (`mixed_scenario`), `features` and
  // `rules`; the tables have the Mark 27.
  TorpedoRig(const Install& game, Scenario scenario, Features features, MissionRules rules)
      : install(game),
        paths(ShPaths::load(game)),
        exe(ExeImage::load(game)),
        tables(load_tables(game)),
        resources(ui::GuiResources::load(game)),
        saves(dir.path()) {
    MissionSetup setup;
    setup.rules = rules;
    session = std::make_unique<MissionSession>(
        MissionScreenSetup{.install = install,
                           .paths = paths,
                           .exe = exe,
                           .tables = tables,
                           .gui = gui,
                           .options = options,
                           .detail = detail,
                           .features = features},
        Mission::create(install, tables, std::move(scenario), std::move(setup)), saves);
  }

  static GameTables load_tables(const Install& game) {
    GameTables t = GameTables::load(game);
    add_extended_torpedoes(t);
    return t;
  }

  MissionScreen& screen() { return session->screen(); }
  Mission& mission() { return screen().mission(); }

  bool frames(int n, std::int32_t ticks = 5) {
    for (int i = 0; i < n; ++i) {
      tick += static_cast<std::uint32_t>(ticks);
      if (!screen().frame(ticks)) {
        return false;
      }
    }
    return true;
  }
  void settle() {
    for (int i = 0; i < 200 && screen().env().dac.fading(); ++i) {
      frames(1);
    }
  }
  void key(std::uint16_t word, int n = 2) {
    gui.mouse().post_key(word, 0);
    frames(n);
  }
  void click(int x, int y) {
    gui.mouse().update(x, y, 0, 0);
    frames(1);
    gui.mouse().update(x, y, ui::kButtonLeft, 0);
    frames(1);
    gui.mouse().update(x, y, 0, 0);
    frames(1);
  }
  // Saves the screen as `name`.bmp into SH_SCREENSHOT_DIR, when set.
  void save(std::string_view name) {
    const char* out = std::getenv("SH_SCREENSHOT_DIR");
    if (out == nullptr || *out == '\0') {
      return;
    }
    save_bmp(std::filesystem::path(out) / (std::string(name) + ".bmp"), screen().screen(), to_palette(screen().dac()));
  }

  const Install& install;
  ShPaths paths;
  ExeImage exe;
  GameTables tables;
  ui::GuiResources resources;
  ui::DefaultSkin skin{resources};
  Image8 screen_image{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen_image, [this] { return tick; }, skin};
  GameOptions options;
  DetailOptions detail;
  TempSaveDir dir;
  SaveDir saves;
  std::unique_ptr<MissionSession> session;
};

}  // namespace sh::test
