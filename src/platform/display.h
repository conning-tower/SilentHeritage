// The SDL3 window showing the 640x480 indexed picture, input events
// (including the system's text input and the application's lifecycle).
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "core/image.h"
#include "core/side_fill.h"
#include "platform/command_panel.h"

namespace sh {

class CommandPanel;
struct CommandIcons;

enum class EventType : std::uint8_t {
  kQuit,
  kKeyDown,
  kKeyUp,
  kMouseMove,
  kMouseDown,
  kMouseUp,
  kMouseWheel,
  // A character of the system's text input (a soft keyboard, an IME, the
  // keyboard while text input is on): its printable ASCII code in
  // `character` (and `key`).
  kText,
  // The window lost or gained the input focus. (The application's
  // lifecycle events reach only a `LifecycleWatch`.)
  kFocusLost,
  kFocusGained,
  // A finger touched, moved on or left a touch screen: `finger`, at (x, y).
  // The first finger also arrives as the left mouse button.
  kFingerDown,
  kFingerMove,
  kFingerUp,
};

enum class MouseButton : std::uint8_t { kNone, kLeft, kRight, kMiddle };

// An input event. Mouse coordinates are in 640x480 screen pixels (the
// letterbox and scaling are removed; they may fall outside the screen).
// `key` is the SDL keycode (printable keys are their lower-case ASCII code);
// `mods` the SDL modifier mask. Key events from the keyboard also carry the
// physical key and what the keyboard layout types with it; synthesized events
// (scripts) leave `scancode` 0 and are read as keys of a US keyboard.
struct InputEvent {
  EventType type = EventType::kQuit;
  std::uint32_t key = 0;
  std::uint16_t mods = 0;
  // The SDL scancode (the physical key, named after its US position).
  std::uint32_t scancode = 0;
  // The code point the layout types with the key under the current Shift
  // and Caps Lock state (SDL keycode values: non-character keys have
  // `SDLK_SCANCODE_MASK`).
  char32_t character = 0;
  // The code point the layout types with the key and AltGr under the same
  // Shift state, 0 when AltGr does not change the character.
  char32_t altgr_character = 0;
  bool repeat = false;
  int x = 0;
  int y = 0;
  MouseButton button = MouseButton::kNone;
  int wheel = 0;
  std::int64_t finger = 0;
};

// How the 640x480 picture is scaled to the window.
enum class Scaling : std::uint8_t {
  kSmooth,  // bilinear, the softer look of DOSBox's scalers (the default)
  kSharp,   // nearest neighbour: the original pixels
};

// A rectangle of the 640x480 screen.
struct ScreenRect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool operator==(const ScreenRect&) const = default;
};

// Returns the part of a `window_w` x `window_h` window (any unit) a
// `screen_w` x 480 picture (4:3 by default) is drawn in, as
// SDL_LOGICAL_PRESENTATION_LETTERBOX places it: the window's full height or
// width, centred between black bars.
ScreenRect letterbox_rect(int window_w, int window_h, int screen_w = kScreenWidth);

// Returns the colour modulation of the side fill textures for a frame shown
// with `palette`: per channel, the palette's brightest value against the
// brightest ever seen (`brightest`, updated), so that the fill fades with
// the frame and takes the colour of rig for red. Not in the original.
std::array<std::uint8_t, 3> side_fill_tint(const Palette& palette, std::array<std::uint8_t, 3>& brightest);

// Returns the width of the wide screen (480 rows) that fills a `window_w` x
// `window_h` window: the window's aspect, between 640 (4:3) and
// `kMaxScreenWidth` (21:9). Not in the original.
int wide_screen_width(int window_w, int window_h);

// The kinds of text a field takes, which pick the soft keyboard.
enum class TextInputKind : std::uint8_t { kText, kDigits };

