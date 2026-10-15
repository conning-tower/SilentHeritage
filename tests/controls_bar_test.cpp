// The extended command bar and menus on HIST01's conn: the command bar
// styles (ORDERS in the bar, - and + around the time compression, touches
// a little above the bar), the menus closing on a click outside them, and
// the original bar and menus with the improvements off.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "assets/fnt.h"
#include "core/features.h"
#include "game/command_bar.h"
#include "game/extended_config.h"
#include "game_data.h"
#include "sim/clock.h"
#include "stations_control_fixture.h"
#include "ui/skin.h"

namespace sh {

namespace {

// The conn's bar from the right: 1MC, then ORDERS and the compression.
constexpr int kBarY = 0x1cb;
constexpr int k1mcX = 640 - 72;
constexpr int kOrdersX = k1mcX - 73;
constexpr int kCompressionX = kOrdersX - 73;

// A HIST01 rig on the conn with `features`, the ORDERS hook recording.
struct BarRig {
  BarRig(const Install& install, Features features) : rig(install) {
    rig.screen->ui_state().features = features;
    rig.screen->bar().on_orders = [this](const ui::Rect& r) { orders.push_back(r); };
    rig.frames(1);
    rig.settle();
    // The bar is built when the conn opens: again, with the hook set.
    rig.key(0x3b00);  // F1
    rig.frames(2);
  }
  GameClock& clock() { return rig.mission().sim().clock(); }

  test::ControlRig rig;
  std::vector<ui::Rect> orders;
};

Features style(CommandBarStyle s) {
  Features f;
  f.command_bar = s;
  return f;
}

}  // namespace

TEST_CASE("controls: the command bar styles", "[controls]") {
  // The classic game's: the floating button and the original bar.
  CHECK(Features::classic().command_bar == CommandBarStyle::kOrdersButton);
  CHECK(Features::classic().floating_orders());
  CHECK(Features::extended().command_bar == CommandBarStyle::kBarOrdersTime);
  const Features plain = style(CommandBarStyle::kOriginal);
  CHECK(!plain.floating_orders());
  CHECK(!plain.orders_in_bar());
  CHECK(style(CommandBarStyle::kBarOrders).orders_in_bar());
  CHECK(!style(CommandBarStyle::kBarOrders).bar_time_buttons());
  CHECK(style(CommandBarStyle::kBarOrdersTime).bar_time_buttons());
}

TEST_CASE("controls: ORDERS in the bar opens the panel above it", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  BarRig b(install, style(CommandBarStyle::kBarOrders));
  b.rig.click(kOrdersX + 30, kBarY + 10);
  REQUIRE(b.orders.size() == 1);
  CHECK(b.orders[0].x0 == kOrdersX);
  CHECK(b.orders[0].y0 == kBarY);
  // A touch a few pixels above the bar still takes it (where the station
  // has no hotspot).
  b.rig.click(kOrdersX + 30, kBarY - 2);
  CHECK(b.orders.size() == 2);
  // The compression button left of it still opens its menu.
  b.rig.click(kCompressionX + 30, kBarY + 10);
  b.rig.frames(2);
  CHECK(b.rig.screen->env().modals.modal_active());
}

TEST_CASE("controls: - and + around the bar's time compression", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  BarRig b(install, style(CommandBarStyle::kBarOrdersTime));
  b.rig.save("controls_bar_orders_time");
  const std::int32_t start = b.clock().requested_compression();
  // + on its plate, then a little right of it and above the bar.
  b.rig.click(kCompressionX + 60, kBarY + 10);
  CHECK(b.clock().requested_compression() == start * 2);
  b.rig.click(kCompressionX + 49, kBarY - 2);
  CHECK(b.clock().requested_compression() == start * 4);
  // - a little right of its plate.
  b.rig.click(kCompressionX + 22, kBarY + 10);
  CHECK(b.clock().requested_compression() == start * 2);
  CHECK(!b.rig.screen->env().modals.modal_active());
  // The middle still opens the TIME COMPRESSION box.
  b.rig.click(kCompressionX + 36, kBarY + 10);
  b.rig.frames(2);
  CHECK(b.rig.screen->env().modals.modal_active());
}

TEST_CASE("controls: off, the bar is the original", "[controls][stations][content]") {
  SH_REQUIRE_GAME_DATA(install);
  BarRig improved(install, style(CommandBarStyle::kOrdersButton));
  test::ControlRig plain(install);
  plain.frames(1);
  plain.settle();
  plain.key(0x3b00);
  plain.frames(2);
  // No ORDERS: the compression button is next to 1MC; a press above the
  // bar is not the bar's.
  const std::int32_t start = improved.clock().requested_compression();
  improved.rig.click(kOrdersX + 30, kBarY - 2);
  CHECK(improved.orders.empty());
  CHECK(!improved.rig.screen->env().modals.modal_active());
  improved.rig.click(kOrdersX + 60, kBarY + 10);
  improved.rig.frames(2);
  CHECK(improved.orders.empty());
  CHECK(improved.rig.screen->env().modals.modal_active());
  CHECK(improved.clock().requested_compression() == start);
  // The bar's pixels are the original's.
  const Image8& a = improved.rig.screen->screen();
  const Image8& p = plain.screen->screen();
  bool same = true;
  for (int y = kBarY; y < 480; ++y) {
    for (int x = 0; x < 640; ++x) {
      same = same && a.at(x, y) == p.at(x, y);
    }
  }
  CHECK(same);
}

TEST_CASE("controls: a click outside the 1MC and time compression boxes closes them", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  Features f;
  f.outside_click_closes = true;
  BarRig b(install, f);
  // 1MC (the bar's right button): outside its box, it closes; the game
  // runs again.
  b.rig.click(k1mcX + 30, kBarY + 10);
  b.rig.frames(2);
  REQUIRE(b.rig.screen->env().modals.modal_active());
  b.rig.click(20, 20);
  b.rig.frames(2);
  CHECK(!b.rig.screen->env().modals.modal_active());
  CHECK(b.rig.screen->in_play());
  // TIME COMPRESSION: + in the box, then outside: closed, the new
  // compression kept.
  const std::int32_t start = b.clock().requested_compression();
  b.rig.click(kOrdersX + 30, kBarY + 10);
  b.rig.frames(2);
  REQUIRE(b.rig.screen->env().modals.modal_active());
  b.rig.click(0x97 + 0xfa + 10, 0xc3 + 0x2f + 8);
  b.rig.frames(1);
  CHECK(b.clock().requested_compression() == start * 2);
  b.rig.click(600, 60);
  b.rig.frames(2);
  CHECK(!b.rig.screen->env().modals.modal_active());
  CHECK(b.clock().requested_compression() == start * 2);
}

TEST_CASE("controls: off, a click outside the 1MC box leaves it open", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  BarRig b(install, Features::classic());
  b.rig.click(k1mcX + 30, kBarY + 10);
  b.rig.frames(2);
  REQUIRE(b.rig.screen->env().modals.modal_active());
  b.rig.click(20, 20);
  b.rig.frames(2);
  CHECK(b.rig.screen->env().modals.modal_active());
}

}  // namespace sh

