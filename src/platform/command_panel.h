// The floating command button of touch screens (no keyboard): a group
// floating over the window, draggable as one piece, of the ORDERS button
// (its tap opens a panel of command icons) above the time compression "+"
// and "-" buttons. Each button and icon types the key the original game
// binds to the command (the command bar's hotkey table,
// game/command_bar.cpp), so the game sees exactly what a keyboard sends.
//
// It lives in window space: its geometry is in the renderer's output pixels
// of the whole window (not the 640x480 game picture), sized in dp from the
// window's display scale. `CommandPanel` is the widget's state and input
// handling, free of SDL; `CommandPanelView` (platform/command_panel_view.h)
// draws it.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sh {

// The commands of the panel, in panel order.
enum class PanelCommand : std::uint8_t {
  kPeriscopeDepth,
  kRadarDepth,
  kSurface,
  kCrashDive,
  kBlowBallast,
  kFireTube1,
  kFireTube2,
  kFireTube3,
  kFireTube4,
  kFireTube5,
  kFireTube6,
  kFireTube7,
  kFireTube8,
  kFireTube9,
  kFireTube10,
  // The buttons under ORDERS.
  kCompressionUp,
  kCompressionDown,
};
// The commands of the panel's icons (the first ones).
inline constexpr std::size_t kPanelCommandCount = 15;
inline constexpr std::size_t kDepthCommandCount = 5;

// The live state the panel shows (extended `panel_status`, not in the
// original), read from the game: the depth order in effect lights its
// plate, each tube's state is its frame and lamp. A tube's `label` is the
// text under it (the torpedo type, for the torpedo loadouts), the state
// keeps the frame and lamp.
enum class TubeState : std::uint8_t {
  kAbsent,     // the boat has no such tube: hidden, its taps ignored
  kEmpty,      // grey
  kReloading,  // amber, with the time left
  kLoaded,     // green
  kDamaged,    // red
};
struct TubeStatus {
  TubeState state = TubeState::kAbsent;
  // The seconds left of a reload.
  std::int32_t reload_seconds = 0;
  std::string label;
  friend bool operator==(const TubeStatus&, const TubeStatus&) = default;
};
inline constexpr std::size_t kPanelTubeCount = 10;
struct PanelStatus {
  // The depth command whose order is in effect (one of the first
  // `kDepthCommandCount` commands), if any.
  std::optional<PanelCommand> depth_order;
  std::array<TubeStatus, kPanelTubeCount> tubes{};
  friend bool operator==(const PanelStatus&, const PanelStatus&) = default;
};

// A key as `InputEvent` carries it: the SDL keycode and modifier mask.
struct PanelKey {
  std::uint32_t key = 0;
  std::uint16_t mods = 0;
};

// Returns the key typed for `command`: 'p', 'r', 's', 'c', 'b' for the depth
// orders (hotkeys 0xff50, 0xff52, 0xff53, 0xff43, 0xff42, matching the
// letter in either case), Alt-1 .. Alt-9, Alt-0 for tubes 1..10 (words
// 0x7800..0x8100), and '+' and '-' for the time compression (the US
// keyboard's 0x0d2b and 0x0c2d, matching hotkeys 0xfe2b and 0xfe2d).
PanelKey panel_command_key(PanelCommand command);

// A straight-alpha ARGB image (0xAARRGGBB), row-major.
struct RgbaImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint32_t> pixels;

  RgbaImage() = default;
  RgbaImage(int w, int h, std::uint32_t fill = 0)
      : width(w), height(h), pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill) {}
  std::uint32_t at(int x, int y) const {
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
  }
  std::uint32_t& at(int x, int y) {
    return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
  }
};

// The pictures of the widget: the ORDERS button, the "+" and "-" buttons
// (and their pressed look), and per panel command its icon and the icon
// shown while it is pressed.
struct CommandIcons {
  RgbaImage button;
  std::array<RgbaImage, 2> compression;
  std::array<RgbaImage, 2> compression_pressed;
  std::array<RgbaImage, kPanelCommandCount> icons;
  std::array<RgbaImage, kPanelCommandCount> pressed;
};

