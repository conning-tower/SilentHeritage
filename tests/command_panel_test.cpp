// The floating command button of touch screens: hit testing, the keys it
// types, its saved place, and its icons from the install.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>

#include "app/command_icons.h"
#include "game/command_bar.h"
#include "game/shpath.h"
#include "game_data.h"
#include "stations_control_fixture.h"
#include "platform/bios_keys.h"
#include "platform/display.h"
#include "core/image.h"
#include "platform/command_panel.h"
#include "platform/command_panel_view.h"
#include "ui/hotspot.h"

namespace {

using sh::CommandPanel;
using sh::PanelCommand;
using sh::PanelRect;

float cx(const PanelRect& r) { return r.x + r.w / 2; }
float cy(const PanelRect& r) { return r.y + r.h / 2; }

// A panel in a 2208x1840 window at the Pixel Fold's 2.625 scale.
CommandPanel fold_panel(const std::filesystem::path& file = {}) {
  CommandPanel panel(file);
  panel.set_window(2208, 1840, 2.625f);
  return panel;
}

// Returns a press and release at the centre of `r`.
sh::PanelInput tap(CommandPanel& panel, const PanelRect& r) {
  const sh::PanelInput down = panel.press(cx(r), cy(r));
  CHECK(down.consumed);
  return panel.release(cx(r), cy(r));
}

std::filesystem::path temp_file(const char* name) {
  const std::filesystem::path dir = std::filesystem::temp_directory_path() / "sh_command_panel_test";
  std::filesystem::create_directories(dir);
  const std::filesystem::path f = dir / name;
  std::filesystem::remove(f);
  return f;
}

}  // namespace

TEST_CASE("command panel: a tap on the button toggles the panel", "[cmdpanel]") {
  CommandPanel panel = fold_panel();
  const PanelRect b = panel.button_rect();
  // ORDERS: 88 x 40 dp at 2.625 pixels per dp.
  CHECK(b.w == 231.0f);
  CHECK(b.h == 105.0f);
  CHECK_FALSE(panel.is_open());
  // A small wobble under the threshold is still a tap.
  CHECK(panel.press(cx(b), cy(b)).consumed);
  CHECK(panel.move(cx(b) + 20, cy(b)).consumed);
  const sh::PanelInput up = panel.release(cx(b) + 20, cy(b));
  CHECK(up.consumed);
  CHECK_FALSE(up.command);
  CHECK(panel.is_open());
  CHECK(panel.button_rect().x == b.x);
  tap(panel, b);
  CHECK_FALSE(panel.is_open());
}

TEST_CASE("command panel: a drag past the threshold moves the button", "[cmdpanel]") {
  CommandPanel panel = fold_panel();
  const PanelRect b = panel.button_rect();
  panel.press(cx(b), cy(b));
  // 10 dp = 26.25 pixels.
  const sh::PanelInput m = panel.move(cx(b) - 30, cy(b) - 400);
  CHECK(m.consumed);
  CHECK(m.redraw);
  CHECK(panel.dragging());
  panel.release(cx(b) - 30, cy(b) - 400);
  CHECK_FALSE(panel.is_open());
  CHECK(cx(panel.button_rect()) == cx(b) - 30);
  CHECK(cy(panel.button_rect()) == cy(b) - 400);
  // Dragged off the window, it stays inside.
  const PanelRect b2 = panel.button_rect();
  panel.press(cx(b2), cy(b2));
  panel.move(5000, -100);
  panel.release(5000, -100);
  const PanelRect b3 = panel.button_rect();
  CHECK(b3.x + b3.w <= 2208);
  CHECK(b3.y >= 0);
}