namespace sh {

TEST_CASE("controls: the improved bar's message letters are a pixel closer", "[controls][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Font msg = Font::load(install, "ART/MSG.FNT");
  const std::string text = "OOD: Ship spotted, bearing 093.";
  const GlyphColourTable identity = ui::GuiResources::identity_table();
  Image8 normal(400, 20, 0);
  Image8 tight(400, 20, 0);
  gui_draw_text(Viewport::full(normal), 6, 2, msg, text, &identity);
  draw_tight_text(Viewport::full(tight), 6, 2, msg, text, identity);
  const auto right = [](const Image8& im) {
    int r = -1;
    for (int y = 0; y < im.height(); ++y) {
      for (int x = 0; x < im.width(); ++x) {
        if (im.at(x, y) != 0) {
          r = std::max(r, x);
        }
      }
    }
    return r;
  };
  // The same first letter, the line shorter by a pixel a letter.
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 6 + msg.glyph_width('O') - 1; ++x) {
      CHECK(normal.at(x, y) == tight.at(x, y));
    }
  }
  CHECK(right(tight) == right(normal) - static_cast<int>(text.size() - 1));
}

}  // namespace sh

namespace sh {

TEST_CASE("controls: the command bar labels fit the options pages", "[controls]") {
  SH_REQUIRE_INSTALL(install);
  // The in-mission page's GUI_S.FNT labels (182 px plates) and the main
  // menu's MENU_B.FNT ones (198 px).
  const Font small = Font::load(install, "ART/GUI_S.FNT");
  const Font bold = Font::load(install, "ART/MENU_B.FNT");
  for (const CommandBarStyle s : {CommandBarStyle::kOriginal, CommandBarStyle::kOrdersButton,
                                  CommandBarStyle::kBarOrders, CommandBarStyle::kBarOrdersTime}) {
    CHECK(small.text_width(command_bar_label(s)) <= 178);
    CHECK(bold.text_width(command_bar_label(s)) <= 190);
  }
  for (const FeatureChoice& c : kDesktopCommandBarChoices) {
    CHECK(small.text_width(c.label) <= 178);
    CHECK(bold.text_width(c.label) <= 190);
  }
}

}  // namespace sh

// --- The MISSION menu (`Features::mission_menu`) ------------------------------

namespace sh {

namespace {

// The GAME OPTIONS dialog's rectangle (cmd_game_options' dialog 0x1200c8).
constexpr ui::Rect kGameOptionsBox{0x9d, 0x1c, 0x9d + 0x141 - 1, 0x1c + 0x19c - 1};

// A HIST01 rig on the conn, the mission menu set to `on`, counting the
// saves its `on_save` runs (none when `can_save` is false).
struct MenuRig {
  MenuRig(const Install& install, bool on, bool can_save = true) : rig(install) {
    Features f = Features::for_build();
    f.mission_menu = on;
    rig.screen->ui_state().features = f;
    if (can_save) {
      rig.screen->bar().on_save = [this] { ++saves; };
    }
    rig.frames(1);
    rig.settle();
    rig.frames(2);
  }
  bool menu_open() { return rig.screen->env().modals.modal_active(); }
  std::vector<std::uint8_t> pixels(const ui::Rect& r) const {
    std::vector<std::uint8_t> out;
    for (int y = r.y0; y <= r.y1; ++y) {
      for (int x = r.x0; x <= r.x1; ++x) {
        out.push_back(rig.screen_image.at(x, y));
      }
    }
    return out;
  }

