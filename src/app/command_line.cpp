#include "app/command_line.h"

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <stdexcept>

#include "core/edition.h"

namespace sh {

namespace {

// Returns the error of a bad command line: the usage.
std::runtime_error usage_error() {
  return std::runtime_error(std::format("usage: {} {}", edition::kExecutable, kUsageOptions));
}

struct NamedKey {
  std::string_view name;
  std::uint32_t key;
};

constexpr NamedKey kKeyNames[] = {
    {"up", SDLK_UP},     {"down", SDLK_DOWN},         {"left", SDLK_LEFT},        {"right", SDLK_RIGHT},
    {"pgup", SDLK_PAGEUP}, {"pgdn", SDLK_PAGEDOWN},   {"home", SDLK_HOME},        {"end", SDLK_END},
    {"enter", SDLK_RETURN}, {"esc", SDLK_ESCAPE},     {"tab", SDLK_TAB},          {"space", SDLK_SPACE},
    {"backspace", SDLK_BACKSPACE}, {"comma", SDLK_COMMA}, {"minus", SDLK_MINUS},  {"period", SDLK_PERIOD},
    {"kpplus", SDLK_KP_PLUS}, {"kpminus", SDLK_KP_MINUS},
    {"f1", SDLK_F1},     {"f2", SDLK_F2},             {"f3", SDLK_F3},            {"f4", SDLK_F4},
    {"f5", SDLK_F5},     {"f6", SDLK_F6},             {"f7", SDLK_F7},            {"f8", SDLK_F8},
    {"f9", SDLK_F9},     {"f10", SDLK_F10},           {"f11", SDLK_F11},          {"f12", SDLK_F12},
};

// Returns the key-down event named `spec` (see the file comment).
InputEvent parse_key(std::string_view spec) {
  InputEvent e{.type = EventType::kKeyDown};
  constexpr std::string_view kShift = "shift+";
  constexpr std::string_view kAlt = "alt+";
  if (spec.starts_with(kShift)) {
    e.mods = SDL_KMOD_LSHIFT;
    spec.remove_prefix(kShift.size());
  } else if (spec.starts_with(kAlt)) {
    e.mods = SDL_KMOD_LALT;
    spec.remove_prefix(kAlt.size());
  }
  for (const NamedKey& k : kKeyNames) {
    if (k.name == spec) {
      e.key = k.key;
      return e;
    }
  }
  if (spec.size() == 1) {
    // SDL keycodes of letters are lower case: an upper-case letter is the
    // key with Shift.
    if (spec[0] >= 'A' && spec[0] <= 'Z') {
      e.key = static_cast<std::uint8_t>(spec[0] - 'A' + 'a');
      e.mods = SDL_KMOD_LSHIFT;
      return e;
    }
    e.key = static_cast<std::uint8_t>(spec[0]);
    return e;
  }
  throw std::runtime_error(std::format("unknown key name '{}'", spec));
}

// Appends the press and release of `button` at "X:Y" (`at`).
void add_click(std::vector<InputEvent>& keys, std::string_view key, std::string_view at, MouseButton button) {
  const std::size_t colon = at.find(':');
  int x = 0;
  int y = 0;
  if (colon == std::string_view::npos || std::from_chars(at.data(), at.data() + colon, x).ec != std::errc{} ||
      std::from_chars(at.data() + colon + 1, at.data() + at.size(), y).ec != std::errc{}) {
    throw std::runtime_error(std::format("bad click '{}'", key));
  }
  for (const EventType type : {EventType::kMouseDown, EventType::kMouseUp}) {
    keys.push_back(InputEvent{.type = type, .x = x, .y = y, .button = button});
  }
}

}  // namespace

CommandLine parse_command_line(std::span<const std::string_view> args) {
  CommandLine o;
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view a = args[i];
    const bool has_value = i + 1 < args.size();
    if (a == "--viewer") {
      o.viewer = true;
      continue;
    }
    if (a == "--nointro") {
      o.intro = false;
      continue;
    }
    if (a == "--editor") {
      o.editor = true;
      continue;
    }
    if (a == "--sharp") {
      o.scaling = Scaling::kSharp;
      continue;
    }
    if (a == "--touch-panel") {
      o.touch_panel = true;
      continue;
    }
    if (a == "--no-orders-button") {
      o.no_orders_button = true;
      continue;
    }
    if (a == "--classic") {
      o.classic = true;
      continue;
    }
    if (a == "--setup") {
      o.setup = true;
      continue;
    }
    if (!has_value) {
      throw usage_error();
    }
    const std::string_view value = args[++i];
    if (a == "--data") {
      o.data_dir = value;
    } else if (a == "--open") {
      o.open = value;
    } else if (a == "--save") {
      o.save_dir = value;
    } else if (a == "--screenshot") {
      o.screenshot = value;
    } else if (a == "--window") {
      const std::size_t x = value.find('x');
      if (x == std::string_view::npos ||
          std::from_chars(value.data(), value.data() + x, o.window_width).ec != std::errc{} ||
          std::from_chars(value.data() + x + 1, value.data() + value.size(), o.window_height).ec != std::errc{} ||
          o.window_width < 1 || o.window_height < 1) {
        throw std::runtime_error("--window needs a size WxH");
      }
    } else if (a == "--frames") {
      if (std::from_chars(value.data(), value.data() + value.size(), o.frames).ec != std::errc{} || o.frames < 1) {
        throw std::runtime_error("--frames needs a positive number");
      }
    } else if (a == "--keys") {
      std::size_t pos = 0;
      while (pos <= value.size()) {
        const std::size_t comma = std::min(value.find(',', pos), value.size());
        const std::string_view key = value.substr(pos, comma - pos);
        if (key.starts_with("click:")) {
          add_click(o.keys, key, key.substr(6), MouseButton::kLeft);
        } else if (key.starts_with("rclick:")) {
          add_click(o.keys, key, key.substr(7), MouseButton::kRight);
        } else if (key.starts_with('-')) {
          int frames = 1;
          if (key.size() > 1 &&
              std::from_chars(key.data() + 1, key.data() + key.size(), frames).ec != std::errc{}) {
            throw std::runtime_error(std::format("bad key wait '{}'", key));
          }
          // A key release does nothing: an idle frame.
          o.keys.insert(o.keys.end(), static_cast<std::size_t>(frames), InputEvent{.type = EventType::kKeyUp});
        } else {
          o.keys.push_back(parse_key(key));
        }
        pos = comma + 1;
      }
    } else {
      throw usage_error();
    }
  }
  if (o.viewer && o.editor) {
    throw usage_error();
  }
  return o;
}

}  // namespace sh