TEST_CASE("command panel: + and - under ORDERS type their keys and drag the group", "[cmdpanel]") {
  CommandPanel panel = fold_panel();
  const PanelRect g = panel.group_rect();
  const PanelRect orders = panel.button_rect();
  const auto [up, down] = panel.compression_rects();
  // One rectangle: ORDERS on top, + and - side by side below, 44 dp square.
  CHECK(orders.x == g.x);
  CHECK(orders.y == g.y);
  CHECK(orders.w == g.w);
  CHECK(down.x == g.x);
  CHECK(up.y == orders.y + orders.h);
  CHECK(up.w == g.w / 2);
  CHECK(up.x == down.x + down.w);
  CHECK(down.w == g.w / 2);
  CHECK(up.h == 44 * 2.625f);
  CHECK(up.w == 44 * 2.625f);
  CHECK(up.y + up.h == g.y + g.h);
  // A tap on + or - gives its command, held it shows pressed; the panel
  // stays closed.
  const sh::PanelInput down_press = panel.press(cx(up), cy(up));
  CHECK(down_press.consumed);
  CHECK(down_press.redraw);
  CHECK(panel.compression_pressed() == PanelCommand::kCompressionUp);
  const sh::PanelInput up_release = panel.release(cx(up), cy(up));
  CHECK(up_release.consumed);
  CHECK(up_release.command == PanelCommand::kCompressionUp);
  CHECK_FALSE(panel.compression_pressed());
  CHECK_FALSE(panel.is_open());
  CHECK(tap(panel, down).command == PanelCommand::kCompressionDown);
  // With the panel open they still work, and leave it open.
  tap(panel, orders);
  REQUIRE(panel.is_open());
  CHECK(tap(panel, up).command == PanelCommand::kCompressionUp);
  CHECK(panel.is_open());
  // A drag from - moves the whole group and sends nothing.
  panel.press(cx(down), cy(down));
  panel.move(cx(down) - 200, cy(down) - 300);
  CHECK(panel.dragging());
  CHECK_FALSE(panel.compression_pressed());
  CHECK_FALSE(panel.release(cx(down) - 200, cy(down) - 300).command);
  const PanelRect moved = panel.group_rect();
  CHECK(moved.x == g.x - 200);
  CHECK(moved.y == g.y - 300);
  CHECK(panel.compression_rects()[1].x == down.x - 200);
  // The panel opens beside the whole group.
  const PanelRect p = panel.panel_rect();
  CHECK(p.x + p.w <= moved.x);
}

TEST_CASE("command panel: presses elsewhere reach the game until the panel opens", "[cmdpanel]") {
  CommandPanel panel = fold_panel();
  CHECK_FALSE(panel.press(100, 100).consumed);
  CHECK_FALSE(panel.move(120, 100).consumed);
  CHECK_FALSE(panel.release(120, 100).consumed);
  CHECK_FALSE(panel.other_button(100, 100).consumed);
  CHECK(panel.other_button(cx(panel.button_rect()), cy(panel.button_rect())).consumed);

  tap(panel, panel.button_rect());
  REQUIRE(panel.is_open());
  // Inside the panel between icons: swallowed, the panel stays open.
  const PanelRect p = panel.panel_rect();
  const sh::PanelInput in = panel.press(p.x + 2, p.y + 2);
  CHECK(in.consumed);
  CHECK(panel.release(p.x + 2, p.y + 2).consumed);
  CHECK(panel.is_open());
  // Outside: the press closes the panel and, with its motion and release,
  // never reaches the game.
  const sh::PanelInput out = panel.press(100, 100);
  CHECK(out.consumed);
  CHECK(out.redraw);
  CHECK_FALSE(panel.is_open());
  CHECK(panel.move(140, 100).consumed);
  const sh::PanelInput out_up = panel.release(140, 100);
  CHECK(out_up.consumed);
  CHECK_FALSE(out_up.command);
  // The next press goes to the game again.
  CHECK_FALSE(panel.press(100, 100).consumed);
  panel.release(100, 100);
}

