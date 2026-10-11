// The pictures of the floating command button and its panel
// (platform/command_panel.h), made at run time from the install's art (no
// game art is stored with the program): the TDC's tube fire buttons
// (TBTTDC.SHP, with their numbers), and for the five depth orders one of
// several designs, each drawn from the game's art in its palettes.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/install.h"
#include "game/shpath.h"
#include "platform/command_panel.h"
#include "sim/player_combat.h"

namespace sh {

// The designs of a depth order's icon.
enum class DepthIconStyle : std::uint8_t {
  // The command bar's submarine (GLOBAL.SHP) at the order's depth against
  // a waterline, tilted with the game's arrows for crash dive and blow.
  kSilhouette,
  // The same, closer: the conning tower area at twice the size.
  kSilhouetteClose,
  // The conn's depth gauge (GAUGES.PCX, its needle dial template in
  // SH.EXE) showing the order's depth, with an arrow for crash dive and
  // blow.
  kGauge,
  // The order's hotkey letter in the main menu's brass font (MENU_H.FNT)
  // on a dark plate.
  kPlate,
};
inline constexpr std::size_t kDepthIconStyleCount = 4;

// The designs of the floating button.
enum class ButtonIconStyle : std::uint8_t {
  // The torpedo selector knob of the TDC (TBTTDC.SHP).
  kKnob,
  // The command bar's submarine in a brass ring.
  kBrassRing,
  // In the same ring, the command bar's pictures (GLOBAL.SHP): the 1MC
  // microphone, the helm wheel, the periscope eyepieces, the torpedo.
  kMicrophone,
  kHelm,
  kEyepieces,
  kTorpedo,
  // The TDC's FIRE cursor (CURSORS.SHP) in the ring.
  kFireCursor,
  // The conn's engine telegraph dial (GAUGES.PCX) in the ring.
  kTelegraph,
  // "ORDERS" on a riveted plate like the depth orders' plates.
  kOrdersPlate,
};
inline constexpr std::size_t kButtonIconStyleCount = 9;

// The design of every depth order (in `PanelCommand` order) and of the
// button: the defaults below are what the game uses; change them here.
struct CommandIconDesign {
  std::array<DepthIconStyle, kDepthCommandCount> depth{DepthIconStyle::kPlate, DepthIconStyle::kPlate,
                                                       DepthIconStyle::kPlate, DepthIconStyle::kPlate,
                                                       DepthIconStyle::kPlate};
  ButtonIconStyle button = ButtonIconStyle::kOrdersPlate;
};

// A tube's torpedo type for the touch panel's label strip (not in the
// original): the WEAPONS.DAT id (0: empty), whether it is still loading,
// and the type's label ("18-2").
struct TubeMark {
  std::uint32_t weapon_id = 0;
  bool loading = false;
  std::string label;

  friend bool operator==(const TubeMark&, const TubeMark&) = default;
};

// The tube buttons of the panel.
inline constexpr std::size_t kTubeIconCount = kPanelCommandCount - kDepthCommandCount;

// Returns the marks of the tubes of `combat` (empty past its tubes); the
// label is the type's name in its TDC table less "MARK" (not in the
// original).
std::array<TubeMark, kTubeIconCount> tube_marks(const PlayerCombat& combat);

// The art the icons are drawn from, read from an install.
class CommandIconArt {
 public:
  // Reads the sprites, pictures, fonts and the depth dial template.
  // Throws `DataError` when a file is missing or malformed.
  CommandIconArt(const Install& install, const ShPaths& paths);
  ~CommandIconArt();
  CommandIconArt(CommandIconArt&&) noexcept;
  CommandIconArt& operator=(CommandIconArt&&) noexcept;

  // Returns the icon of the depth order `command` in `style`.
  RgbaImage depth_icon(PanelCommand command, DepthIconStyle style) const;
  // Returns the fire button of tube `tube` (0..9): ready, or pressed (the
  // fired frame).
  RgbaImage tube_icon(std::size_t tube, bool pressed) const;
  // Returns the floating button in `style`.
  RgbaImage button_icon(ButtonIconStyle style) const;
  // Returns the time compression button "+" (`up`) or "-": a riveted
  // plate like the depth orders'.
  RgbaImage compression_icon(bool up) const;

 private:
  struct Data;
  std::unique_ptr<Data> data_;
};

// Returns every picture of the widget in `design`.
CommandIcons build_command_icons(const CommandIconArt& art, const CommandIconDesign& design = {});

// Returns a contact sheet of every design: one row per depth order (each
// style, left to right in `DepthIconStyle` order), then the tube buttons
// (ready and pressed) and the button styles, each icon scaled by `zoom`
// on a dark background.
RgbaImage command_icon_sheet(const CommandIconArt& art, int zoom);
// Returns a contact sheet of the floating button's styles (in
// `ButtonIconStyle` order), each at its size and enlarged, over a dark
// and over a light background.
RgbaImage command_button_sheet(const CommandIconArt& art);

}  // namespace sh