// The game window. The indexed frame is converted through the palette and
// scaled to the window keeping its 4:3 aspect ratio (black bars). Only one
// display may exist; it initializes and shuts down SDL video.
class Display {
 public:
  // Opens a window `scale` times the original size (or `window_w` x
  // `window_h` when given). `wide`: the screen follows the window's aspect
  // (`screen_width`) and narrower frames are shown centred between the side
  // fill; else it is 640x480 letterboxed. Throws std::runtime_error if SDL
  // fails.
  Display(std::string_view title, int scale = 2, Scaling scaling = Scaling::kSmooth, bool wide = false,
          int window_w = 0, int window_h = 0);
  ~Display();
  Display(const Display&) = delete;
  Display& operator=(const Display&) = delete;

  // Shows `frame` with `palette`. The frame is 480 rows of 640 up to
  // `kMaxScreenWidth` pixels, shown centred on the screen; input
  // coordinates are those of the last frame shown.
  void present(const Image8& frame, const Palette& palette);

  // Saves the next picture shown, as the window shows it (with the side
  // fill, at the window's size), to the BMP file `file`. Failures are
  // logged.
  void capture_next(std::filesystem::path file);

  // Returns the width of the screen the window shows (640 unless wide):
  // the width a wide layout draws at.
  int screen_width() const;
  // Sets the picture of a texture kind of side fill (`kBrass`, `kWood`),
  // repeated across the screen with one copy under the frame. Throws
  // std::runtime_error if SDL cannot make its texture.
  void set_side_texture(SideFill::Kind kind, const RgbaImage& picture);
  // Sets what fills the screen beside a narrower frame from the next
  // `present` on: a texture tinted after the frame's palette
  // (`side_fill_tint`), or the screen's own pictures through it. The
  // brass at first.
  void set_side_fill(const SideFill& fill);

  // Returns the next pending input event, if any. The window's close
  // button and the system ending the program (SDL_EVENT_QUIT) give
  // `kQuit`. While text input is on,
  // the typed characters arrive as `kText` events and the key downs typing
  // them are dropped (`key_types_text`).
  std::optional<InputEvent> poll();

  // Turns the system's text input on (showing the soft keyboard where
  // there is one) for a field of `kind` at `area` (screen coordinates, so
  // that the system keeps it in view), or shows the keyboard again when
  // it is already on.
  void begin_text_input(const ScreenRect& area, TextInputKind kind);
  // Turns it off (hiding the soft keyboard).
  void end_text_input();

  // Shows `panel` (not owned; must outlive the display), drawn with
  // `icons`, over the picture in window space. The left button (finger)
  // presses it takes, with their motion and release, and the other
  // buttons' presses over it never reach `poll`'s caller; each command it
  // gives arrives as the key down and key up of its key
  // (`panel_command_key`, without scancode). Throws std::runtime_error if
  // SDL cannot make its textures.
  void set_command_panel(CommandPanel& panel, const CommandIcons& icons);
  // Shows or hides the command panel set (shown at first). Hiding it
  // closes it; hidden, it is not drawn and takes no input.
  void show_command_panel(bool shown);
  // Opens the command panel above `area` of the 640x480 picture (the
  // command bar's ORDERS button).
  void open_command_panel_above(const ScreenRect& area);

  // Whether the window is fullscreen (borderless, at the desktop's
  // resolution). On the desktop Alt+Enter (either Enter key) toggles it,
  // as in DOSBox: `poll` takes the key down, its repeats and its release,
  // which never reach the caller. While fullscreen, the mouse is confined
  // to the picture (`letterbox_rect` of the window). Android windows are
  // always fullscreen, without the toggle.
  bool fullscreen() const;
  void set_fullscreen(bool fullscreen);
  // Restores the fullscreen state saved in `file`, and saves it there at
  // each change from now on (a missing or unreadable file: windowed).
  void remember_fullscreen(std::filesystem::path file);

  // Returns milliseconds since SDL initialization.
  static std::uint64_t ticks_ms();
  static void delay_ms(std::uint32_t ms);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace sh