TEST_CASE("command panel: an icon gives its command on release over it", "[cmdpanel]") {
  CommandPanel panel = fold_panel();
  tap(panel, panel.button_rect());
  const auto icons = panel.icon_rects();
  // Touch targets of at least 48 dp.
  for (const PanelRect& r : icons) {
    CHECK(r.w >= 48 * 2.625f);
    CHECK(r.h >= 48 * 2.625f);
  }
  const sh::PanelInput up = tap(panel, icons[static_cast<std::size_t>(PanelCommand::kCrashDive)]);
  REQUIRE(up.command);
  CHECK(*up.command == PanelCommand::kCrashDive);
  CHECK(panel.is_open());
  // Pressed, then slid off the icon: no command.
  const PanelRect t3 = icons[static_cast<std::size_t>(PanelCommand::kFireTube3)];
  panel.press(cx(t3), cy(t3));
  CHECK(panel.pressed() == PanelCommand::kFireTube3);
  CHECK(panel.move(cx(t3) + t3.w, cy(t3)).redraw);
  CHECK_FALSE(panel.pressed());
  CHECK_FALSE(panel.release(cx(t3) + t3.w, cy(t3)).command);
  // Tube 10 is the last of the bottom row.
  CHECK(tap(panel, icons[static_cast<std::size_t>(PanelCommand::kFireTube10)]).command ==
        PanelCommand::kFireTube10);
}

TEST_CASE("command panel: it fits the Pixel Fold's screens beside the button", "[cmdpanel]") {
  // The cover screen (landscape, ~21:9) and the inner one (~6:5).
  for (const auto& [w, h] : {std::pair{2520.0f, 1080.0f}, std::pair{2208.0f, 1840.0f}}) {
    CommandPanel panel;
    panel.set_window(w, h, 2.625f);
    tap(panel, panel.button_rect());
    const PanelRect p = panel.panel_rect();
    const PanelRect b = panel.button_rect();
    CHECK(p.x >= 0);
    CHECK(p.y >= 0);
    CHECK(p.x + p.w <= w);
    CHECK(p.y + p.h <= h);
    // Left of the button, not over it.
    CHECK(p.x + p.w <= b.x);
    CHECK(panel.icon_rects()[0].w == 56 * 2.625f);
  }
  // A window too small for 56 dp icons shrinks the panel to fit.
  CommandPanel small;
  small.set_window(640, 400, 2.625f);
  tap(small, small.button_rect());
  const PanelRect p = small.panel_rect();
  CHECK(p.h <= 400);
  CHECK(p.w <= 640);
}

TEST_CASE("command panel: each icon types the original hotkey", "[cmdpanel]") {
  // The words of the command bar's hotkey table (game/command_bar.cpp).
  const auto table_key = [](sh::HotkeyAction action) {
    for (const sh::Hotkey& h : sh::hotkey_table()) {
      if (h.action == action) {
        return std::uint32_t{h.key};
      }
    }
    FAIL("no hotkey");
    return std::uint32_t{0};
  };
  constexpr std::size_t kCommands = sh::kPanelCommandCount + 2;
  const std::array<sh::HotkeyAction, kCommands> actions{
      sh::HotkeyAction::kPeriscopeDepth, sh::HotkeyAction::kRadarDepth, sh::HotkeyAction::kSurface,
      sh::HotkeyAction::kCrashDive,      sh::HotkeyAction::kBlowBallast, sh::HotkeyAction::kFireTube1,
      sh::HotkeyAction::kFireTube2,      sh::HotkeyAction::kFireTube3,   sh::HotkeyAction::kFireTube4,
      sh::HotkeyAction::kFireTube5,      sh::HotkeyAction::kFireTube6,   sh::HotkeyAction::kFireTube7,
      sh::HotkeyAction::kFireTube8,      sh::HotkeyAction::kFireTube9,   sh::HotkeyAction::kFireTube10,
      sh::HotkeyAction::kCompressionUp,  sh::HotkeyAction::kCompressionDown};
  // The BIOS words typed: lower-case letters, Alt-digits, '+' and '-'.
  const std::array<std::uint16_t, kCommands> words{0x1970, 0x1372, 0x1f73, 0x2e63, 0x3062, 0x7800,
                                                   0x7900, 0x7a00, 0x7b00, 0x7c00, 0x7d00, 0x7e00,
                                                   0x7f00, 0x8000, 0x8100, 0x0d2b, 0x0c2d};
  for (std::size_t i = 0; i < kCommands; ++i) {
    const sh::PanelKey k = sh::panel_command_key(static_cast<PanelCommand>(i));
    // As the display delivers it: a synthesized key down (no scancode).
    sh::InputEvent e;
    e.type = sh::EventType::kKeyDown;
    e.key = k.key;
    e.mods = k.mods;
    const std::optional<std::uint16_t> word = sh::bios_key_word(e);
    REQUIRE(word);
    CHECK(*word == words[i]);
    CHECK(sh::ui::hotkey_matches(table_key(actions[i]), *word));
  }
  CHECK(sh::panel_command_key(PanelCommand::kFireTube10).key == SDLK_0);
  CHECK(sh::panel_command_key(PanelCommand::kFireTube1).mods == SDL_KMOD_LALT);
}