  test::ControlRig rig;
  int saves = 0;
};

constexpr std::uint16_t kEsc = 0x011b;
constexpr std::uint16_t kDown = 0x5000;
constexpr std::uint16_t kEnter = 0x1c0d;

}  // namespace

TEST_CASE("mission menu: Game Options opens Alt-O's page, then the game", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  MenuRig esc(install, true);
  esc.rig.key(kEsc);
  REQUIRE(esc.menu_open());
  esc.rig.save("mission_menu");
  esc.rig.key(0x2267);  // 'g'
  esc.rig.frames(2);
  REQUIRE(esc.menu_open());
  MenuRig alt(install, true);
  alt.rig.key(0x1800);  // Alt-O
  alt.rig.frames(2);
  REQUIRE(alt.menu_open());
  CHECK((esc.pixels(kGameOptionsBox) == alt.pixels(kGameOptionsBox)));
  // Closing it goes back to the game, not to the menu.
  esc.rig.key(kEsc);
  esc.rig.frames(2);
  CHECK(!esc.menu_open());
  CHECK(esc.rig.screen->in_play());
}

TEST_CASE("mission menu: Save Mission runs GAME OPTIONS' save, then the game", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  MenuRig m(install, true);
  m.rig.key(kEsc);
  m.rig.key(0x1f73);  // 's'
  m.rig.frames(2);
  CHECK(m.saves == 1);
  CHECK(!m.menu_open());
  CHECK(m.rig.screen->in_play());
  // The arrows go down the column: Cancel, Game Options, Save Mission.
  m.rig.key(kEsc);
  m.rig.key(kDown);
  m.rig.key(kDown);
  m.rig.key(kEnter);
  m.rig.frames(2);
  CHECK(m.saves == 2);
  CHECK(m.rig.screen->in_play());
}

TEST_CASE("mission menu: Save Mission is disabled when saving is not allowed", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  MenuRig m(install, true, false);
  m.rig.key(kEsc);
  m.rig.key(0x1f73);  // 's': nothing
  m.rig.frames(2);
  CHECK(m.menu_open());
  // Down from Game Options skips it to Abort, which asks.
  m.rig.key(kDown);
  m.rig.key(kDown);
  m.rig.key(kEnter);
  m.rig.frames(2);
  REQUIRE(m.menu_open());
  m.rig.key(0x1579);  // 'y': YES
  CHECK(m.rig.mission().end_code() == 1);
}

TEST_CASE("mission menu: Abort and Return To Base act as END MISSION?'s, in its places", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  // END MISSION?'s rows on the screen (box at y 0xb3, cells at 0x4d and
  // 0x65).
  constexpr int kAbortY = 0xb3 + 0x4d + 8;
  constexpr int kReturnY = 0xb3 + 0x65 + 8;
  for (const bool on : {false, true}) {
    MenuRig m(install, on);
    m.rig.key(kEsc);
    m.rig.click(320, kAbortY);
    m.rig.frames(2);
    CHECK(m.rig.mission().end_code() == 0);
    REQUIRE(m.menu_open());
    m.rig.key(0x1579);  // 'y': YES
    CHECK(m.rig.mission().end_code() == 1);
  }
  // Return To Base: the same answer with the menu as without.
  MenuRig plain(install, false);
  MenuRig menu(install, true);
  for (MenuRig* m : {&plain, &menu}) {
    m->rig.key(kEsc);
    m->rig.click(320, kReturnY);
    m->rig.frames(2);
  }
  CHECK(menu.rig.mission().end_code() == plain.rig.mission().end_code());
  CHECK(menu.menu_open() == plain.menu_open());
  CHECK((menu.pixels(kGameOptionsBox) == plain.pixels(kGameOptionsBox)));
}

TEST_CASE("mission menu: off, END MISSION? has no options or save", "[controls][stations]") {
  SH_REQUIRE_INSTALL(install);
  MenuRig m(install, false);
  MenuRig menu(install, true);
  m.rig.key(kEsc);
  menu.rig.key(kEsc);
  // The original box: from y 0xb3, the menu's two rows taller above.
  const ui::Rect above{0xc9, 0xb3 - 0x30, 0xc9 + 0xef - 1, 0xb3 - 1};
  MenuRig closed(install, false);
  CHECK((m.pixels(above) == closed.pixels(above)));
  CHECK((menu.pixels(above) != closed.pixels(above)));
  m.rig.key(0x2267);  // 'g'
  m.rig.key(0x1f73);  // 's'
  m.rig.frames(2);
  CHECK(m.saves == 0);
  m.rig.key(kEsc);
  m.rig.frames(2);
  CHECK(!m.menu_open());
  CHECK(m.rig.screen->in_play());
}

}  // namespace sh
