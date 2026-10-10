// Translation of SDL keys to the BIOS keyboard words the game reads from
// the BIOS keyboard buffer (`input_timer_isr`, 0x39020): `scan << 8 |
// ascii` as INT 16h function 10h returns them for a US enhanced keyboard
// (gray cursor keys with ASCII 0xe0, keypad 5 as 0x4cf0, Alt combinations
// with ASCII 0), and the BIOS shift state byte (0x417).
//
// Keyboard events follow the active layout: a character key gives the word
// a US keyboard gives for the character the layout types (the scan code of
// the US key typing it), so that the hotkeys matching a character (0xffXX,
// 0xfeXX) and the exact words of the digits (the telegraph) fire on the
// character printed on the key: '+' on a BEPO top-row key gives 0x0d2b
// (time compression), Shift with it types '7' and gives 0x0837. With Alt
// and Ctrl, a key typing a letter gives the Alt / Ctrl word of that letter
// and any other key the word of its physical position, like the BIOS under
// a DOS national keyboard driver (KEYB FR): Alt with the top-row key below
// a digit is Alt-digit (fires that tube) on AZERTY and BEPO as on a US
// keyboard. AltGr (right Alt on layouts that have it) types the layout's
// AltGr character when it is ASCII, and otherwise acts as Alt. Characters
// outside ASCII give no word (the game takes ASCII only). The non-character
// keys (F-keys, cursor block, keypad, Esc, Enter, Tab, Backspace, Space)
// keep their scan codes; Android's Back key (SDLK_AC_BACK) gives the Esc words.
//
// Text typed through the system's text input (a soft keyboard, an IME)
// arrives as `EventType::kText` characters, which give the word a US
// keyboard gives for the character.
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "platform/display.h"

namespace sh {

// Returns the BIOS keyboard word of the SDL key `key` of a US keyboard
// pressed with the SDL modifiers `mods` (Alt takes precedence over Ctrl,
// Ctrl over Shift; Caps Lock and Num Lock apply to letters and the keypad),
// or nothing for a key that puts no word in the buffer (a lone modifier, an
// unmapped key, combinations the BIOS ignores such as Ctrl-1). A printable
// ASCII `key` that the US keyboard types with Shift (such as '+') gives the
// word of that shifted key.
std::optional<std::uint16_t> bios_key_word(std::uint32_t key, std::uint16_t mods);

// Returns the BIOS word of a `EventType::kKeyDown` event (repeats
// included, like the typematic repeat) or of a `EventType::kText`
// character, nothing for other events. Key events with a scancode follow
// the layout (see the file comment), synthesized ones without are read by
// `bios_key_word(key, mods)`.
std::optional<std::uint16_t> bios_key_word(const InputEvent& event);

// Returns the word a US keyboard gives for typing the character `c`
// (Shift included for the shifted characters), nothing outside printable
// ASCII.
std::optional<std::uint16_t> bios_character_word(char32_t c);

// Returns the `EventType::kText` events of the UTF-8 text of an
// SDL_EVENT_TEXT_INPUT: one per printable ASCII character, the other
// characters (control codes, any non-ASCII sequence) dropped.
std::vector<InputEvent> text_input_events(std::string_view utf8);

// Returns whether the `EventType::kKeyDown` event `event` types a
// character, which the system's text input delivers as text when it is
// on: a key typing a printable character with no Ctrl or Alt (AltGr
// typing a character counts), the keypad's operators, and its digits and
// point under Num Lock without Shift. While text input is on, these key
// downs are dropped so that each character arrives once; the other keys
// (Enter, Backspace, Esc, the arrows, Ctrl and Alt combinations) stay key
// events.
bool key_types_text(const InputEvent& event);

// Returns the BIOS shift state byte (0x417) for the SDL modifiers: bit 0
// right Shift, 1 left Shift, 2 Ctrl, 3 Alt, 4 Scroll Lock, 5 Num Lock,
// 6 Caps Lock.
std::uint8_t bios_shift_state(std::uint16_t mods);

}  // namespace sh
