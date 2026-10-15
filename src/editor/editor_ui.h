// The editor's screen and GUI state, as `se_main`'s start-up leaves it
// (`load_cursors`, `load_gui_fonts` / `gui_init` config 0x981f4, the extra
// fonts and SBCHART.SHP of `sb_load_data`): the 640x480 frame, the screen
// palette and the DAC, the dialog toolkit with the editor's skin, and the
// palette helpers the dialogs use. The chart screen, the toolbox and every
// dialog of the editor draw through one `EditorUi`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "assets/fnt.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "core/image.h"
#include "editor/se_strings.h"
#include "game/frontend_host.h"
#include "ui/default_skin.h"
#include "ui/dialog.h"

namespace sh {
class Install;
}

namespace sh::editor {

// SBCHART.SHP frames used by the dialogs (docs/editor-assets.md).
namespace sbchart_frame {
inline constexpr std::size_t kShipPictureBackground = 29;
inline constexpr std::size_t kShipPicturePalette = 30;
inline constexpr std::size_t kSubPicturePalette = 31;
inline constexpr std::size_t kAircraftPictureBackground = 32;
inline constexpr std::size_t kAircraftPicturePalette = 33;
inline constexpr std::size_t kWeatherFirst = 34;  // Clear .. Overcast
inline constexpr std::size_t kMoonFirst = 38;     // moon phase 0 (full) .. 4 (new)
inline constexpr std::size_t kAboutLogo = 44;
}  // namespace sbchart_frame

// The default skin of the editor's `gui_init`, with what the dialogs change
// around a run: the list metrics 0x9bc60..0x9bc66 (the picture dialogs show
// six rows) and the list text font and offset 0x9bc68 / 0x9bc6c (PATROL AREA
// lists in FONT1).
class EditorSkin : public ui::DefaultSkin {
 public:
  using ui::DefaultSkin::DefaultSkin;

  ui::SkinMetrics& mutable_metrics() { return metrics_; }
  // The list row font (GUI_S when null).
  const Font* list_font = nullptr;

  ui::Rect list_item(const Viewport& gfx, int x, int y, int w, std::string_view text, bool selected) override;
};

// Sets the list metrics of `skin` for the life of the scope, then puts the
// previous ones back (the picture dialogs: 6 rows, 100 pixels, track 43).
class ListMetricsScope {
 public:
  ListMetricsScope(EditorSkin& skin, int rows, int height, int track);
  ~ListMetricsScope();
  ListMetricsScope(const ListMetricsScope&) = delete;
  ListMetricsScope& operator=(const ListMetricsScope&) = delete;

 private:
  EditorSkin& skin_;
  ui::SkinMetrics saved_;
};

class EditorUi {
 public:
  // The first colour `palette_grey_out` changes.
  static constexpr int kFirstGreyedColour = 9;

  // Loads the GUI fonts (GUI_S, GUI_L, GUI_M, GUI_1), GUISPR.SHP, FONT1,
  // MSG and MENU_S, SBCHART.SHP and SBCURSOR.SHP, and shows the pointer.
  // `install`, `se` and `host` must outlive the object. Throws `DataError`
  // if a file is missing.
  EditorUi(const Install& install, const SeExe& se, FrontendHost& host);
  EditorUi(const EditorUi&) = delete;
  EditorUi& operator=(const EditorUi&) = delete;

  const Install& install() const { return install_; }
  const SeExe& se() const { return se_; }
  FrontendHost& host() { return host_; }
  Image8& screen() { return screen_; }
  ui::Gui& gui() { return gui_; }
  EditorSkin& skin() { return skin_; }
  ui::GuiResources& resources() { return resources_; }

  // The screen palette 0xa4af0 (where `sprite_apply_palette` writes) and
  // the displayed VGA DAC.
  VgaPalette& palette() { return palette_; }
  VgaPalette& dac() { return dac_; }

  // The fonts and sprites of `sb_load_data`: FONT1.FNT (0xa5050), MSG.FNT
  // (0xa5054), MENU_S.FNT (0xa5058), SBCHART.SHP (0xa505c), SBCURSOR.SHP.
  const Font& font1() const { return font1_; }
  const Font& msg_font() const { return msg_font_; }
  const Font& menu_font() const { return menu_font_; }
  const Shp& sbchart() const { return sbchart_; }
  const Shp& cursors() const { return cursors_; }

  // Returns the SE.EXE string at `address` / the `count` strings of the
  // `char*` table at `table`.
  std::string str(std::uint32_t address) const { return std::string(se_.str(address)); }
  std::vector<std::string> strings(std::uint32_t table, std::size_t count) const;

  // One displayed frame of the modal loops: draws the cursor, presents the
  // screen with the DAC, reads the input.
  void frame();

  // `palette_grey_out(percent, last)`: DAC colours 9..`last` are saved and
  // replaced by a grey of `percent` of their lightness.
  void palette_grey_out(int percent, int last);
  // `palette_restore_saved(last)`: puts the saved colours 9..`last` back.
  void palette_restore_saved(int last);
  // `palette_fade_out(delay, step)` / `palette_fade_in(target, delay,
  // step)` on the DAC, one frame per step.
  void palette_fade_out(std::uint32_t delay, int step);
  void palette_fade_in(const VgaPalette& target, std::uint32_t delay, int step);
  // Copies the screen palette's colours `first`..`last` into the DAC (the
  // DAC hook 0x9feb8 called per entry).
  void set_dac_from_palette(int first, int last);
  // Copies the colours `first`..`last` of `frame`'s own palette into the
  // screen palette and the DAC (`sprite_apply_palette` into a copy, then
  // the range copied out, as the picture dialogs do).
  void apply_frame_colours(const SpriteFrame& frame, int first, int last);
  // The same into the DAC only (the About logo).
  void set_dac_from_frame(const SpriteFrame& frame, int first, int last);
  // Returns the screen palette with `frame`'s own colours applied
  // (`sprite_apply_palette` into a copy).
  VgaPalette frame_colours(const SpriteFrame& frame) const;

 private:
  const Install& install_;
  const SeExe& se_;
  FrontendHost& host_;
  Image8 screen_;
  ui::GuiResources resources_;
  Font font1_;
  Font msg_font_;
  Font menu_font_;
  Shp sbchart_;
  Shp cursors_;
  EditorSkin skin_;
  ui::Gui gui_;
  VgaPalette palette_{};
  VgaPalette dac_{};
  VgaPalette grey_saved_{};
};

// A modal dialog of the editor, placed at (`x`, `y`) of the original's
// 640-wide screen: on a wider screen (the wide chart,
// `Features::wide_screen`; not in the original) it is centred with that
// frame (`ui::Gui::frame_left`).
class EditorDialog : public ui::Dialog {
 public:
  EditorDialog(EditorUi& ui, int x, int y, int w, int h, unsigned flags)
      : ui::Dialog(ui.gui(), ui.gui().frame_left() + x, y, w, h, flags) {}
};

}  // namespace sh::editor
