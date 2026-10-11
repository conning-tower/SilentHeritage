#include "platform/display.h"

#include <SDL3/SDL.h>

#include <deque>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/edition.h"
#include "platform/bios_keys.h"
#include "platform/command_panel.h"
#include "platform/command_panel_view.h"

namespace sh {

namespace {

[[noreturn]] void sdl_fail(std::string_view what) {
  throw std::runtime_error(std::format("{}: {}", what, SDL_GetError()));
}

MouseButton to_button(std::uint8_t b) {
  switch (b) {
    case SDL_BUTTON_LEFT:
      return MouseButton::kLeft;
    case SDL_BUTTON_RIGHT:
      return MouseButton::kRight;
    case SDL_BUTTON_MIDDLE:
      return MouseButton::kMiddle;
    default:
      return MouseButton::kNone;
  }
}

#ifndef SDL_PLATFORM_ANDROID
// Whether `key` is the fullscreen toggle: Alt with either Enter key.
bool is_fullscreen_toggle(const SDL_KeyboardEvent& key) {
  return (key.key == SDLK_RETURN || key.key == SDLK_KP_ENTER) && (key.mod & SDL_KMOD_ALT) != 0;
}
#endif

}  // namespace

ScreenRect letterbox_rect(int window_w, int window_h) {
  // Integer products: the 4:3 test is exact.
  if (window_w * kScreenHeight > window_h * kScreenWidth) {
    const int w = window_h * kScreenWidth / kScreenHeight;
    return {(window_w - w) / 2, 0, w, window_h};
  }
  const int h = window_w * kScreenHeight / kScreenWidth;
  return {0, (window_h - h) / 2, window_w, h};
}

struct Display::Impl {
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;
  std::vector<std::uint32_t> argb = std::vector<std::uint32_t>(static_cast<std::size_t>(kScreenWidth) * kScreenHeight);
  // The characters of a text input event not returned yet.
  std::deque<InputEvent> pending;
  bool text_input = false;
  // The floating command button, if shown.
  CommandPanel* panel = nullptr;
  std::unique_ptr<CommandPanelView> panel_view;
  bool panel_shown = true;
  bool fullscreen = false;
  // Where the fullscreen state is saved (empty: not saved).
  std::filesystem::path fullscreen_file;
  // The Enter key of the Alt+Enter being held: its repeats and release are
  // dropped.
  std::optional<SDL_Keycode> toggle_key;

  // Confines the mouse to the picture while fullscreen, frees it otherwise.
  void confine_mouse() {
#ifndef SDL_PLATFORM_ANDROID  // always fullscreen, the pointer a finger
    if (!fullscreen) {
      SDL_SetWindowMouseRect(window, nullptr);
      return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);
    const ScreenRect r = letterbox_rect(w, h);
    const SDL_Rect rect{r.x, r.y, r.w, r.h};
    if (!SDL_SetWindowMouseRect(window, &rect)) {
      SDL_Log("%s: cannot confine the mouse: %s", edition::kExecutable, SDL_GetError());
    }
#endif
  }

  void set_fullscreen(bool on) {
    if (!SDL_SetWindowFullscreen(window, on)) {
      SDL_Log("%s: cannot change the fullscreen state: %s", edition::kExecutable, SDL_GetError());
      return;
    }
    fullscreen = on;
    confine_mouse();
    if (!fullscreen_file.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(fullscreen_file.parent_path(), ec);
      std::ofstream out(fullscreen_file, std::ios::trunc);
      out << (on ? 1 : 0) << '\n';
      if (!out.flush()) {
        SDL_Log("%s: cannot save the fullscreen state", edition::kExecutable);
      }
    }
  }

