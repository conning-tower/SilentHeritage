// Tests of the layout-aware BIOS key words: key-down events as SDL delivers
// them for US, French AZERTY and BEPO layouts (the event keycode with the
// default SDL_HINT_KEYCODE_OPTIONS, which reports both French number rows
// as digits; the characters of the layout's keymap), checked against the
// BIOS word and the in-game hotkey they fire.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <optional>
#include <string>
#include <vector>

#include "game/command_bar.h"
#include "platform/bios_keys.h"
#include "ui/hotspot.h"

namespace sh {
namespace {

// A key press of a layout.
struct Press {
  const char* layout;
  SDL_Scancode scancode;
  std::uint32_t key;  // the event keycode
  std::uint16_t mods;
  char32_t character;
  char32_t altgr_character;
  std::optional<std::uint16_t> word;
  std::optional<HotkeyAction> action;
};

InputEvent key_down(const Press& p) {
  InputEvent e{.type = EventType::kKeyDown, .key = p.key, .mods = p.mods};
  e.scancode = p.scancode;
  e.character = p.character;
  e.altgr_character = p.altgr_character;
  return e;
}

// Returns the action of the control bar hotkey that `word` fires: the
// first match in table order, as `HotspotTable::find_hotkey` finds it (the
// 0xe0 byte dropped as `input_timer_isr` does). '-' also matches the Enter
// hotkey 0xff0d (the original's case folding), after the '-' one.
std::optional<HotkeyAction> fired(std::uint16_t word) {
  const auto key = static_cast<std::uint16_t>((word & 0xff) == 0xe0 ? word & 0xff00 : word);
  for (const Hotkey& h : hotkey_table()) {
    if (ui::hotkey_matches(h.key, key)) {
      return h.action;
    }
  }
  return std::nullopt;
}

using HA = HotkeyAction;
constexpr std::uint16_t kShift = SDL_KMOD_LSHIFT;
constexpr std::uint16_t kAlt = SDL_KMOD_LALT;
constexpr std::uint16_t kRightAlt = SDL_KMOD_RALT;
constexpr std::uint16_t kCtrl = SDL_KMOD_LCTRL;
constexpr std::uint16_t kCaps = SDL_KMOD_CAPS;
constexpr std::optional<std::uint16_t> kNoWord;
constexpr std::optional<HA> kNoAction;

}  // namespace

TEST_CASE("Key words follow the keyboard layout", "[keys]") {
  const std::vector<Press> presses = {
      // US.
      {"us", SDL_SCANCODE_EQUALS, SDLK_EQUALS, kShift, '+', 0, 0x0d2b, HA::kCompressionUp},
      {"us", SDL_SCANCODE_MINUS, SDLK_MINUS, 0, '-', 0, 0x0c2d, HA::kCompressionDown},
      {"us", SDL_SCANCODE_7, SDLK_7, 0, '7', 0, 0x0837, HA::kBackTwoThirds},
      {"us", SDL_SCANCODE_7, SDLK_7, kShift, '&', 0, 0x0826, kNoAction},
      {"us", SDL_SCANCODE_1, SDLK_1, kAlt, '1', 0, 0x7800, HA::kFireTube1},
      {"us", SDL_SCANCODE_0, SDLK_0, kRightAlt, '0', 0, 0x8100, HA::kFireTube10},
      {"us", SDL_SCANCODE_P, SDLK_P, kAlt, 'p', 0, 0x1900, HA::kTogglePeriscope},
      {"us", SDL_SCANCODE_B, SDLK_B, 0, 'b', 0, 0x3062, HA::kBlowBallast},
      {"us", SDL_SCANCODE_S, SDLK_S, kShift, 'S', 0, 0x1f53, HA::kSurface},
      {"us", SDL_SCANCODE_A, SDLK_A, kCtrl, 'a', 0, 0x1e01, kNoAction},
      {"us", SDL_SCANCODE_6, SDLK_6, kCtrl, '6', 0, 0x071e, kNoAction},
      // AZERTY: '+' is Shift with '=', '-' is the unshifted 6 key, the
      // digits are shifted.
      {"fr", SDL_SCANCODE_EQUALS, SDLK_EQUALS, kShift, '+', 0, 0x0d2b, HA::kCompressionUp},
      {"fr", SDL_SCANCODE_6, SDLK_6, 0, '-', '|', 0x0c2d, HA::kCompressionDown},
      {"fr", SDL_SCANCODE_7, SDLK_7, 0, U'è', '`', kNoWord, kNoAction},
      {"fr", SDL_SCANCODE_7, SDLK_7, kShift, '7', 0, 0x0837, HA::kBackTwoThirds},
      {"fr", SDL_SCANCODE_1, SDLK_1, 0, '&', 0, 0x0826, kNoAction},
      {"fr", SDL_SCANCODE_1, SDLK_1, kShift, '1', 0, 0x0231, HA::kAheadOneThird},
      {"fr", SDL_SCANCODE_0, SDLK_0, kShift, '0', 0, 0x0b30, HA::kAllStop},
      {"fr", SDL_SCANCODE_2, SDLK_2, 0, U'é', '~', kNoWord, kNoAction},
      // Alt with a top-row key fires the tube of its digit.
      {"fr", SDL_SCANCODE_1, SDLK_1, kAlt, '&', 0, 0x7800, HA::kFireTube1},
      {"fr", SDL_SCANCODE_7, SDLK_7, kAlt, U'è', '`', 0x7e00, HA::kFireTube7},
      // AltGr: the AltGr character, or Alt when the key has none.
      {"fr", SDL_SCANCODE_0, SDLK_0, kRightAlt, U'à', '@', 0x0340, kNoAction},
      {"fr", SDL_SCANCODE_0, SDLK_0, kCtrl | kRightAlt, U'à', '@', 0x0340, kNoAction},
      {"fr", SDL_SCANCODE_0, SDLK_0, SDL_KMOD_MODE, U'à', '@', 0x0340, kNoAction},
      {"fr", SDL_SCANCODE_1, SDLK_1, kRightAlt, '&', 0, 0x7800, HA::kFireTube1},
      // Letters follow the layout, with Alt too.
      {"fr", SDL_SCANCODE_Q, SDLK_A, kAlt, 'a', 0, 0x1e00, HA::kToggleSdRadar},
      {"fr", SDL_SCANCODE_A, SDLK_Q, kAlt, 'q', 0, 0x1000, HA::kQuitToDos},
      {"fr", SDL_SCANCODE_SEMICOLON, SDLK_M, 0, 'm', 0, 0x326d, HA::kMenu1mc},
      {"fr", SDL_SCANCODE_M, SDLK_COMMA, 0, ',', 0, 0x332c, kNoAction},
      {"fr", SDL_SCANCODE_NONUSBACKSLASH, SDLK_LESS, kShift, '>', 0, 0x343e, kNoAction},
      {"fr", SDL_SCANCODE_NONUSBACKSLASH, SDLK_LESS, kAlt, '<', 0, kNoWord, kNoAction},
      // BEPO: the top row types $"«»()@+-/*=% unshifted, the digits
      // shifted.
      {"bepo", SDL_SCANCODE_7, SDLK_7, 0, '+', U'±', 0x0d2b, HA::kCompressionUp},
      {"bepo", SDL_SCANCODE_7, SDLK_7, kShift, '7', 0, 0x0837, HA::kBackTwoThirds},
      {"bepo", SDL_SCANCODE_8, SDLK_8, 0, '-', U'−', 0x0c2d, HA::kCompressionDown},
      {"bepo", SDL_SCANCODE_9, SDLK_9, 0, '/', U'÷', 0x352f, kNoAction},
      {"bepo", SDL_SCANCODE_1, SDLK_1, 0, '"', U'—', 0x2822, kNoAction},
      {"bepo", SDL_SCANCODE_1, SDLK_1, kShift, '1', U'„', 0x0231, HA::kAheadOneThird},
      {"bepo", SDL_SCANCODE_1, SDLK_1, kAlt, '"', U'—', 0x7800, HA::kFireTube1},
      {"bepo", SDL_SCANCODE_1, SDLK_1, kRightAlt, '"', U'—', 0x7800, HA::kFireTube1},
      {"bepo", SDL_SCANCODE_E, SDLK_P, kAlt, 'p', U'&', 0x1900, HA::kTogglePeriscope},
      {"bepo", SDL_SCANCODE_Q, SDLK_B, 0, 'b', '|', 0x3062, HA::kBlowBallast},
      {"bepo", SDL_SCANCODE_Q, SDLK_B, kCtrl, 'b', '|', 0x3002, kNoAction},
      {"bepo", SDL_SCANCODE_H, SDLK_C, kShift, 'C', U'©', 0x2e43, HA::kCrashDive},
      {"bepo", SDL_SCANCODE_K, SDLK_S, kCaps, 's', U'ß', 0x1f53, HA::kSurface},
      {"bepo", SDL_SCANCODE_K, SDLK_S, kCaps | kShift, 'S', U'ẞ', 0x1f73, HA::kSurface},
      {"bepo", SDL_SCANCODE_W, U'é', 0, U'é', U'ˊ', kNoWord, kNoAction},
      {"bepo", SDL_SCANCODE_V, SDLK_PERIOD, 0, '.', U'…', 0x342e, kNoAction},
      // Non-character keys keep their scan codes on every layout.
      {"bepo", SDL_SCANCODE_RETURN, SDLK_RETURN, 0, '\r', 0, 0x1c0d, HA::kCompressionReset},
      {"bepo", SDL_SCANCODE_F1, SDLK_F1, 0, SDLK_F1, 0, 0x3b00, HA::kStationConn},
      {"bepo", SDL_SCANCODE_F4, SDLK_F4, kAlt, SDLK_F4, 0, 0x6b00, HA::kStationDeckGun},
      {"bepo", SDL_SCANCODE_KP_PLUS, SDLK_KP_PLUS, 0, SDLK_KP_PLUS, 0, 0x4e2b, HA::kCompressionUp},
      {"bepo", SDL_SCANCODE_KP_5, SDLK_KP_5, 0, SDLK_KP_5, 0, 0x4cf0, HA::kStraightAndLevel},
      {"bepo", SDL_SCANCODE_UP, SDLK_UP, 0, SDLK_UP, 0, 0x48e0, HA::kPlanesRise},
      {"bepo", SDL_SCANCODE_TAB, SDLK_TAB, 0, '\t', 0, 0x0f09, HA::kReconPhoto},
      {"bepo", SDL_SCANCODE_SPACE, SDLK_SPACE, 0, ' ', U' ', 0x3920, kNoAction},
  };
  for (const Press& p : presses) {
    INFO(p.layout << " scancode " << p.scancode << " mods " << p.mods);
    const std::optional<std::uint16_t> word = bios_key_word(key_down(p));
    CHECK(word == p.word);
    if (word) {
      CHECK(fired(*word) == p.action);
    }
  }
}

TEST_CASE("Text typed on a BEPO keyboard reaches the game", "[keys]") {
  // "Bo7+" (B, o, Shift and the '+' key, the '+' key).
  const std::vector<Press> presses = {
      {"bepo", SDL_SCANCODE_Q, SDLK_B, kShift, 'B', 0, {}, {}},
      {"bepo", SDL_SCANCODE_R, SDLK_O, 0, 'o', 0, {}, {}},
      {"bepo", SDL_SCANCODE_7, SDLK_7, kShift, '7', 0, {}, {}},
      {"bepo", SDL_SCANCODE_7, SDLK_7, 0, '+', 0, {}, {}},
  };
  std::string text;
  for (const Press& p : presses) {
    const std::optional<std::uint16_t> word = bios_key_word(key_down(p));
    REQUIRE(word);
    text += static_cast<char>(*word & 0xff);
  }
  CHECK(text == "Bo7+");
}

TEST_CASE("Synthesized key events are read as US keys", "[keys]") {
  // The --keys script events carry no scancode.
  const auto synthetic = [](std::uint32_t key, std::uint16_t mods) {
    return bios_key_word(InputEvent{.type = EventType::kKeyDown, .key = key, .mods = mods});
  };
  CHECK(synthetic('+', 0) == 0x0d2b);
  CHECK(synthetic('*', 0) == 0x092a);
  CHECK(synthetic(SDLK_7, 0) == 0x0837);
  CHECK(synthetic(SDLK_1, kAlt) == 0x7800);
  CHECK(synthetic(SDLK_S, kShift) == 0x1f53);
  CHECK(synthetic(SDLK_KP_PLUS, 0) == 0x4e2b);
}

TEST_CASE("Android's Back key is Esc, told apart", "[keys][back]") {
  InputEvent back{.type = EventType::kKeyDown, .key = SDLK_AC_BACK};
  back.scancode = SDL_SCANCODE_AC_BACK;
  CHECK(bios_key_word(back) == 0x011b);
  CHECK(is_back_key(back));
  CHECK_FALSE(is_back_key(InputEvent{.type = EventType::kKeyDown, .key = SDLK_ESCAPE}));
  CHECK_FALSE(is_back_key(InputEvent{.type = EventType::kKeyUp, .key = SDLK_AC_BACK}));
}

}  // namespace sh
