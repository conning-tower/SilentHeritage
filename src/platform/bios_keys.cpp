#include "platform/bios_keys.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

#include <array>

namespace sh {

namespace {

// The words of one key: plain, with Shift, Ctrl and Alt (0 = no word).
struct KeyWords {
  std::uint32_t key;
  std::uint16_t plain;
  std::uint16_t shift;
  std::uint16_t ctrl;
  std::uint16_t alt;
};

// Keys whose words do not depend on Caps Lock or Num Lock (US layout).
constexpr std::array<KeyWords, 55> kKeys{{
    {SDLK_ESCAPE, 0x011b, 0x011b, 0x011b, 0x0100},
    // Android's Back button and gesture (see `is_back_key`).
    {SDLK_AC_BACK, 0x011b, 0x011b, 0x011b, 0x0100},
    {SDLK_1, 0x0231, 0x0221, 0, 0x7800},
    {SDLK_2, 0x0332, 0x0340, 0x0300, 0x7900},
    {SDLK_3, 0x0433, 0x0423, 0, 0x7a00},
    {SDLK_4, 0x0534, 0x0524, 0, 0x7b00},
    {SDLK_5, 0x0635, 0x0625, 0, 0x7c00},
    {SDLK_6, 0x0736, 0x075e, 0x071e, 0x7d00},
    {SDLK_7, 0x0837, 0x0826, 0, 0x7e00},
    {SDLK_8, 0x0938, 0x092a, 0, 0x7f00},
    {SDLK_9, 0x0a39, 0x0a28, 0, 0x8000},
    {SDLK_0, 0x0b30, 0x0b29, 0, 0x8100},
    {SDLK_MINUS, 0x0c2d, 0x0c5f, 0x0c1f, 0x8200},
    {SDLK_EQUALS, 0x0d3d, 0x0d2b, 0, 0x8300},
    {SDLK_BACKSPACE, 0x0e08, 0x0e08, 0x0e7f, 0x0e00},
    {SDLK_TAB, 0x0f09, 0x0f00, 0x9400, 0xa500},
    {SDLK_LEFTBRACKET, 0x1a5b, 0x1a7b, 0x1a1b, 0x1a00},
    {SDLK_RIGHTBRACKET, 0x1b5d, 0x1b7d, 0x1b1d, 0x1b00},
    {SDLK_RETURN, 0x1c0d, 0x1c0d, 0x1c0a, 0x1c00},
    {SDLK_SEMICOLON, 0x273b, 0x273a, 0, 0x2700},
    {SDLK_APOSTROPHE, 0x2827, 0x2822, 0, 0x2800},
    {SDLK_GRAVE, 0x2960, 0x297e, 0, 0x2900},
    {SDLK_BACKSLASH, 0x2b5c, 0x2b7c, 0x2b1c, 0x2b00},
    {SDLK_COMMA, 0x332c, 0x333c, 0, 0x3300},
    {SDLK_PERIOD, 0x342e, 0x343e, 0, 0x3400},
    {SDLK_SLASH, 0x352f, 0x353f, 0, 0x3500},
    {SDLK_SPACE, 0x3920, 0x3920, 0x3920, 0x3920},
    {SDLK_F1, 0x3b00, 0x5400, 0x5e00, 0x6800},
    {SDLK_F2, 0x3c00, 0x5500, 0x5f00, 0x6900},
    {SDLK_F3, 0x3d00, 0x5600, 0x6000, 0x6a00},
    {SDLK_F4, 0x3e00, 0x5700, 0x6100, 0x6b00},
    {SDLK_F5, 0x3f00, 0x5800, 0x6200, 0x6c00},
    {SDLK_F6, 0x4000, 0x5900, 0x6300, 0x6d00},
    {SDLK_F7, 0x4100, 0x5a00, 0x6400, 0x6e00},
    {SDLK_F8, 0x4200, 0x5b00, 0x6500, 0x6f00},
    {SDLK_F9, 0x4300, 0x5c00, 0x6600, 0x7000},
    {SDLK_F10, 0x4400, 0x5d00, 0x6700, 0x7100},
    {SDLK_F11, 0x8500, 0x8700, 0x8900, 0x8b00},
    {SDLK_F12, 0x8600, 0x8800, 0x8a00, 0x8c00},
    // The gray cursor block: ASCII 0xe0 (the game drops it).
    {SDLK_INSERT, 0x52e0, 0x52e0, 0x92e0, 0xa200},
    {SDLK_DELETE, 0x53e0, 0x53e0, 0x93e0, 0xa300},
    {SDLK_HOME, 0x47e0, 0x47e0, 0x77e0, 0x9700},
    {SDLK_END, 0x4fe0, 0x4fe0, 0x75e0, 0x9f00},
    {SDLK_PAGEUP, 0x49e0, 0x49e0, 0x84e0, 0x9900},
    {SDLK_PAGEDOWN, 0x51e0, 0x51e0, 0x76e0, 0xa100},
    {SDLK_UP, 0x48e0, 0x48e0, 0x8de0, 0x9800},
    {SDLK_DOWN, 0x50e0, 0x50e0, 0x91e0, 0xa000},
    {SDLK_LEFT, 0x4be0, 0x4be0, 0x73e0, 0x9b00},
    {SDLK_RIGHT, 0x4de0, 0x4de0, 0x74e0, 0x9d00},
    // The keypad keys that Num Lock does not change.
    {SDLK_KP_DIVIDE, 0xe02f, 0xe02f, 0x9500, 0xa400},
    {SDLK_KP_MULTIPLY, 0x372a, 0x372a, 0x9600, 0x3700},
    {SDLK_KP_MINUS, 0x4a2d, 0x4a2d, 0x8e00, 0x4a00},
    {SDLK_KP_PLUS, 0x4e2b, 0x4e2b, 0x9000, 0x4e00},
    {SDLK_KP_ENTER, 0xe00d, 0xe00d, 0xe00a, 0xa600},
    {SDLK_PRINTSCREEN, 0, 0, 0x7200, 0},
}};

// The scan codes of the letters a..z.
constexpr std::array<std::uint8_t, 26> kLetterScans{0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                                    0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                                    0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};

// The keypad digit keys: the cursor word (Num Lock off), the digit word
// (Num Lock on), the Ctrl word.
struct KeypadWords {
  std::uint32_t key;
  std::uint16_t cursor;
  std::uint16_t digit;
  std::uint16_t ctrl;
};

constexpr std::array<KeypadWords, 11> kKeypad{{
    {SDLK_KP_7, 0x4700, 0x4737, 0x7700},
    {SDLK_KP_8, 0x4800, 0x4838, 0x8d00},
    {SDLK_KP_9, 0x4900, 0x4939, 0x8400},
    {SDLK_KP_4, 0x4b00, 0x4b34, 0x7300},
    {SDLK_KP_5, 0x4cf0, 0x4c35, 0x8f00},
    {SDLK_KP_6, 0x4d00, 0x4d36, 0x7400},
    {SDLK_KP_1, 0x4f00, 0x4f31, 0x7500},
    {SDLK_KP_2, 0x5000, 0x5032, 0x9100},
    {SDLK_KP_3, 0x5100, 0x5133, 0x7600},
    {SDLK_KP_0, 0x5200, 0x5230, 0x9200},
    {SDLK_KP_PERIOD, 0x5300, 0x532e, 0x9300},
}};

std::optional<std::uint16_t> word_or_none(std::uint16_t w) {
  return w == 0 ? std::nullopt : std::optional<std::uint16_t>(w);
}

// The word of the letter `index` (0 = a) with ASCII `ascii`.
std::uint16_t letter_word(std::uint32_t index, std::uint32_t ascii) {
  return static_cast<std::uint16_t>((kLetterScans[index] << 8) | ascii);
}

// Returns the index (0 = a) of the ASCII letter `c` of either case.
std::optional<std::uint32_t> letter_index(char32_t c) {
  if (c >= 'a' && c <= 'z') {
    return c - 'a';
  }
  if (c >= 'A' && c <= 'Z') {
    return c - 'A';
  }
  return std::nullopt;
}

bool printable_ascii(char32_t c) { return c >= 0x20 && c <= 0x7e; }

// Returns the word a US keyboard gives for typing the printable ASCII
// character `c` (with Shift when it is a shifted character).
std::uint16_t us_character_word(char32_t c) {
  if (const std::optional<std::uint32_t> letter = letter_index(c)) {
    return letter_word(*letter, c);
  }
  // The main block comes first in `kKeys`: '+' is Shift-= rather than the
  // keypad's.
  for (const KeyWords& k : kKeys) {
    if (char32_t{static_cast<std::uint8_t>(k.plain)} == c) {
      return k.plain;
    }
    if (char32_t{static_cast<std::uint8_t>(k.shift)} == c) {
      return k.shift;
    }
  }
  return 0;  // unreachable: the US keyboard types all of printable ASCII
}

// The physical keys of the main block that type characters (the key
// between left Shift and Z of ISO keyboards has no US key: 0).
std::optional<std::uint32_t> us_character_key(std::uint32_t scancode) {
  if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
    return SDLK_A + (scancode - SDL_SCANCODE_A);
  }
  if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9) {
    return SDLK_1 + (scancode - SDL_SCANCODE_1);
  }
  switch (scancode) {
    case SDL_SCANCODE_0:
      return SDLK_0;
    case SDL_SCANCODE_MINUS:
      return SDLK_MINUS;
    case SDL_SCANCODE_EQUALS:
      return SDLK_EQUALS;
    case SDL_SCANCODE_LEFTBRACKET:
      return SDLK_LEFTBRACKET;
    case SDL_SCANCODE_RIGHTBRACKET:
      return SDLK_RIGHTBRACKET;
    case SDL_SCANCODE_BACKSLASH:
    case SDL_SCANCODE_NONUSHASH:
      return SDLK_BACKSLASH;
    case SDL_SCANCODE_SEMICOLON:
      return SDLK_SEMICOLON;
    case SDL_SCANCODE_APOSTROPHE:
      return SDLK_APOSTROPHE;
    case SDL_SCANCODE_GRAVE:
      return SDLK_GRAVE;
    case SDL_SCANCODE_COMMA:
      return SDLK_COMMA;
    case SDL_SCANCODE_PERIOD:
      return SDLK_PERIOD;
    case SDL_SCANCODE_SLASH:
      return SDLK_SLASH;
    case SDL_SCANCODE_NONUSBACKSLASH:
      return 0;
    default:
      return std::nullopt;
  }
}