// A rectangle in window pixels.
struct PanelRect {
  float x = 0;
  float y = 0;
  float w = 0;
  float h = 0;
  bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// What a pointer event did to the widget.
struct PanelInput {
  // The event belongs to the widget and must not reach the game.
  bool consumed = false;
  // The widget's look changed: the window needs redrawing.
  bool redraw = false;
  // A command whose key to type.
  std::optional<PanelCommand> command;
  // The button was moved but its place could not be saved.
  bool save_failed = false;
};

// The floating button and its panel. Sizes are in dp: one dp is the
// window's display scale in pixels.
class CommandPanel {
 public:
  // The group: ORDERS across its width, "+" and "-" side by side below,
  // each 44 dp square (the smallest comfortable touch target).
  static constexpr float kGroupWidthDp = 88;
  static constexpr float kOrdersHeightDp = 40;
  static constexpr float kCompressionHeightDp = 44;
  static constexpr float kIconDp = 56;     // a command's touch target
  static constexpr float kGapDp = 6;       // between icons
  static constexpr float kPaddingDp = 8;   // inside the panel
  static constexpr float kGroupGapDp = 10; // extra gap between depth orders and tubes
  static constexpr float kMarginDp = 8;    // between the button and the panel
  // A press moving farther than this is a drag of the button, not a tap.
  static constexpr float kDragThresholdDp = 10;
  static constexpr int kColumns = 5;

  // The button at its default place (right edge, below the middle), or at
  // the one saved in `position_file` (empty: not persisted).
  explicit CommandPanel(std::filesystem::path position_file = {});

  // Sets the window's size in pixels and its display scale (pixels per
  // dp); the button keeps its relative place.
  void set_window(float width, float height, float scale);

  // Left button (finger) press, motion and release at (x, y) window pixels.
  // A press on the group starts a tap or, past the threshold, a drag (moves
  // the group; the place is saved on release). A tap on ORDERS toggles the
  // panel; a tap on "+" or "-" gives its command. With the panel open, a press and release on one icon gives
  // its command (the panel stays open); a press elsewhere in the panel does
  // nothing, and a press outside closes it. Every event of a press the
  // widget took (until its release) is consumed; the others are not.
  PanelInput press(float x, float y);
  PanelInput move(float x, float y);
  PanelInput release(float x, float y);
  // Another mouse button's press or release: consumed over the widget.
  PanelInput other_button(float x, float y) const;

  // Closes the panel and forgets a press in progress (its motion and
  // release then reach the game); returns whether anything changed.
  bool close();

  // Sets the live state shown (none: the plain panel); returns whether
  // the look changed. A reload's progress is measured against the longest
  // time left seen since it started.
  bool set_status(std::optional<PanelStatus> status);
  const std::optional<PanelStatus>& status() const { return status_; }
  // The part of tube `tube`'s reload done, 0..1 (0 when not reloading).
  float reload_progress(std::size_t tube) const;

  bool is_open() const { return open_; }
  // The whole group, and its parts: ORDERS, then "+" (right half) and "-"
  // (left half) below it.
  PanelRect group_rect() const;
  PanelRect button_rect() const;
  std::array<PanelRect, 2> compression_rects() const;
  // The "+" or "-" button under a press that is still down (not dragged).
  std::optional<PanelCommand> compression_pressed() const;
  // The panel's rectangle and its icons' squares, in `PanelCommand` order
  // (valid while open).
  PanelRect panel_rect() const;
  std::array<PanelRect, kPanelCommandCount> icon_rects() const;
  // The icon under a press that is still down, if any.
  std::optional<PanelCommand> pressed() const;
  // Whether the group is being dragged.
  bool dragging() const { return state_ == State::kDrag; }

  // The group's centre as fractions of the window size.
  float rel_x() const { return rel_x_; }
  float rel_y() const { return rel_y_; }

 private:
  enum class State : std::uint8_t { kIdle, kButtonPress, kDrag, kIconPress, kSwallow };

  float dp() const { return scale_; }
  // The panel's size at the current scale, shrunk to fit the window.
  float panel_scale() const;
  // Writes the group's place to `position_file_`; returns false on failure.
  bool save() const;
  std::optional<PanelCommand> icon_at(float x, float y) const;

  std::filesystem::path position_file_;
  float width_ = 640;
  float height_ = 480;
  float scale_ = 1;
  float rel_x_ = 0;
  float rel_y_ = 0;
  bool open_ = false;
  State state_ = State::kIdle;
  float press_x_ = 0;
  float press_y_ = 0;
  // The group centre's offset from the press point while dragging.
  float grab_dx_ = 0;
  float grab_dy_ = 0;
  std::optional<PanelCommand> icon_;
  bool icon_hover_ = false;
  // The group's part pressed: none for ORDERS, else "+" or "-".
  std::optional<PanelCommand> part_;
  std::optional<PanelStatus> status_;
  // The longest reload time seen per tube while it reloads.
  std::array<std::int32_t, kPanelTubeCount> reload_total_{};
};

}  // namespace sh