TEST_CASE("command panel: the button's place is saved and restored", "[cmdpanel]") {
  const std::filesystem::path file = temp_file("CMDPANEL.CFG");
  {
    CommandPanel panel = fold_panel(file);
    const PanelRect b = panel.button_rect();
    panel.press(cx(b), cy(b));
    panel.move(400, 300);
    const sh::PanelInput up = panel.release(400, 300);
    CHECK_FALSE(up.save_failed);
    // A tap does not rewrite it.
    std::filesystem::remove(file);
    tap(panel, panel.button_rect());
    CHECK_FALSE(std::filesystem::exists(file));
    panel.press(400, 300);
    panel.move(420, 330);
    panel.release(420, 330);
    REQUIRE(std::filesystem::exists(file));
  }
  // Restored at the same relative place, on a screen of another shape.
  CommandPanel restored(file);
  restored.set_window(2208, 1840, 2.625f);
  CHECK(cx(restored.button_rect()) == Catch::Approx(420.0).margin(0.5));
  CHECK(cy(restored.button_rect()) == Catch::Approx(330.0).margin(0.5));
  restored.set_window(2520, 1080, 2.625f);
  CHECK(cx(restored.button_rect()) / 2520 == Catch::Approx(420.0 / 2208).margin(0.001));

  // A foreign or damaged file leaves the default place.
  {
    std::ofstream out(file, std::ios::trunc);
    out << "SH-COMMAND-BUTTON 1\n7 nonsense\n";
  }
  CommandPanel fallback(file);
  CommandPanel defaults;
  fallback.set_window(2208, 1840, 2.625f);
  defaults.set_window(2208, 1840, 2.625f);
  CHECK(fallback.button_rect().x == defaults.button_rect().x);
  CHECK(fallback.button_rect().y == defaults.button_rect().y);
}

TEST_CASE("command icons: the tube buttons are TBTTDC.SHP's, every design is drawn", "[cmdpanel]") {
  SH_REQUIRE_GAME_DATA(install);
  const sh::ShPaths paths = sh::ShPaths::load(install);
  const sh::CommandIconArt art(install, paths);
  const sh::CommandIcons icons = sh::build_command_icons(art);
  // The 32x32 fire button of tube 1 centred on a 36x36 canvas: its red
  // middle opaque, the corners transparent.
  const sh::RgbaImage& t1 = icons.icons[static_cast<std::size_t>(PanelCommand::kFireTube1)];
  CHECK(t1.width == 36);
  CHECK((t1.at(0, 0) >> 24) == 0);
  CHECK((t1.at(18, 18) >> 24) == 255);
  // Ready buttons are bright red, fired (pressed) ones dark red.
  const auto red = [](std::uint32_t c) { return static_cast<int>((c >> 16) & 0xff); };
  CHECK(red(t1.at(10, 12)) > red(icons.pressed[static_cast<std::size_t>(PanelCommand::kFireTube1)].at(10, 12)));
  for (std::size_t s = 0; s < sh::kDepthIconStyleCount; ++s) {
    for (std::size_t c = 0; c < sh::kDepthCommandCount; ++c) {
      const sh::RgbaImage icon = art.depth_icon(static_cast<PanelCommand>(c), static_cast<sh::DepthIconStyle>(s));
      CHECK(icon.width > 0);
      CHECK(icon.height == icon.width);
    }
  }
  for (std::size_t b = 0; b < sh::kButtonIconStyleCount; ++b) {
    const sh::RgbaImage button = art.button_icon(static_cast<sh::ButtonIconStyle>(b));
    CHECK(button.width > 0);
    CHECK(button.height > 0);
  }
}

