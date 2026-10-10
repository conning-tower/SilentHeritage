// Tests of the dialog toolkit: BIOS key words, hotkeys, the dialog state
// machine driven by synthetic events and an injected clock, the controls'
// keyboard behaviour, the default and MENU skins on the real GUI files, and
// the mouse cursor.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "game_data.h"
#include "gfx/sprite.h"
#include "gfx/text.h"
#include "platform/bios_keys.h"
#include "ui/controls.h"
#include "ui/default_skin.h"
#include "ui/draw.h"
#include "ui/menu_skin.h"

namespace sh::ui {
namespace {

// A GUI on a black screen with an injected clock. The skin has empty fonts
// unless real resources are given, which the state machine does not need.
struct Harness {
  Image8 screen{kScreenWidth, kScreenHeight, 0};
  std::uint32_t tick = 0;
  GuiResources resources{};
  DefaultSkin skin{resources};
  Gui gui{screen, [this] { return tick; }, skin};

  // Presses and releases the left button at (x, y).
  void click(int x, int y) {
    gui.mouse().update(x, y, kButtonLeft, 0);
    gui.mouse().update(x, y, 0, 0);
  }
  void key(std::uint16_t word) { gui.mouse().post_key(word, 0); }
  // Runs dialog iterations until a result or `max` iterations.
  int steps(Dialog& d, int max = 8) {
    for (int i = 0; i < max; ++i) {
      if (const int r = d.run_step(); r != 0) {
        return r;
      }
    }
    return 0;
  }
};

}  // namespace

TEST_CASE("BIOS key words of a US keyboard", "[keys]") {
  CHECK(bios_key_word(SDLK_ESCAPE, 0) == 0x011b);
  CHECK(bios_key_word(SDLK_RETURN, 0) == 0x1c0d);
  CHECK(bios_key_word(SDLK_TAB, 0) == 0x0f09);
  CHECK(bios_key_word(SDLK_TAB, SDL_KMOD_LSHIFT) == 0x0f00);
  CHECK(bios_key_word(SDLK_SPACE, 0) == 0x3920);
  CHECK(bios_key_word(SDLK_BACKSPACE, 0) == 0x0e08);
  // Gray arrows carry ASCII 0xe0; the keypad ones (Num Lock off) do not.
  CHECK(bios_key_word(SDLK_UP, 0) == 0x48e0);
  CHECK(bios_key_word(SDLK_KP_8, 0) == 0x4800);
  CHECK(bios_key_word(SDLK_KP_2, 0) == 0x5000);
  CHECK(bios_key_word(SDLK_KP_4, 0) == 0x4b00);
  CHECK(bios_key_word(SDLK_KP_6, 0) == 0x4d00);
  CHECK(bios_key_word(SDLK_KP_5, 0) == 0x4cf0);
  CHECK(bios_key_word(SDLK_KP_8, SDL_KMOD_NUM) == 0x4838);
  CHECK(bios_key_word(SDLK_KP_8, SDL_KMOD_NUM | SDL_KMOD_LSHIFT) == 0x4800);
  CHECK(bios_key_word(SDLK_KP_ENTER, 0) == 0xe00d);
  CHECK(bios_key_word(SDLK_KP_PLUS, 0) == 0x4e2b);
  CHECK(bios_key_word(SDLK_F1, 0) == 0x3b00);
  CHECK(bios_key_word(SDLK_F10, 0) == 0x4400);
  CHECK(bios_key_word(SDLK_F4, SDL_KMOD_LALT) == 0x6b00);
  CHECK(bios_key_word(SDLK_F1, SDL_KMOD_LSHIFT) == 0x5400);
  CHECK(bios_key_word(SDLK_F1, SDL_KMOD_LCTRL) == 0x5e00);
  CHECK(bios_key_word(SDLK_B, 0) == 0x3062);
  CHECK(bios_key_word(SDLK_B, SDL_KMOD_RSHIFT) == 0x3042);
  CHECK(bios_key_word(SDLK_B, SDL_KMOD_CAPS) == 0x3042);
  CHECK(bios_key_word(SDLK_B, SDL_KMOD_CAPS | SDL_KMOD_LSHIFT) == 0x3062);
  CHECK(bios_key_word(SDLK_A, SDL_KMOD_LCTRL) == 0x1e01);
  CHECK(bios_key_word(SDLK_P, SDL_KMOD_LALT) == 0x1900);
  CHECK(bios_key_word(SDLK_1, SDL_KMOD_LALT) == 0x7800);
  CHECK(bios_key_word(SDLK_0, SDL_KMOD_RALT) == 0x8100);
  CHECK(bios_key_word(SDLK_0, 0) == 0x0b30);
  CHECK(bios_key_word(SDLK_EQUALS, SDL_KMOD_LSHIFT) == 0x0d2b);
  CHECK(bios_key_word(SDLK_1, SDL_KMOD_LCTRL) == std::nullopt);
  CHECK(bios_key_word(SDLK_LSHIFT, SDL_KMOD_LSHIFT) == std::nullopt);
  CHECK(bios_shift_state(SDL_KMOD_LSHIFT | SDL_KMOD_LCTRL | SDL_KMOD_NUM) == 0x26);
  CHECK(bios_shift_state(SDL_KMOD_RSHIFT | SDL_KMOD_RALT | SDL_KMOD_CAPS) == 0x49);

  InputEvent up;
  up.type = EventType::kKeyUp;
  up.key = SDLK_ESCAPE;
  CHECK(bios_key_word(up) == std::nullopt);
}

TEST_CASE("The in-game hotkeys are reachable from the keyboard", "[keys]") {
  SH_REQUIRE_GAME_EXE();
  // A sample of the control bar hotkeys:
  // after `input_timer_isr` drops the 0xe0 byte, the BIOS word of the key
  // must match the hotkey record.
  struct Case {
    std::uint32_t hotkey;
    std::uint32_t key;
    std::uint16_t mods;
  };
  const std::vector<Case> cases = {
      {0x3b00, SDLK_F1, 0},          {0x4400, SDLK_F10, 0},        {0x6b00, SDLK_F4, SDL_KMOD_LALT},
      {0x7000, SDLK_F9, SDL_KMOD_LALT}, {0x7100, SDLK_F10, SDL_KMOD_LALT}, {0xfe2b, SDLK_EQUALS, SDL_KMOD_LSHIFT},
      {0xfe2b, SDLK_KP_PLUS, 0},     {0xfe2d, SDLK_MINUS, 0},      {0xfe2d, SDLK_KP_MINUS, 0},
      {0xff0d, SDLK_RETURN, 0},      {0xff0d, SDLK_KP_ENTER, 0},   {0x0b30, SDLK_0, 0},
      {0x0231, SDLK_1, 0},           {0x0a39, SDLK_9, 0},          {0x4d00, SDLK_RIGHT, 0},
      {0x4b00, SDLK_KP_4, 0},        {0x4800, SDLK_UP, 0},         {0x5000, SDLK_DOWN, 0},
      {0x4cf0, SDLK_KP_5, 0},        {0xff42, SDLK_B, 0},          {0xff53, SDLK_S, SDL_KMOD_LSHIFT},
      {0xff43, SDLK_C, 0},           {0x1900, SDLK_P, SDL_KMOD_LALT}, {0x1f00, SDLK_S, SDL_KMOD_LALT},
      {0x1e00, SDLK_A, SDL_KMOD_LALT}, {0x2200, SDLK_G, SDL_KMOD_LALT}, {0x1400, SDLK_T, SDL_KMOD_LALT},
      {0x7800, SDLK_1, SDL_KMOD_LALT}, {0x8100, SDLK_0, SDL_KMOD_LALT}, {0x0e08, SDLK_BACKSPACE, 0},
      {0x0f09, SDLK_TAB, 0},         {0xff4d, SDLK_M, 0},          {0x1800, SDLK_O, SDL_KMOD_LALT},
      {0x2000, SDLK_D, SDL_KMOD_LALT}, {0x1000, SDLK_Q, SDL_KMOD_LALT}, {0x2500, SDLK_K, SDL_KMOD_LALT},
  };
  for (const Case& c : cases) {
    Harness h;
    const std::optional<std::uint16_t> word = bios_key_word(c.key, c.mods);
    REQUIRE(word);
    h.gui.mouse().post_key(*word, bios_shift_state(c.mods));
    const std::uint16_t delivered = h.gui.events().get()->key;
    INFO("hotkey " << c.hotkey << " word " << *word);
    CHECK(hotkey_matches(c.hotkey, delivered));
  }
}

TEST_CASE("Hotkey matching rules", "[ui]") {
  // 0xffXX: the character, case-insensitively, any scan code.
  CHECK(hotkey_matches(0xff42, 0x3062));
  CHECK(hotkey_matches(0xff42, 0x3042));
  CHECK_FALSE(hotkey_matches(0xff42, 0x2e63));
  CHECK(hotkey_matches(0xff0d, 0x1c0d));
  // 0xfeXX: the low byte exactly.
  CHECK(hotkey_matches(0xfe2b, 0x0d2b));
  CHECK(hotkey_matches(0xfe2b, 0x4e2b));
  CHECK_FALSE(hotkey_matches(0xfe2b, 0x0d0b));
  // Anything else: the exact word.
  CHECK(hotkey_matches(0x3b00, 0x3b00));
  CHECK_FALSE(hotkey_matches(0x3b00, 0x5400));
  CHECK_FALSE(hotkey_matches(0x0231, 0x0221));
}

TEST_CASE("Hotspot table: disable counts, hit testing", "[ui]") {
  HotspotTable t;
  Hotspot a;
  a.x0 = 0;
  a.y0 = 0;
  a.x1 = 99;
  a.y1 = 99;
  Hotspot b = a;
  b.x0 = 50;
  t.add(a);
  t.add(b);
  t.add(Hotspot{});  // key-only
  CHECK(t.hit_test(60, 10) == 1);  // the topmost
  CHECK(t.hit_test(10, 10) == 0);
  t.disable(1);
  t.disable(1);
  CHECK(t[1].flags == 4);
  CHECK(t.hit_test(60, 10) == 0);
  t.enable(1);
  CHECK_FALSE(t[1].enabled());
  t.enable(1);
  CHECK(t[1].enabled());
  CHECK(t.hit_test(150, 150) == -1);
  for (int i = 0; i < 200; ++i) {
    t.add(Hotspot{});
  }
  CHECK(t.size() == HotspotTable::kMaxHotspots);
}

TEST_CASE("Event queue and mouse events", "[ui]") {
  std::uint32_t tick = 100;
  EventQueue q([&] { return tick; });
  Mouse m(q);
  m.update(10, 20, kButtonLeft, 0);
  m.update(10, 20, 0, 0);
  m.update(700, -5, kButtonRight, 0);
  m.post_key(0x48e0, kShiftNumLock);
  REQUIRE(q.size() == 4);
  Event e = *q.get();
  CHECK(e.flags == (kEventMoved | kEventLeftDown));
  CHECK(e.buttons == kButtonLeft);
  CHECK(e.tick == bios_ticks(100));
  CHECK(q.get()->flags == kEventLeftUp);
  e = *q.get();
  CHECK(e.flags == (kEventMoved | kEventRightDown));
  CHECK(e.x == 639);
  CHECK(e.y == 0);
  e = *q.get();
  CHECK(e.flags == kEventKey);
  CHECK(e.key == 0x4800);
  CHECK(e.buttons == kKeyButtonRight);
  CHECK(e.shift == kShiftNumLock);
  for (std::size_t i = 0; i < EventQueue::kCapacity + 3; ++i) {
    q.post(Event{});
  }
  CHECK(q.size() == EventQueue::kCapacity);
  CHECK(q.lost() == 3);
  q.flush();
  CHECK_FALSE(q.peek());
}

TEST_CASE("Dialog: clicks, Esc, Tab order, Enter, hotkeys", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  Dialog d(h.gui, 100, 100, 300, 200, kDialogFramed | kDialogCancel);
  d.emplace<Button>(10, 10, 80, "OK", [](int) { return 7; });
  d.emplace<Button>(10, 50, 80, "Other", [](int) { return 42; }, 0xff43);
  d.emplace<Label>(10, 90, "label");
  d.emplace<Button>(10, 130, 80, "Third", [](int) { return 9; });
  REQUIRE(d.run_begin() == kResultContinue);
  CHECK(d.hotspot_count() == 3);
  CHECK(d.focus() == 0);

  SECTION("a click returns on_click") {
    h.click(150, 120);
    CHECK(h.steps(d) == 7);
    CHECK(h.gui.hotspots().size() == 0);
  }
  SECTION("a release outside the button does not activate it") {
    h.gui.mouse().update(150, 120, kButtonLeft, 0);
    h.gui.mouse().update(300, 290, 0, 0);
    CHECK(h.steps(d) == 0);
  }
  SECTION("Esc cancels") {
    h.key(kKeyEsc);
    CHECK(h.steps(d) == kResultCancel);
  }
  SECTION("a right click cancels") {
    h.gui.mouse().update(5, 5, kButtonRight, 0);
    h.gui.mouse().update(5, 5, 0, 0);
    CHECK(h.steps(d) == kResultCancel);
  }
  SECTION("Tab and Shift-Tab cycle the focus") {
    h.key(kKeyTab);
    CHECK(h.steps(d, 1) == 0);
    CHECK(d.focus() == 1);
    h.key(kKeyTab);
    h.key(kKeyTab);
    h.steps(d, 2);
    CHECK(d.focus() == 0);
    h.key(kKeyShiftTab);
    h.steps(d, 1);
    CHECK(d.focus() == 2);
    SECTION("Enter activates the focused button") {
      h.key(kKeyEnter);
      CHECK(h.steps(d) == 9);
    }
  }
  SECTION("Space activates the focused button") {
    h.key(kKeySpace);
    CHECK(h.steps(d) == 7);
  }
  SECTION("a hotkey focuses and activates its button") {
    h.key(0x2e63);  // 'c'
    CHECK(h.steps(d) == 42);
  }
}

TEST_CASE("Dialog without hotspots returns -3", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  Dialog d(h.gui, 0, 0, 100, 100, 0);
  d.emplace<Label>(0, 0, "x");
  CHECK(d.run_begin() == kResultNoHotspots);
  CHECK(d.run() == kResultNoHotspots);
}

TEST_CASE("Dialog restores the screen under it", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  h.screen.fill_rect(0, 0, 640, 480, 0x33);
  Dialog d(h.gui, 20, 30, 100, 50, kDialogFramed | kDialogCancel);
  d.emplace<Button>(10, 10, 60, "OK", return_accept);
  d.run_begin();
  h.steps(d, 1);
  CHECK(h.screen.at(20, 30) != 0x33);  // the frame was copied to the screen
  h.key(kKeyEsc);
  CHECK(h.steps(d) == kResultCancel);
  CHECK(h.screen.at(20, 30) == 0x33);
  CHECK(h.screen.at(119, 79) == 0x33);
}

TEST_CASE("Button auto-repeat follows the GUI ticks", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  std::vector<int> clicks;
  Dialog d(h.gui, 0, 0, 200, 200, 0);
  d.emplace<Button>(10, 10, 80, "Rep", [&](int n) {
    clicks.push_back(n);
    return 0;
  }, 0, 10);
  d.run_begin();
  h.gui.mouse().update(20, 20, kButtonLeft, 0);
  h.steps(d, 1);
  CHECK(d.repeat_interval() == 10);
  h.tick = 9;
  h.steps(d, 1);
  CHECK(clicks.empty());
  h.tick = 10;
  h.steps(d, 1);
  CHECK(clicks == std::vector<int>{1});
  h.tick = 19;
  h.steps(d, 1);
  h.tick = 20;
  h.steps(d, 1);
  CHECK(clicks == std::vector<int>{1, 2});
  h.gui.mouse().update(20, 20, 0, 0);
  h.steps(d, 1);
  CHECK(clicks == std::vector<int>{1, 2, 3});
  // The release reset the count: the next click is 1 again.
  h.click(20, 20);
  h.steps(d, 2);
  CHECK(clicks.back() == 1);
}

TEST_CASE("List: selection by click, double click, keys", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  std::vector<int> selected;
  std::vector<int> activated;
  Dialog d(h.gui, 50, 40, 400, 300, 0);
  List& list = d.emplace<List>(10, 10, 200, [] { return 20; }, [](int i) { return "item " + std::to_string(i); });
  list.on_select = [&](int i) { selected.push_back(i); };
  list.on_activate = [&](int i) { activated.push_back(i); };
  d.run_begin();
  h.steps(d, 1);
  // Rows start at the list y + 1 (screen 51), 16 pixels each.
  const int row2_y = 51 + 2 * 16 + 5;

  SECTION("double click") {
    h.click(80, row2_y);
    h.steps(d, 2);
    CHECK(list.selection == 2);
    CHECK(activated.empty());
    h.tick = 30;
    h.click(80, row2_y);
    h.steps(d, 2);
    CHECK(activated == std::vector<int>{2});
    // A slow second click is not a double click.
    h.tick = 200;
    h.click(80, row2_y);
    h.steps(d, 2);
    CHECK(activated.size() == 1);
  }
  SECTION("Up / Down keys move the selection and scroll") {
    for (int i = 0; i < 10; ++i) {
      h.key(kKeyDown);
      h.steps(d, 2);
    }
    CHECK(list.selection == 9);
    CHECK(list.top == 1);
    h.key(kKeyUp);
    h.steps(d, 2);
    CHECK(list.selection == 8);
    CHECK(list.top == 1);
    h.key(kKeyEnter);
    h.steps(d, 2);
    CHECK(activated == std::vector<int>{8});
  }
  SECTION("the arrows scroll and stop at the ends") {
    // The down arrow (default skin rect: x + w + 6.., y + 93 + 34..).
    const int ax = 50 + 10 + 200 + 10;
    const int ay = 40 + 10 + 93 + 40;
    for (int i = 0; i < 15; ++i) {
      h.click(ax, ay);
      h.steps(d, 3);
    }
    CHECK(list.top == 11);  // 20 items, 9 rows
  }
}

TEST_CASE("Slider keyboard behaviour", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  int value = 5;
  std::vector<int> changes;
  Dialog d(h.gui, 0, 0, 300, 100, 0);
  d.emplace<Slider>(30, 30, 100, 7, value, [&](int& v) { changes.push_back(v); });
  d.run_begin();
  h.steps(d, 1);
  h.key(kKeyRight);
  h.steps(d, 2);
  CHECK(value == 6);
  for (int i = 0; i < 3; ++i) {
    h.key(kKeyRight);
    h.steps(d, 2);
  }
  CHECK(value == 7);  // clamped to max
  h.key(kKeyLeft);
  h.steps(d, 2);
  CHECK(value == 6);
  CHECK(changes == std::vector<int>{6, 7, 6});
  // A click on the track sets the value under the pointer (then the click
  // action takes one off, as in the original).
  h.click(30 + 50, 35);
  h.steps(d, 3);
  CHECK(value == 3);
}

TEST_CASE("Button grid neighbour links", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  std::vector<GridCell> cells(4);
  cells[0] = {10, 10, 60, "A", -1, 2, -1, 1, 0, false};
  cells[1] = {80, 10, 60, "B", -1, 3, 0, -1, 0, false};
  cells[2] = {10, 40, 60, "C", 0, -1, -1, 3, 0, true};
  cells[3] = {80, 40, 60, "D", 1, -1, 2, -1, 0xff44, false};
  Dialog d(h.gui, 0, 0, 300, 100, 0);
  ButtonGrid& grid = d.emplace<ButtonGrid>(cells, [](int i) { return 100 + i; });
  d.run_begin();
  h.steps(d, 1);
  h.key(kKeyRight);
  h.steps(d, 2);
  CHECK(grid.selection == 1);
  h.key(kKeyDown);
  h.steps(d, 2);
  CHECK(grid.selection == 3);
  h.key(kKeyLeft);  // cell 2 is disabled: no move
  h.steps(d, 2);
  CHECK(grid.selection == 3);
  h.key(kKeyEnter);
  CHECK(h.steps(d) == 103);
}

TEST_CASE("Text entry modal edit", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  std::deque<std::uint16_t> keys = {0x1e41, 0x3042, kKeyBackspace, 0x2e43, kKeyHome, 0x2d78, kKeyEnter};
  int pumps = 0;
  h.gui.set_pump([&] {
    ++h.tick;
    if (++pumps > 1000) {
      FAIL("the edit loop does not end");
    }
    if (!keys.empty() && pumps % 3 == 0) {
      h.key(keys.front());
      keys.pop_front();
    }
  });
  Dialog d(h.gui, 0, 0, 300, 100, kDialogCancel);
  std::string done_text;
  TextEntry& entry = d.emplace<TextEntry>(10, 10, 150, 8, "", [&](std::string& text, int) {
    done_text = text;
    return 5;
  });
  d.emplace<Button>(10, 50, 80, "OK", return_accept);
  CHECK(d.run() == 5);
  CHECK(done_text == "xAC");
  CHECK(entry.caret == 1);
}

TEST_CASE("Text entry respects its buffer length", "[ui]") {
  SH_REQUIRE_GAME_EXE();
  Harness h;
  Dialog d(h.gui, 0, 0, 200, 50, kDialogCancel);
  std::deque<std::uint16_t> keys = {0x2e63, 0x2064, 0x1265, kKeyTab};
  int pumps = 0;
  h.gui.set_pump([&] {
    ++h.tick;
    REQUIRE(++pumps < 1000);
    // The first key would reach the dialog before the edit loop starts.
    if (pumps > 2 && !keys.empty()) {
      h.key(keys.front());
      keys.pop_front();
    } else if (pumps > 50) {
      h.key(kKeyEsc);
    }
  });
  // The buffer holds max_length - 1 characters.
  TextEntry& e = d.emplace<TextEntry>(10, 10, 100, 4, "ab");
  CHECK(d.run() == kResultCancel);
  CHECK(e.text == "abc");
}

TEST_CASE("Default skin draws a button with the original colours", "[ui]") {
  SH_REQUIRE_GAME_DATA(install);
  GuiResources res = GuiResources::load(install);
  CHECK(res.small.height() == 15);
  DefaultSkin skin(res);
  Image8 img(200, 100, 0xee);
  const Viewport vp = Viewport::full(img);
  const int x = 20;
  const int y = 30;
  const int w = 100;
  const Rect r = skin.button(vp, x, y, w, "OK", false, false, false, false);
  CHECK(r == Rect{x - 2, y - 2, x + w + 5, y + 29});
  // The raised box: fill A1 = 3, left / top A3 = 4, right / bottom A2 = 2.
  CHECK(img.at(x + 3, y + 3) == 3);
  CHECK(img.at(x, y + 10) == 4);
  CHECK(img.at(x + 1, y + 10) == 4);
  CHECK(img.at(x + 10, y) == 4);
  CHECK(img.at(x + w - 1, y + 10) == 2);
  CHECK(img.at(x + 10, y + 23) == 2);
  // The sunken 2-pixel frame around it: B3 = 5 left / top, B2 = 7 right.
  CHECK(img.at(x - 2, y + 10) == 5);
  CHECK(img.at(x - 1, y - 1) == 5);
  CHECK(img.at(x + w + 1, y + 10) == 7);
  CHECK(img.at(x + 10, y + 25) == 7);
  CHECK(img.at(x + w + 2, y + 10) == 0xee);
  // The label is centred: its leftmost drawn pixel is the first opaque
  // column of 'O' at x + (w - text width) / 2.
  const int tx = x + (w - res.small.text_width("OK")) / 2;
  const Glyph* o = res.small.glyph('O');
  REQUIRE(o != nullptr);
  int first_col = static_cast<int>(o->width);
  for (int gy = 0; gy < res.small.height(); ++gy) {
    for (int gx = 0; gx < static_cast<int>(o->width); ++gx) {
      const std::uint8_t c = o->pixels[static_cast<std::size_t>(gy) * o->width + static_cast<std::size_t>(gx)];
      if (res.table[c] != 0xff) {
        first_col = std::min(first_col, gx);
      }
    }
  }
  int leftmost = 1000;
  for (int py = y + 7; py < y + 7 + res.small.height(); ++py) {
    for (int px = x + 2; px < x + w - 2; ++px) {
      if (img.at(px, py) != 3) {
        leftmost = std::min(leftmost, px);
      }
    }
  }
  CHECK(leftmost == tx + first_col);

  SECTION("pressed: edges swapped, label one pixel lower right") {
    Image8 pressed(200, 100, 0xee);
    skin.button(Viewport::full(pressed), x, y, w, "OK", true, false, false, false);
    CHECK(pressed.at(x, y + 10) == 2);
    CHECK(pressed.at(x + w - 1, y + 10) == 4);
    for (int py = 0; py < res.small.height(); ++py) {
      for (int px = 0; px < 40; ++px) {
        if (img.at(tx + px, y + 7 + py) != 3) {
          CHECK(pressed.at(tx + px + 1, y + 8 + py) == img.at(tx + px, y + 7 + py));
        }
      }
    }
  }
  SECTION("disabled: the plain colours") {
    Image8 disabled(200, 100, 0xee);
    skin.button(Viewport::full(disabled), x, y, w, "OK", false, false, false, true);
    CHECK(disabled.at(x + 3, y + 3) == 6);
    CHECK(disabled.at(x, y + 10) == 5);
    CHECK(disabled.at(x + w - 1, y + 10) == 7);
  }
}

TEST_CASE("Default skin: dialog frame, list and slider geometry", "[ui]") {
  SH_REQUIRE_GAME_DATA(install);
  GuiResources res = GuiResources::load(install);
  DefaultSkin skin(res);
  Image8 img(300, 300, 0xee);
  const Viewport vp = Viewport::full(img);
  CHECK(skin.dialog_frame(vp, 0, 0, 200, 100, true) == Rect{0, 0, 202, 102});
  CHECK(img.at(5, 5) == 3);    // outer fill A1
  CHECK(img.at(0, 50) == 4);   // outer left A3
  CHECK(img.at(199, 50) == 2); // outer right A2
  CHECK(img.at(6, 50) == 7);   // inner left B2
  CHECK(img.at(20, 20) == 6);  // inner fill B1
  CHECK(img.at(202, 102) == 0);  // the shadow
  CHECK(skin.list_down_rect(10, 20, 100) == Rect{116, 147, 135, 166});
  CHECK(skin.slider_track_rect(10, 20, 100) == Rect{10, 20, 109, 33});
  CHECK(skin.check_box_width("x") == 206);
  CHECK(skin.check_box_width("") == 19);
}

TEST_CASE("Text box word wrap and caret positions", "[ui]") {
  SH_REQUIRE_GAME_DATA(install);
  GuiResources res = GuiResources::load(install);
  DefaultSkin skin(res);
  Image8 screen(kScreenWidth, kScreenHeight, 0);
  std::uint32_t tick = 0;
  Gui gui(screen, [&] { return tick; }, skin);
  Dialog d(gui, 0, 0, 300, 200, 0);
  TextBox& box = d.emplace<TextBox>(10, 10, 120, 100, 16, 200, "one two three four five six seven eight");
  d.run_begin();
  d.run_step();
  CHECK(box.total > 1);
  // Every character of the first line maps back from its drawn position.
  for (int i = 0; i < 3; ++i) {
    int x = 0;
    int y = 0;
    box.layout(d, i, x, y);
    CHECK(y == 10);
    skin.textbox_text_origin(x, y);
    CHECK(box.layout(d, TextBox::kLayoutHitTest, x, y) == i);
  }
}

TEST_CASE("MENU skin metrics and sprites", "[ui]") {
  SH_REQUIRE_GAME_DATA(install);
  GuiResources res = GuiResources::load(install);
  const MenuResources menu = MenuResources::load(install);
  MenuSkin skin(res, menu);
  CHECK(skin.metrics().button_height == 35);
  CHECK(skin.metrics().list_height == 162);
  CHECK(skin.metrics().list_track == 100);
  CHECK(&skin.font(FontSlot::kSmall) == &menu.small);
  CHECK(&skin.font(FontSlot::kGui1) == &res.gui1);
  int clicks = 0;
  skin.click_sound = [&] { ++clicks; };
  // A 121-wide button uses MENU.SHP frames 2 / 3.
  Image8 a(300, 100, 0);
  Image8 b(300, 100, 0);
  skin.button(Viewport::full(a), 20, 20, 121, "", true, false, false, false);
  draw_sprite_clipped(Viewport::full(b), menu.sprites.frame(3), 20, 20);
  gui_focus_rect(Viewport::full(b), 23, 23, 115, 29, 0x51, false);
  CHECK(std::equal(a.pixels().begin(), a.pixels().end(), b.pixels().begin()));
  CHECK(clicks == 1);
  CHECK(skin.check_box_width("label") == 232);
}

TEST_CASE("Mouse cursor counter, drawing and erasing", "[ui]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp cursors = Shp::load(install, "ART/CURSORS.SHP");
  REQUIRE(cursors.frame_count() == 40);
  std::uint32_t tick = 0;
  EventQueue q([&] { return tick; });
  Mouse m(q);
  m.set_cursor(cursors, CursorId::kPointer);
  CHECK_FALSE(m.shown());
  m.show();
  CHECK(m.shown());
  m.hide();
  m.hide();
  m.show();
  CHECK_FALSE(m.shown());
  m.show();
  CHECK(m.shown());
  m.update(100, 100, 0, 0);
  Image8 frame(kScreenWidth, kScreenHeight, 0x42);
  const Image8 before = frame;
  m.draw(frame);
  const SpriteFrame& f = cursors.frame(3);
  CHECK(m.cursor_rect()->left == 100 + f.x0);
  CHECK_FALSE(std::equal(frame.pixels().begin(), frame.pixels().end(), before.pixels().begin()));
  m.erase(frame);
  CHECK(std::equal(frame.pixels().begin(), frame.pixels().end(), before.pixels().begin()));
  m.set_hide_rect(ClipRect{90, 90, 110, 110});
  m.draw(frame);
  CHECK(std::equal(frame.pixels().begin(), frame.pixels().end(), before.pixels().begin()));
}

}  // namespace sh::ui
