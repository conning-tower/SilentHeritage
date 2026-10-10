// The chart screen of the editor tests: a scenario (optionally a game file)
// on the editor's chart with the GUI, fonts and sprites, driven frame by
// frame like `sbchart_run`, with fake dialogs that record their questions
// and give scripted answers.
#pragma once

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/scn_file.h"
#include "core/install.h"
#include "editor/chart_screen.h"
#include "editor/data.h"
#include "editor/options.h"
#include "editor/scenario.h"
#include "platform/screenshot.h"
#include "ui/default_skin.h"

namespace sh::editor::test {

inline const EditorData& editor_data(const Install& install) {
  static const EditorData data = EditorData::load(install);
  return data;
}

// The answers and records of the fake dialogs.
struct FakeDialogs {
  // ask_yes_no / show_message_box: the first line of each, and the answer.
  bool answer = true;
  std::vector<std::string> questions;
  std::vector<std::string> messages;
  // orders_popup_run: each menu shown, and the index chosen (-3 cancels).
  struct Menu {
    std::vector<std::string> labels;
    std::vector<bool> disabled;
    int initial = 0;
  };
  std::vector<Menu> menus;
  int menu_choice = -3;
  // The selection dialogs: the class or type chosen (-1 / -2 cancel) and the
  // boat name given by SELECT SUB TYPE / SELECT SUB NAME.
  std::int32_t ship_choice = -1;
  std::int32_t aircraft_choice = -1;
  std::int32_t sub_choice = -1;
  std::string sub_name;
  // The values the dialogs were opened with.
  std::vector<std::int32_t> opened_with;
};

struct ChartRig {
  explicit ChartRig(const Install& game, std::string_view file = {})
      : install(game),
        data(editor_data(game)),
        scenario(game, data, options, [] { return 1000; }),
        resources(ui::GuiResources::load(game)),
        font1(Font::load(game, "ART/FONT1.FNT")),
        msg(Font::load(game, "ART/MSG.FNT")),
        menu(Font::load(game, "ART/MENU_S.FNT")),
        sbchart(Shp::load(game, "ART/SBCHART.SHP")),
        cursors(Shp::load(game, "ART/SBCURSOR.SHP")) {
    if (!file.empty()) {
      const std::vector<std::uint8_t> bytes = game.read(file);
      REQUIRE(scenario.load(read_scn_file(bytes, file)));
    }
    hooks.ask_yes_no = [this](std::string_view, const std::array<std::string_view, 3>& lines) {
      fake.questions.emplace_back(lines[0]);
      return fake.answer;
    };
    hooks.message = [this](std::string_view, const std::array<std::string_view, 3>& lines) {
      fake.messages.emplace_back(lines[0]);
    };
    hooks.orders_popup = [this](const std::vector<std::string>& labels, const std::vector<bool>& disabled,
                                int initial) {
      fake.menus.push_back({labels, disabled, initial});
      return fake.menu_choice;
    };
    hooks.select_ship_type = [this](std::int32_t& ship_class) {
      fake.opened_with.push_back(ship_class);
      ship_class = fake.ship_choice;
    };
    hooks.select_aircraft_type = [this](std::int32_t& type) {
      fake.opened_with.push_back(type);
      if (fake.aircraft_choice < 0) {
        return ui::kResultCancel;
      }
      type = fake.aircraft_choice;
      return ui::kResultAccept;
    };
    hooks.select_sub_type = [this](std::int32_t& sub_class, ScnChars<31>& name) {
      fake.opened_with.push_back(sub_class);
      if (fake.sub_choice < 0) {
        return ui::kResultCancel;
      }
      sub_class = fake.sub_choice;
      name = {};
      std::copy(fake.sub_name.begin(), fake.sub_name.end(), name.begin());
      return ui::kResultAccept;
    };
    hooks.select_sub_name = [this](std::int32_t sub_class, ScnChars<31>& name) {
      fake.opened_with.push_back(sub_class);
      if (fake.sub_name.empty()) {
        return ui::kResultCancel;
      }
      name = {};
      std::copy(fake.sub_name.begin(), fake.sub_name.end(), name.begin());
      return ui::kResultAccept;
    };
    chart = std::make_unique<ChartScreen>(ChartScreenEnv{game, gui, palette, font1, msg, menu, sbchart, cursors},
                                          scenario, options, hooks, true);
    gui.mouse().update(320, 200, 0, 0);
    chart->open();
    frames(1, true);
    frames(2);
  }

  void frames(int n, bool first = false) {
    for (int i = 0; i < n; ++i) {
      tick += 5;
      while (const std::optional<ui::Event> e = gui.events().get()) {
        chart->handle_event(*e);
        chart->process();
      }
      chart->process();
      chart->update(first && i == 0);
    }
  }
  void key(std::uint16_t word) {
    gui.mouse().post_key(word, 0);
    frames(2);
  }
  // A left click at screen pixel (x, y), then the pointer parked on the
  // panel's title (away from the map and the controls).
  void click_screen(int x, int y) {
    gui.mouse().update(x, y, 0, 0);
    frames(1);
    gui.mouse().update(x, y, ui::kButtonLeft, 0);
    frames(1);
    gui.mouse().update(x, y, 0, 0);
    frames(2);
  }
  // A left click at (x, y) of the open panel's dialog.
  void click_panel(int x, int y) {
    const ui::Dialog* d = chart->panel_dialog();
    REQUIRE(d != nullptr);
    click_screen(d->x() + x, d->y() + y);
  }
  void save(std::string_view name) const {
    const char* dir = std::getenv("SH_SCREENS");
    if (dir != nullptr && *dir != '\0') {
      save_bmp(std::filesystem::path(dir) / (std::string(name) + ".bmp"), screen, to_palette(palette));
    }
  }
  // The water depth (ft) under view pixel (x, y).
  std::int32_t depth_at(int x, int y) const {
    const WorldPoint w = chart->screen_to_world(x, y);
    return -scenario.world().floor().height_at(w.x, w.y) / 16;
  }
  // The first view pixel, scanning rows from the centre, satisfying `pred`.
  template <class Pred>
  std::array<int, 2> find_pixel(Pred pred, int x0 = 330, int y0 = 20) const {
    for (int y = y0; y < ChartScreen::kViewY1 - 20; y += 3) {
      for (int x = x0; x < ChartScreen::kViewX1 - 20; x += 3) {
        if (pred(x, y)) {
          return {x, y};
        }
      }
    }
    FAIL("no such pixel");
    return {0, 0};
  }

  const Install& install;
  const EditorData& data;
  EditorOptions options;
  Scenario scenario;
  ui::GuiResources resources;
  ui::DefaultSkin skin{resources};
  Image8 screen{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  ui::Gui gui{screen, [this] { return tick; }, skin};
  VgaPalette palette{};
  Font font1, msg, menu;
  Shp sbchart, cursors;
  ChartHooks hooks;
  FakeDialogs fake;
  std::unique_ptr<ChartScreen> chart;
};

// The first SHIPS.DAT class of `type`.
inline std::size_t ship_class(const EditorData& data, ShipType type) {
  for (std::size_t i = 0; i < data.tables.ships.size(); ++i) {
    if (data.tables.ships[i].ship_type == type) {
      return i;
    }
  }
  FAIL("no ship class of that type");
  return 0;
}

}  // namespace sh::editor::test