TEST_CASE("command panel: the display swallows its taps and types its keys", "[cmdpanel]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  sh::Display display("test", 2);
  CommandPanel panel;
  sh::CommandIcons icons;
  icons.button = sh::RgbaImage(4, 4, 0xff808080u);
  icons.compression[0] = icons.compression[1] = sh::RgbaImage(4, 4, 0xff808080u);
  icons.compression_pressed = icons.compression;
  for (std::size_t i = 0; i < sh::kPanelCommandCount; ++i) {
    icons.icons[i] = icons.pressed[i] = sh::RgbaImage(4, 4, 0xffff0000u);
  }
  display.set_command_panel(panel, icons);
  // The dummy window: 1280x960 pixels at scale 1.
  panel.set_window(1280, 960, 1);
  // The window's own events (its focus).
  while (display.poll()) {
  }
  const auto click = [](float x, float y) {
    for (const Uint32 type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
      SDL_Event e{};
      e.type = type;
      e.button.button = SDL_BUTTON_LEFT;
      e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      e.button.x = x;
      e.button.y = y;
      REQUIRE(SDL_PushEvent(&e));
    }
  };
  const PanelRect b = panel.button_rect();
  click(cx(b), cy(b));
  CHECK_FALSE(display.poll());
  REQUIRE(panel.is_open());
  const PanelRect t1 = panel.icon_rects()[static_cast<std::size_t>(PanelCommand::kFireTube1)];
  click(cx(t1), cy(t1));
  const std::optional<sh::InputEvent> down = display.poll();
  REQUIRE(down);
  CHECK(down->type == sh::EventType::kKeyDown);
  CHECK(sh::bios_key_word(*down) == std::uint16_t{0x7800});
  const std::optional<sh::InputEvent> up = display.poll();
  REQUIRE(up);
  CHECK(up->type == sh::EventType::kKeyUp);
  CHECK(up->mods == 0);
  CHECK_FALSE(display.poll());
  // "-" under ORDERS: typed as the US keyboard's '-'.
  const PanelRect minus = panel.compression_rects()[1];
  click(cx(minus), cy(minus));
  const std::optional<sh::InputEvent> slower = display.poll();
  REQUIRE(slower);
  CHECK(sh::bios_key_word(*slower) == std::uint16_t{0x0c2d});
  while (display.poll()) {
  }
  // Outside: closes the panel, the game gets nothing; the next tap is the
  // game's.
  click(10, 10);
  CHECK_FALSE(display.poll());
  CHECK_FALSE(panel.is_open());
  click(10, 10);
  const std::optional<sh::InputEvent> game = display.poll();
  REQUIRE(game);
  CHECK(game->type == sh::EventType::kMouseDown);
  while (display.poll()) {
  }
  // Hidden (no mission in play): it closes, and its taps reach the game.
  click(cx(b), cy(b));
  CHECK_FALSE(display.poll());
  REQUIRE(panel.is_open());
  display.show_command_panel(false);
  CHECK_FALSE(panel.is_open());
  click(cx(b), cy(b));
  const std::optional<sh::InputEvent> through = display.poll();
  REQUIRE(through);
  CHECK(through->type == sh::EventType::kMouseDown);
  CHECK_FALSE(panel.is_open());
}

