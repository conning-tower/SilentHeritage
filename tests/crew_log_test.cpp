// Tests of the crew message log of the extended variant (`Features::crew_log`):
// the history the command bar keeps, its listed lines and scrolling, its
// "CLOG" save section, the log the message strip and `L` open, and that
// without the flag (and with it, outside the log) the game is unchanged.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/byte_reader.h"
#include "core/error.h"
#include "game/command_bar.h"
#include "game/crew_log.h"
#include "game/sim_state.h"
#include "game_data.h"
#include "sim/state_io.h"
#include "stations_control_fixture.h"

namespace sh {
namespace {

using test::ControlRig;

// Returns a font of 128 one-pixel glyphs: a text is as wide as its length.
Font unit_font() {
  constexpr std::uint32_t kGlyphs = 128;
  ByteWriter out;
  out.bytes(std::vector<std::uint8_t>{'1', '.', 0, 0});
  out.u32(kGlyphs);
  out.u32(1);
  out.u32(0);
  for (std::uint32_t i = 0; i < kGlyphs; ++i) {
    out.u32(16 + 4 * kGlyphs + 5 * i);
  }
  for (std::uint32_t i = 0; i < kGlyphs; ++i) {
    out.u32(1);
    out.u8(1);
  }
  return Font::parse(out.data(), "unit font");
}

GameClock clock_at(std::int32_t day, std::int32_t hour, std::int32_t minute, std::int32_t second) {
  GameClock c;
  c.init(14, 1, day, hour, minute, second);
  return c;
}

// A click on the message strip, and the BIOS words of `l` and Esc.
constexpr int kStripX = 100;
constexpr int kStripY = 470;
constexpr std::uint16_t kKeyL = 0x266c;
constexpr std::uint16_t kKeyEsc = 0x011b;

// Starts HIST01 with the crew log switched `on`.
void start(ControlRig& rig, bool on) {
  rig.screen->env().ui.features.crew_log = on;
  rig.frames(1);
  rig.settle();
}

// Posts crew messages through the keys (rudder, telegraph) and the bar.
void script(ControlRig& rig) {
  rig.key(0x0534);  // '4': ahead full
  rig.frames(20);
  rig.key(0x4d00);  // Right: rudder
  rig.frames(20);
  rig.screen->bar().crew_message(31, -1, -1, "Stern planes");
  rig.frames(20);
  rig.key(0x4cf0);  // keypad 5: straight and level
  rig.key(0x0b30);  // '0': all stop
  rig.frames(20);
}

std::vector<std::uint8_t> sim_bytes(ControlRig& rig) { return capture_sim_state(rig.mission(), {}, {}).serialize(); }

std::vector<std::uint8_t> session_bytes(ControlRig& rig) {
  return capture_sim_state(rig.mission(), {}, {.screen = rig.screen.get()}).serialize();
}

std::uint32_t random_state(ControlRig& rig) { return rig.mission().sim().random().state(); }

bool contains(const std::vector<std::uint8_t>& bytes, std::string_view s) {
  return std::search(bytes.begin(), bytes.end(), s.begin(), s.end()) != bytes.end();
}

}  // namespace

// --- The history ----------------------------------------------------------

TEST_CASE("The crew log keeps the newest 100 messages with their game times", "[crewlog]") {
  CrewLog log;
  log.add(clock_at(3, 22, 15, 40), "OOD", "Ship spotted, bearing 337.");
  REQUIRE(log.entries().size() == 1);
  const CrewLogEntry& e = log.entries().front();
  CHECK(e.day == 3);
  CHECK(e.month == 1);
  CHECK(e.year == 14);
  CHECK(e.hour == 22);
  CHECK(e.minute == 15);
  CHECK(e.second == 40);
  CHECK(e.speaker == "OOD");
  CHECK(e.text == "Ship spotted, bearing 337.");
  CHECK(crew_log_time(e) == "22:15:40");
  for (int i = 1; i < 105; ++i) {
    log.add(clock_at(4, i / 60, i % 60, 0), "Sound", std::to_string(i));
  }
  CHECK(log.entries().size() == CrewLog::kMaxEntries);
  CHECK(log.entries().front().text == "5");
  CHECK(log.entries().back().text == "104");
}

TEST_CASE("The crew log's lines head each day and wrap the messages", "[crewlog]") {
  CrewLog log;
  log.add(clock_at(3, 23, 59, 0), "OOD", "abc def ghi");
  log.add(clock_at(3, 23, 59, 30), "OOD", "x");
  log.add(clock_at(4, 0, 1, 0), "Sound", "abcdefghijklmnopqrstuvwxyz");
  const Font font = unit_font();
  using K = CrewLogLine::Kind;
  const std::vector<CrewLogLine> expected = {
      {K::kDate, 0, ""},
      {K::kMessage, 0, "OOD: abc"},
      {K::kContinuation, 0, "def ghi"},
      {K::kMessage, 1, "OOD: x"},
      {K::kDate, 2, ""},
      {K::kMessage, 2, "Sound:"},
      // A word wider than the line is cut.
      {K::kContinuation, 2, "abcdefghij"},
      {K::kContinuation, 2, "klmnopqrst"},
      {K::kContinuation, 2, "uvwxyz"},
  };
  CHECK(crew_log_lines(log.entries(), font, 10) == expected);
  CHECK(crew_log_lines({}, font, 10).empty());
}

TEST_CASE("The crew log's list starts on the newest lines and scrolls within them", "[crewlog]") {
  CrewLogScroll scroll(40, 25);
  CHECK(scroll.top() == 15);
  CHECK(scroll.at_end());
  CHECK_FALSE(scroll.scroll_by(1));
  CHECK(scroll.scroll_by(-25));
  CHECK(scroll.top() == 0);
  CHECK(scroll.at_start());
  CHECK_FALSE(scroll.scroll_by(-1));
  CHECK(scroll.scroll_to(7));
  CHECK(scroll.top() == 7);
  CHECK(scroll.scroll_to(1000));
  CHECK(scroll.top() == 15);
  // Fewer lines than rows: nothing to scroll.
  CrewLogScroll short_list(3, 25);
  CHECK(short_list.top() == 0);
  CHECK(short_list.at_start());
  CHECK(short_list.at_end());
  CHECK_FALSE(short_list.scroll_by(1));
}

TEST_CASE("The crew log's CLOG section round-trips", "[crewlog][save]") {
  CrewLog log;
  log.add(clock_at(3, 22, 15, 40), "OOD", "Ship spotted, bearing 337.");
  log.add(clock_at(4, 0, 1, 2), "Radio Operator", std::string(80, 'x'));
  const std::vector<std::uint8_t> bytes = log.serialize();
  CHECK(CrewLog::parse(bytes) == log);
  CHECK(CrewLog::parse(CrewLog().serialize()).entries().empty());
  const std::vector<std::uint8_t> cut(bytes.begin(), bytes.end() - 1);
  CHECK_THROWS_AS(CrewLog::parse(cut), DataError);
  std::vector<std::uint8_t> longer = bytes;
  longer.push_back(0);
  CHECK_THROWS_AS(CrewLog::parse(longer), DataError);
}

// --- In the mission -------------------------------------------------------

TEST_CASE("The bar records each crew message with its game time and full text", "[crewlog]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  start(rig, true);
  const GameClock& clock = rig.mission().sim().clock();
  const std::size_t before = rig.screen->bar().history().entries().size();
  rig.key(0x4d00);  // Right: rudder
  const std::string name(60, 'n');
  rig.screen->bar().crew_message(31, -1, -1, name);
  const std::deque<CrewLogEntry>& entries = rig.screen->bar().history().entries();
  REQUIRE(entries.size() == before + 2);
  const CrewLogEntry& rudder = entries[before];
  CHECK(rudder.speaker == "Helmsman");
  CHECK(rudder.text == "5 degrees right rudder, aye.");
  CHECK(rudder.day == clock.day());
  CHECK(rudder.hour == clock.hour());
  CHECK(rudder.minute == clock.minute());
  CHECK(rudder.second == clock.second());
  // The ring keeps 0x31 characters, the log the whole text.
  CHECK(entries.back().speaker == "OOD");
  CHECK(entries.back().text == name + " damaged, sir.");
  CHECK(rig.screen->bar().current_message().size() < entries.back().text.size());
}

TEST_CASE("The message strip and L open the crew log over the paused mission", "[crewlog]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  start(rig, true);
  for (int i = 0; i < 8; ++i) {
    script(rig);
  }
  REQUIRE(rig.screen->bar().history().entries().size() > 25);
  rig.click(kStripX, kStripY);
  rig.frames(3);
  CHECK(rig.screen->env().modals.modal_active());
  CHECK(rig.mission().paused());
  const std::vector<std::uint8_t> sim = sim_bytes(rig);
  const std::uint32_t random = random_state(rig);
  rig.save("crew_log");
  // The pixels of the list's box (the focus and the pointer move outside).
  const auto screen = [&] {
    std::vector<std::uint8_t> out;
    for (int y = 0x10 + 0x34; y < 0x10 + 0x34 + 25 * 13 + 6; ++y) {
      for (int x = 0x28 + 0x12; x < 0x28 + 0x12 + 0x1f0; ++x) {
        out.push_back(rig.screen->screen().at(x, y));
      }
    }
    return out;
  };
  const std::vector<std::uint8_t> newest = screen();
  // A key acts in its second frame; the screen shows it in the next.
  rig.key(0x4900, 3);  // PgUp: earlier lines
  CHECK(screen() != newest);
  rig.save("crew_log_earlier");
  rig.key(0x4f00, 3);  // End: the newest again
  CHECK(screen() == newest);
  ui::Mouse& mouse = rig.gui.mouse();
  // The arrows right of the list: the down one is disabled at the end.
  const int arrow_x = 0x28 + 0x20a + 10;
  const int up_y = 0x10 + 0x36 + 8;
  const int down_y = 0x10 + 0x34 + 25 * 13 + 6 - 2 - rig.gui.skin().metrics().button_height + 8;
  const auto click = [&](int x, int y) {
    rig.click(x, y);
    rig.frames(2);
  };
  click(arrow_x, down_y);
  CHECK(screen() == newest);
  click(arrow_x, up_y);
  CHECK(screen() != newest);
  click(arrow_x, down_y);
  CHECK(screen() == newest);
  // Dragging the lines down (a touch drag) shows earlier ones.
  mouse.update(300, 200, 0, 0);
  rig.frames(1);
  mouse.update(300, 200, ui::kButtonLeft, 0);
  rig.frames(1);
  mouse.update(300, 265, ui::kButtonLeft, 0);
  rig.frames(2);
  mouse.update(300, 265, 0, 0);
  rig.frames(1);
  mouse.update(kStripX, kStripY, 0, 0);
  rig.frames(2);
  CHECK(screen() != newest);
  rig.key(0x4f00, 3);
  CHECK(screen() == newest);
  // L closes it. Reopened at once, before the simulation runs again, the
  // log finds the paused simulation as it left it.
  rig.gui.mouse().post_key(kKeyL, 0);
  for (int i = 0; i < 4 && rig.screen->env().modals.modal_active(); ++i) {
    rig.frames(1);
  }
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  rig.screen->bar().open_crew_log();
  rig.frames(1);
  CHECK(rig.mission().paused());
  CHECK(sim_bytes(rig) == sim);
  CHECK(random_state(rig) == random);
  rig.key(kKeyEsc);
  rig.frames(2);
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  CHECK_FALSE(rig.mission().paused());

  rig.key(kKeyL);
  rig.frames(2);
  CHECK(rig.screen->env().modals.modal_active());
  rig.key(kKeyEsc);
  rig.frames(2);
  CHECK_FALSE(rig.screen->env().modals.modal_active());
}

TEST_CASE("The crew log is saved in a CLOG section and restored with the session", "[crewlog][save]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  start(rig, true);
  script(rig);
  const SimStateFile file = capture_sim_state(rig.mission(), {}, {.screen = rig.screen.get()});
  REQUIRE_FALSE(file.crew_log_extension.empty());
  const std::vector<std::uint8_t> bytes = file.serialize();
  ByteReader reader(bytes, "state");
  const SimStateFile parsed = SimStateFile::parse(reader, kSimStateVersion);
  CHECK(parsed.crew_log_extension == file.crew_log_extension);

  ControlRig restored(install);
  start(restored, true);
  restore_session_state(parsed, {.screen = restored.screen.get()});
  CHECK(restored.screen->bar().history() == rig.screen->bar().history());

  // The faithful game reads the file and leaves the log out.
  ControlRig faithful(install);
  start(faithful, false);
  restore_session_state(parsed, {.screen = faithful.screen.get()});
  CHECK(faithful.screen->bar().history().entries().empty());
}

// --- Without the flag -----------------------------------------------------

TEST_CASE("Without crew_log the bar records nothing and neither the strip nor L opens a log",
          "[crewlog][faithful]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig rig(install);
  start(rig, false);
  script(rig);
  CHECK(rig.screen->bar().history().entries().empty());
  rig.click(kStripX, kStripY);
  rig.frames(3);
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  CHECK_FALSE(rig.mission().paused());
  rig.key(kKeyL);
  rig.frames(2);
  CHECK_FALSE(rig.screen->env().modals.modal_active());
  const SimStateFile file = capture_sim_state(rig.mission(), {}, {.screen = rig.screen.get()});
  CHECK(file.crew_log_extension.empty());
  CHECK_FALSE(contains(file.serialize(), "CLOG"));
}

TEST_CASE("crew_log leaves the pixels, the simulation and the saved bytes as without it", "[crewlog][faithful]") {
  SH_REQUIRE_GAME_DATA(install);
  ControlRig off(install);
  ControlRig on(install);
  start(off, false);
  start(on, true);
  for (ControlRig* rig : {&off, &on}) {
    script(*rig);
    rig->frames(10);
  }
  CHECK(std::ranges::equal(on.screen->screen().pixels(), off.screen->screen().pixels()));
  CHECK(on.screen->dac() == off.screen->dac());
  CHECK(sim_bytes(on) == sim_bytes(off));
  CHECK(random_state(on) == random_state(off));
  // The session's bytes: the same, then the CLOG section.
  const std::vector<std::uint8_t> a = session_bytes(off);
  const std::vector<std::uint8_t> b = session_bytes(on);
  const std::vector<std::uint8_t> clog = on.screen->bar().history().serialize();
  REQUIRE(b.size() == a.size() + 8 + clog.size());
  CHECK(std::equal(a.begin(), a.end(), b.begin()));
  ByteReader tail(std::span(b).subspan(a.size()), "tail");
  CHECK(tail.u32() == 0x474f4c43);  // "CLOG"
  CHECK(tail.u32() == clog.size());
  CHECK(std::ranges::equal(tail.bytes(clog.size()), clog));
}

}  // namespace sh