  // Gives the command button the window's current output size and display
  // scale: they change with a resize, a fold or unfold, a move to another
  // display or a rotation, before any touch.
  void sync_panel_window() {
    if (panel == nullptr) {
      return;
    }
    int w = 0;
    int h = 0;
    SDL_GetRenderOutputSize(renderer, &w, &h);
    if (w > 0 && h > 0) {
      panel->set_window(static_cast<float>(w), static_cast<float>(h), SDL_GetWindowDisplayScale(window));
    }
  }

  // Shows the picture's texture and the command button.
  void render() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, nullptr, nullptr);
    if (panel_view && panel_shown) {
      sync_panel_window();
      panel_view->draw(*panel);
    }
    SDL_RenderPresent(renderer);
  }

  // Passes a mouse event `e` (window coordinates) to the command button;
  // returns its result, or nothing when the event is not its own.
  std::optional<PanelInput> panel_input(const SDL_Event& e) {
    if (panel == nullptr || !panel_shown) {
      return std::nullopt;
    }
    sync_panel_window();
    // Window points to output pixels.
    const float density = SDL_GetWindowPixelDensity(window);
    PanelInput in;
    if (e.type == SDL_EVENT_MOUSE_MOTION) {
      in = panel->move(e.motion.x * density, e.motion.y * density);
    } else if (e.button.button != SDL_BUTTON_LEFT) {
      in = panel->other_button(e.button.x * density, e.button.y * density);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
      in = panel->press(e.button.x * density, e.button.y * density);
    } else {
      in = panel->release(e.button.x * density, e.button.y * density);
    }
    if (in.redraw) {
      render();
    }
    if (in.save_failed) {
      SDL_Log("%s: cannot save the command button's place", edition::kExecutable);
    }
    return in;
  }
};

Display::Display(std::string_view title, int scale, Scaling scaling) : impl_(std::make_unique<Impl>()) {
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
#ifdef SDL_PLATFORM_ANDROID
  // Landscape only (either way up); the resizable window follows the
  // screen as a foldable folds and unfolds. Fullscreen hides the system
  // bars (immersive mode). A finger is the left mouse button.
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
  flags |= SDL_WINDOW_FULLSCREEN;
  impl_->fullscreen = true;
#endif
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    sdl_fail("SDL_InitSubSystem(VIDEO)");
  }
  const std::string t(title);
  if (!SDL_CreateWindowAndRenderer(t.c_str(), kScreenWidth * scale, kScreenHeight * scale, flags, &impl_->window,
                                   &impl_->renderer)) {
    sdl_fail("SDL_CreateWindowAndRenderer");
  }
  if (!SDL_SetRenderLogicalPresentation(impl_->renderer, kScreenWidth, kScreenHeight,
                                        SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
    sdl_fail("SDL_SetRenderLogicalPresentation");
  }
  SDL_SetRenderVSync(impl_->renderer, 1);
  impl_->texture = SDL_CreateTexture(impl_->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING,
                                     kScreenWidth, kScreenHeight);
  if (impl_->texture == nullptr) {
    sdl_fail("SDL_CreateTexture");
  }
  SDL_SetTextureScaleMode(impl_->texture,
                          scaling == Scaling::kSharp ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);
  // The game draws its own mouse. SDL only hides the cursor over its own
  // windows, so the OS cursor stays visible outside.
  SDL_HideCursor();
}