TEST_CASE("command panel: a place saved at one window size is kept at another", "[cmdpanel]") {
  // Saved on the unfolded Pixel Fold, restored folded (and back): at the
  // same fractions of the window, inside it.
  const std::filesystem::path file = temp_file("resize.cfg");
  {
    CommandPanel unfolded(file);
    unfolded.set_window(2208, 1840, 2.625f);
    const PanelRect b = unfolded.button_rect();
    unfolded.press(cx(b), cy(b));
    unfolded.move(1100, 700);
    unfolded.release(1100, 700);
  }
  CommandPanel folded(file);
  folded.set_window(2092, 1080, 2.625f);
  const PanelRect g = folded.group_rect();
  CHECK(cx(g) / 2092 == Catch::Approx(folded.rel_x()));
  CHECK(cy(g) / 1080 == Catch::Approx(folded.rel_y()));
  CHECK(g.x >= 0);
  CHECK(g.y >= 0);
  CHECK(g.x + g.w <= 2092);
  CHECK(g.y + g.h <= 1080);
  // Its size follows the window's density, without a touch.
  CHECK(g.w == CommandPanel::kGroupWidthDp * 2.625f);
  folded.set_window(1280, 800, 1.5f);
  CHECK(folded.group_rect().w == CommandPanel::kGroupWidthDp * 1.5f);

  // Saved in a corner of the big window: inside the small one too.
  {
    CommandPanel big(file);
    big.set_window(2208, 1840, 2.625f);
    const PanelRect b = big.button_rect();
    big.press(cx(b), cy(b));
    big.move(2200, 1835);
    big.release(2200, 1835);
  }
  CommandPanel small(file);
  small.set_window(1080, 600, 2.625f);
  const PanelRect s2 = small.group_rect();
  CHECK(s2.x + s2.w <= 1080);
  CHECK(s2.y + s2.h <= 600);
  CHECK(s2.x >= 0);
  CHECK(s2.y >= 0);
}

TEST_CASE("command panel: the display gives it the window's size before any touch", "[cmdpanel]") {
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  sh::Display display("test", 2);
  CommandPanel panel;
  sh::CommandIcons icons;
  icons.button = sh::RgbaImage(4, 4, 0xff808080u);
  icons.compression[0] = icons.compression[1] = sh::RgbaImage(4, 4, 0xff808080u);
  icons.compression_pressed = icons.compression;
  for (std::size_t i = 0; i < sh::kPanelCommandCount; ++i) {
    icons.icons[i] = icons.pressed[i] = sh::RgbaImage(4, 4, 0xffff0000u);
  }
  // A panel shown first with its default 640x480 was drawn small and in
  // the wrong place until the first touch.
  display.set_command_panel(panel, icons);
  while (display.poll()) {
  }
  const sh::Image8 frame(sh::kScreenWidth, sh::kScreenHeight, 0);
  const sh::Palette palette{};
  display.present(frame, palette);
  // The default place, by the right edge of the 1280x960 window.
  const float edge = CommandPanel::kGroupWidthDp / 2 + CommandPanel::kMarginDp;
  CHECK(cx(panel.group_rect()) == 1280 - edge);
  CHECK(cy(panel.group_rect()) == Catch::Approx(panel.rel_y() * 960));
  // Resized (a fold, a rotation): the next frame follows.
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  REQUIRE(count == 1);
  REQUIRE(SDL_SetWindowSize(windows[0], 900, 500));
  SDL_SyncWindow(windows[0]);
  SDL_free(windows);
  while (display.poll()) {
  }
  display.present(frame, palette);
  const PanelRect g = panel.group_rect();
  CHECK(cx(g) == 900 - edge);
  CHECK(cy(g) == Catch::Approx(panel.rel_y() * 500));
}

TEST_CASE("command panel: shown only while the mission is in play", "[cmdpanel]") {
  SH_REQUIRE_GAME_DATA(install);
  sh::test::ControlRig rig(install);
  rig.frames(1);
  // The opening fade holds the game.
  CHECK_FALSE(rig.screen->in_play());
  rig.settle();
  rig.frames(2);
  CHECK(rig.screen->in_play());
  // Esc: the END MISSION? menu.
  rig.key(0x011b);
  CHECK_FALSE(rig.screen->in_play());
  rig.key(0x011b);
  rig.frames(2);
  CHECK(rig.screen->in_play());
  // The time compression box (the bar button left of 1MC).
  rig.click(530, 470);
  rig.frames(2);
  CHECK_FALSE(rig.screen->in_play());
  rig.key(0x011b);
  rig.frames(2);
  CHECK(rig.screen->in_play());
  // Alt-O: the GAME OPTIONS menu.
  rig.key(0x1800);
  CHECK_FALSE(rig.screen->in_play());
  rig.key(0x011b);
  rig.frames(2);
  CHECK(rig.screen->in_play());
  // A pause held by a station.
  ++rig.screen->ui_state().pause;
  CHECK_FALSE(rig.screen->in_play());
  --rig.screen->ui_state().pause;
  CHECK(rig.screen->in_play());
}