// Returns the word of the character key `event` (see the header comment);
// `us_key` is the US key at its position (0 for none).
std::optional<std::uint16_t> layout_key_word(const InputEvent& event, std::uint32_t us_key) {
  const std::uint16_t mods = event.mods;
  const bool altgr_key = (mods & (SDL_KMOD_RALT | SDL_KMOD_MODE)) != 0;
  if (altgr_key && printable_ascii(event.altgr_character)) {
    return us_character_word(event.altgr_character);
  }
  const std::optional<std::uint32_t> letter = letter_index(event.character);
  // The words of the physical key with a modifier.
  const auto physical = [&](std::uint16_t modifier) {
    return us_key == 0 ? std::nullopt : bios_key_word(us_key, modifier);
  };
  if ((mods & (SDL_KMOD_ALT | SDL_KMOD_MODE)) != 0) {
    return letter ? std::optional<std::uint16_t>(letter_word(*letter, 0)) : physical(SDL_KMOD_LALT);
  }
  if ((mods & SDL_KMOD_CTRL) != 0) {
    return letter ? std::optional<std::uint16_t>(letter_word(*letter, *letter + 1)) : physical(SDL_KMOD_LCTRL);
  }
  if (letter) {
    const bool upper = ((mods & SDL_KMOD_SHIFT) != 0) != ((mods & SDL_KMOD_CAPS) != 0);
    return letter_word(*letter, (upper ? 'A' : 'a') + *letter);
  }
  if (printable_ascii(event.character)) {
    return us_character_word(event.character);
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::uint16_t> bios_key_word(std::uint32_t key, std::uint16_t mods) {
  const bool shift = (mods & SDL_KMOD_SHIFT) != 0;
  const bool ctrl = (mods & SDL_KMOD_CTRL) != 0;
  const bool alt = (mods & SDL_KMOD_ALT) != 0;
  if (key >= SDLK_A && key <= SDLK_Z) {
    const std::uint32_t letter = key - SDLK_A;
    if (alt) {
      return letter_word(letter, 0);
    }
    if (ctrl) {
      return letter_word(letter, letter + 1);
    }
    const bool upper = shift != ((mods & SDL_KMOD_CAPS) != 0);
    return letter_word(letter, (upper ? 'A' : 'a') + letter);
  }
  for (const KeypadWords& k : kKeypad) {
    if (k.key == key) {
      if (alt) {
        // Alt with the keypad digits composes a character code: no word
        // until Alt is released (not emulated).
        return std::nullopt;
      }
      if (ctrl) {
        return k.ctrl;
      }
      const bool digits = shift != ((mods & SDL_KMOD_NUM) != 0);
      return digits ? k.digit : k.cursor;
    }
  }
  for (const KeyWords& k : kKeys) {
    if (k.key == key) {
      return word_or_none(alt ? k.alt : ctrl ? k.ctrl : shift ? k.shift : k.plain);
    }
  }
  // A character the US keyboard types with Shift (a script's '+').
  if (!alt && !ctrl && printable_ascii(key)) {
    return us_character_word(key);
  }
  return std::nullopt;
}

std::optional<std::uint16_t> bios_key_word(const InputEvent& event) {
  if (event.type == EventType::kText) {
    return bios_character_word(event.character);
  }
  if (event.type != EventType::kKeyDown) {
    return std::nullopt;
  }
  if (event.scancode != SDL_SCANCODE_UNKNOWN) {
    if (const std::optional<std::uint32_t> us_key = us_character_key(event.scancode)) {
      return layout_key_word(event, *us_key);
    }
  }
  return bios_key_word(event.key, event.mods);
}

bool is_back_key(const InputEvent& event) { return event.type == EventType::kKeyDown && event.key == SDLK_AC_BACK; }

std::optional<std::uint16_t> bios_character_word(char32_t c) {
  if (!printable_ascii(c)) {
    return std::nullopt;
  }
  return us_character_word(c);
}

std::vector<InputEvent> text_input_events(std::string_view utf8) {
  std::vector<InputEvent> events;
  for (const char byte : utf8) {
    // The bytes of a multi-byte sequence all have the high bit set.
    const auto c = static_cast<char32_t>(static_cast<unsigned char>(byte));
    if (printable_ascii(c)) {
      InputEvent e;
      e.type = EventType::kText;
      e.key = c;
      e.character = c;
      events.push_back(e);
    }
  }
  return events;
}

bool key_types_text(const InputEvent& event) {
  if (event.type != EventType::kKeyDown) {
    return false;
  }
  const std::uint16_t mods = event.mods;
  const bool altgr = (mods & (SDL_KMOD_RALT | SDL_KMOD_MODE)) != 0;
  if (altgr && printable_ascii(event.altgr_character)) {
    return true;
  }
  if ((mods & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_MODE)) != 0) {
    return false;
  }
  switch (event.key) {
    case SDLK_KP_DIVIDE:
    case SDLK_KP_MULTIPLY:
    case SDLK_KP_MINUS:
    case SDLK_KP_PLUS:
      return true;
    default:
      break;
  }
  for (const KeypadWords& k : kKeypad) {
    if (k.key == event.key) {
      return (mods & SDL_KMOD_NUM) != 0 && (mods & SDL_KMOD_SHIFT) == 0;
    }
  }
  // Keys with a scancode type the layout's character; synthesized ones
  // their key code. Non-ASCII characters count: their text is dropped.
  const char32_t c = event.scancode != SDL_SCANCODE_UNKNOWN ? event.character : event.key;
  return c >= 0x20 && c != 0x7f && (c & SDLK_SCANCODE_MASK) == 0;
}

std::uint8_t bios_shift_state(std::uint16_t mods) {
  std::uint8_t s = 0;
  s |= (mods & SDL_KMOD_RSHIFT) != 0 ? 0x01 : 0;
  s |= (mods & SDL_KMOD_LSHIFT) != 0 ? 0x02 : 0;
  s |= (mods & SDL_KMOD_CTRL) != 0 ? 0x04 : 0;
  s |= (mods & SDL_KMOD_ALT) != 0 ? 0x08 : 0;
  s |= (mods & SDL_KMOD_SCROLL) != 0 ? 0x10 : 0;
  s |= (mods & SDL_KMOD_NUM) != 0 ? 0x20 : 0;
  s |= (mods & SDL_KMOD_CAPS) != 0 ? 0x40 : 0;
  return s;
}

}  // namespace sh