Display::~Display() {
  impl_->panel_view.reset();
  SDL_DestroyTexture(impl_->texture);
  SDL_DestroyRenderer(impl_->renderer);
  SDL_DestroyWindow(impl_->window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void Display::present(const Image8& frame, const Palette& palette) {
  if (frame.width() != kScreenWidth || frame.height() != kScreenHeight) {
    throw std::invalid_argument("Display::present: frame must be 640x480");
  }
  std::array<std::uint32_t, 256> lut{};
  for (std::size_t i = 0; i < 256; ++i) {
    const Rgb c = palette.colors[i];
    lut[i] = (std::uint32_t{c.r} << 16) | (std::uint32_t{c.g} << 8) | c.b;
  }
  const auto px = frame.pixels();
  for (std::size_t i = 0; i < px.size(); ++i) {
    impl_->argb[i] = lut[px[i]];
  }
  SDL_UpdateTexture(impl_->texture, nullptr, impl_->argb.data(), kScreenWidth * 4);
  impl_->render();
}

std::optional<InputEvent> Display::poll() {
  if (!impl_->pending.empty()) {
    const InputEvent e = impl_->pending.front();
    impl_->pending.pop_front();
    return e;
  }
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      if (const std::optional<PanelInput> in = impl_->panel_input(e); in && in->consumed) {
        if (in->command) {
          // Typed like a key of the keyboard: down, then up.
          const PanelKey k = panel_command_key(*in->command);
          InputEvent down;
          down.type = EventType::kKeyDown;
          down.key = k.key;
          down.mods = k.mods;
          InputEvent up = down;
          up.type = EventType::kKeyUp;
          up.mods = 0;
          impl_->pending.push_back(up);
          return down;
        }
        continue;
      }
    }
    SDL_ConvertEventToRenderCoordinates(impl_->renderer, &e);
    InputEvent out;
    switch (e.type) {
      case SDL_EVENT_QUIT:
        out.type = EventType::kQuit;
        return out;
      case SDL_EVENT_WINDOW_FOCUS_LOST:
        out.type = EventType::kFocusLost;
        return out;
      case SDL_EVENT_WINDOW_FOCUS_GAINED:
        out.type = EventType::kFocusGained;
        return out;
      case SDL_EVENT_TEXT_INPUT: {
        if (!impl_->text_input) {
          break;
        }
        std::vector<InputEvent> chars = text_input_events(e.text.text);
        if (chars.empty()) {
          break;
        }
        impl_->pending.insert(impl_->pending.end(), chars.begin() + 1, chars.end());
        return chars.front();
      }
      case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
      case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        // Also when the window manager changes it.
        impl_->fullscreen = e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        impl_->confine_mouse();
        break;
      case SDL_EVENT_WINDOW_RESIZED:
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
        impl_->confine_mouse();
        impl_->sync_panel_window();
        break;
      case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      case SDL_EVENT_DISPLAY_ORIENTATION:
      case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
        impl_->sync_panel_window();
        break;
      case SDL_EVENT_KEY_DOWN:
      case SDL_EVENT_KEY_UP:
#ifndef SDL_PLATFORM_ANDROID
        if (e.type == SDL_EVENT_KEY_DOWN && is_fullscreen_toggle(e.key)) {
          if (!e.key.repeat) {
            impl_->set_fullscreen(!impl_->fullscreen);
          }
          impl_->toggle_key = e.key.key;
          break;
        }
        if (e.type == SDL_EVENT_KEY_UP && impl_->toggle_key == e.key.key) {
          impl_->toggle_key.reset();
          break;
        }
#endif
        out.type = e.type == SDL_EVENT_KEY_DOWN ? EventType::kKeyDown : EventType::kKeyUp;
        out.key = e.key.key;
        out.mods = e.key.mod;
        out.repeat = e.key.repeat;
        out.scancode = e.key.scancode;
        if (e.key.scancode != SDL_SCANCODE_UNKNOWN) {
          // The layout's own characters: the event keycode applies the
          // SDL_HINT_KEYCODE_OPTIONS (French layouts report their number
          // row as digits) and no modifiers.
          const auto level = static_cast<SDL_Keymod>(e.key.mod & (SDL_KMOD_SHIFT | SDL_KMOD_CAPS));
          out.character = SDL_GetKeyFromScancode(e.key.scancode, level, false);
          const char32_t altgr =
              SDL_GetKeyFromScancode(e.key.scancode, static_cast<SDL_Keymod>(level | SDL_KMOD_MODE), false);
          out.altgr_character = altgr != out.character ? altgr : 0;
        }
        if (impl_->text_input && key_types_text(out)) {
          // Its character comes as SDL_EVENT_TEXT_INPUT (Android's soft
          // keyboard sends both for ASCII).
          break;
        }
        return out;
      case SDL_EVENT_MOUSE_MOTION:
        out.type = EventType::kMouseMove;
        out.x = static_cast<int>(e.motion.x);
        out.y = static_cast<int>(e.motion.y);
        return out;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
      case SDL_EVENT_MOUSE_BUTTON_UP:
        out.type = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::kMouseDown : EventType::kMouseUp;
        out.x = static_cast<int>(e.button.x);
        out.y = static_cast<int>(e.button.y);
        out.button = to_button(e.button.button);
        return out;
      case SDL_EVENT_MOUSE_WHEEL:
        out.type = EventType::kMouseWheel;
        // Whole notches (a touchpad's fractions accumulated by SDL), away
        // from the user positive.
        out.wheel = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.integer_y : e.wheel.integer_y;
        return out;
      default:
        break;
    }
  }
  return std::nullopt;
}