namespace {

// Writes `sheet` as a PAM file at `path`.
void write_pam(const char* path, const sh::RgbaImage& sheet) {
  std::ofstream out(path, std::ios::binary);
  out << "P7\nWIDTH " << sheet.width << "\nHEIGHT " << sheet.height
      << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
  for (const std::uint32_t p : sheet.pixels) {
    const char px[4] = {static_cast<char>((p >> 16) & 0xff), static_cast<char>((p >> 8) & 0xff),
                        static_cast<char>(p & 0xff), static_cast<char>((p >> 24) & 0xff)};
    out.write(px, 4);
  }
  CHECK(out.good());
}

}  // namespace

// Writes the contact sheets of the icon designs to $SH_ICON_SHEET and of
// the floating button's to $SH_BUTTON_SHEET (PAM files); run explicitly.
TEST_CASE("command icons: contact sheet", "[.cmdpanel_sheet]") {
  SH_REQUIRE_GAME_DATA(install);
  const char* icons = std::getenv("SH_ICON_SHEET");
  const char* buttons = std::getenv("SH_BUTTON_SHEET");
  REQUIRE(icons != nullptr);
  REQUIRE(buttons != nullptr);
  const sh::CommandIconArt art(install, sh::ShPaths::load(install));
  write_pam(icons, sh::command_icon_sheet(art, 1));
  write_pam(buttons, sh::command_button_sheet(art));
}

// Renders the panel open over the 640x480 BMP $SH_PANEL_FRAME, letterboxed
// in windows the size of the Pixel Fold's cover (2520x1080) and inner
// (2208x1840) screens at its 2.625 scale, to $SH_PANEL_OUT-<w>x<h>.bmp;
// run explicitly.
TEST_CASE("command panel: window screenshots", "[.cmdpanel_window]") {
  SH_REQUIRE_GAME_DATA(install);
  const char* frame_path = std::getenv("SH_PANEL_FRAME");
  const char* out_prefix = std::getenv("SH_PANEL_OUT");
  REQUIRE(frame_path != nullptr);
  REQUIRE(out_prefix != nullptr);
  const sh::CommandIcons icons =
      sh::build_command_icons(sh::CommandIconArt(install, sh::ShPaths::load(install)));
  SDL_Surface* frame = SDL_LoadBMP(frame_path);
  REQUIRE(frame != nullptr);
  for (const auto& [w, h] : {std::pair{2520, 1080}, std::pair{2208, 1840}}) {
    SDL_Surface* window = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer(window);
    REQUIRE(renderer != nullptr);
    SDL_SetRenderLogicalPresentation(renderer, sh::kScreenWidth, sh::kScreenHeight,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_Texture* picture = SDL_CreateTextureFromSurface(renderer, frame);
    SDL_SetTextureScaleMode(picture, SDL_SCALEMODE_LINEAR);
    {
      CommandPanel panel;
      panel.set_window(static_cast<float>(w), static_cast<float>(h), 2.625f);
      tap(panel, panel.button_rect());
      // Tube 3 held down.
      const PanelRect t3 = panel.icon_rects()[static_cast<std::size_t>(PanelCommand::kFireTube3)];
      panel.press(cx(t3), cy(t3));
      sh::CommandPanelView view(renderer, icons);
      SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
      SDL_RenderClear(renderer);
      SDL_RenderTexture(renderer, picture, nullptr, nullptr);
      view.draw(panel);
      SDL_RenderPresent(renderer);
    }
    const std::string out = std::string(out_prefix) + "-" + std::to_string(w) + "x" + std::to_string(h) + ".bmp";
    CHECK(SDL_SaveBMP(window, out.c_str()));
    SDL_DestroyTexture(picture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroySurface(window);
  }
  SDL_DestroySurface(frame);
}