void Display::begin_text_input(const ScreenRect& area, TextInputKind kind) {
  // The field's corners through the letterbox to window coordinates.
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(area.x), static_cast<float>(area.y), &x0, &y0);
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(area.x + area.w),
                                static_cast<float>(area.y + area.h), &x1, &y1);
  const SDL_Rect rect{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
  SDL_SetTextInputArea(impl_->window, &rect, 0);
  const SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                        kind == TextInputKind::kDigits ? SDL_TEXTINPUT_TYPE_NUMBER : SDL_TEXTINPUT_TYPE_TEXT);
  // The game edits the text itself: no suggestions rewriting what was typed.
  SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
  if (!SDL_StartTextInputWithProperties(impl_->window, props)) {
    SDL_Log("%s: no text input: %s", edition::kExecutable, SDL_GetError());
  }
  SDL_DestroyProperties(props);
  impl_->text_input = true;
}

void Display::end_text_input() {
  SDL_StopTextInput(impl_->window);
  impl_->text_input = false;
  impl_->pending.clear();
}

void Display::set_command_panel(CommandPanel& panel, const CommandIcons& icons) {
  impl_->panel_view = std::make_unique<CommandPanelView>(impl_->renderer, icons);
  impl_->panel = &panel;
  impl_->sync_panel_window();
}

void Display::open_command_panel_above(const ScreenRect& area) {
  if (impl_->panel == nullptr) {
    return;
  }
  impl_->sync_panel_window();
  float x0 = 0;
  float y0 = 0;
  float x1 = 0;
  float y1 = 0;
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(area.x), static_cast<float>(area.y), &x0, &y0);
  SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(area.x + area.w),
                                static_cast<float>(area.y + area.h), &x1, &y1);
  // Window points to output pixels.
  const float density = SDL_GetWindowPixelDensity(impl_->window);
  impl_->panel->open_above({x0 * density, y0 * density, (x1 - x0) * density, (y1 - y0) * density});
}

void Display::show_command_panel(bool shown) {
  if (impl_->panel != nullptr && !shown) {
    impl_->panel->close();
  }
  impl_->panel_shown = shown;
}

bool Display::fullscreen() const { return impl_->fullscreen; }

void Display::set_fullscreen(bool fullscreen) {
#ifndef SDL_PLATFORM_ANDROID
  impl_->set_fullscreen(fullscreen);
#else
  (void)fullscreen;
#endif
}

void Display::remember_fullscreen(std::filesystem::path file) {
#ifndef SDL_PLATFORM_ANDROID
  int saved = 0;
  if (std::ifstream in(file); in >> saved && saved == 1) {
    impl_->set_fullscreen(true);
  }
  impl_->fullscreen_file = std::move(file);
#else
  (void)file;
#endif
}

std::uint64_t Display::ticks_ms() { return SDL_GetTicks(); }

void Display::delay_ms(std::uint32_t ms) { SDL_Delay(ms); }

}  // namespace sh
