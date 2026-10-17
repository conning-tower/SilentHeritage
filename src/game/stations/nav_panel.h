// The shared navigation panel (module gauges, dialog 0x11c704 built by
// `gauges_panel_open` 0x39ed5): the compass with the ordered course, the
// engine telegraph, the helm, the speed and depth dials and the course
// engage button that the gauges station, the chart (NAVIGATION panel), the
// bridge, the periscope and the TBT show, each from its own template of
// positions and sprites in SH.EXE (and its own `.SHP`).
//
// The dials are the guiuser dial controls (`needle_dial_proc` 0x4978c,
// `compass_dial_proc` 0x49c1d, `telegraph_dial_proc` 0x4a3e3,
// `rudder_dial_proc` 0x4a666, `cycle_button_proc` 0x49472 and the pitch
// bubble 0x4a95c). The original composes each dial over the station
// picture kept in the screen bitmap and shows the result on the VGA only;
// here the panel keeps that picture (`background`) and draws the dials onto
// the screen.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "assets/exe_image.h"
#include "assets/shp.h"
#include "core/image.h"
#include "game/station_env.h"
#include "game/stations/attack_services.h"
#include "gfx/text.h"
#include "gfx/viewport.h"
#include "ui/controls.h"
#include "ui/dial_scrub.h"
#include "ui/dialog.h"

namespace sh {

// The screens that show the panel.
enum class NavPanelSite : std::uint8_t { kBridge, kScope, kTbt, kChart };

// A needle dial (0x58 bytes, e.g. the gauges depth dial 0x108670).
// Coordinates are those of the panel dialog.
struct NeedleDial {
  int cx = 0, cy = 0;              // +0x00 the needle's pivot
  int digits_x = 0, digits_y = 0;  // +0x08
  std::int32_t arc_start = 0;      // +0x10 degrees of value 0
  std::int32_t arc_end = 0;        // +0x14 degrees of `max`
  std::int32_t max = 0;            // +0x18
  std::int32_t digits = 0;         // +0x1c digit count, negative: the value / 10; 0 none
  ui::Rect rect;                   // +0x20 the area redrawn
  std::int32_t needle_frame = 0;   // +0x34
  std::int32_t marker_frame = 0;   // +0x38 the ordered value's needle
  // +0x54 drawn over the pivot (-1 none); `gauges_panel_open` sets it to -1
  // for the speed and depth dials, whose templates hold 0.
  std::int32_t cap_frame = -1;

  // Reads the template at `address` of SH.EXE.
  static NeedleDial read(const ExeImage& exe, std::uint32_t address);
};

// Returns the needle angle (tenths of a degree, 0..3599) of `value` on
// `dial` (`needle_dial_draw` 0x49866): the arc span plus 1 degree, in
// hundredths per unit, from the start angle.
std::int32_t needle_angle(const NeedleDial& dial, std::int32_t value);
// Returns the value a click at `angle` (tenths, from north) selects
// (`needle_dial_proc`): beyond the arc it snaps to `max` below 180 deg,
// else to 0.
std::int32_t needle_value_at(const NeedleDial& dial, std::int32_t angle);
// Returns the angle (tenths, from north, clockwise) of the screen point
// (x, y) seen from (cx, cy), as the dials read the mouse
// (`heading_from_delta(y - cy - 1, x - cx - 1) + 90 deg`).
std::int32_t dial_angle(int x, int y, int cx, int cy);

// Draws the needle dial `dial` into `out` (dialog coordinates), composed
// over `background` (same coordinates): the digits of `digit_value` in
// `digits_font` through `colours` (the template's table at +0x3c), the
// marker at `ordered` (none when unset) and the needle at `value`.
void draw_needle_dial(const Viewport& out, const Image8& background, const NeedleDial& dial, const Shp& sprites,
                      const Font& digits_font, const GlyphColourTable& colours, std::int32_t value,
                      std::optional<std::int32_t> ordered, std::int32_t digit_value);

// The compass (0x58 bytes, e.g. 0x108618).
struct CompassDial {
  int cx = 0, cy = 0;
  int digits_x = 0, digits_y = 0;
  int lamp_x = 0, lamp_y = 0;       // +0x10 the course-mode lamp
  std::int32_t digits = 0;          // +0x18
  ui::Rect rect;                    // +0x1c
  std::int32_t card_frame = 0;      // +0x30 turned by the heading
  std::int32_t ordered_frame = 0;   // +0x34 turned by the ordered course
  std::int32_t lamp_on = -1;        // +0x38 course mode on
  std::int32_t lamp_off = -1;       // +0x3c
  static CompassDial read(const ExeImage& exe, std::uint32_t address);
};

// The engine telegraph (0x30 bytes, e.g. 0x108720): the order's sprite and
// a click map frame whose colour - 1 is the order clicked.
struct TelegraphDial {
  int x = 0, y = 0;
  ui::Rect rect;                  // +0x08
  std::int32_t base_frame = 0;    // +0x1c frame of order 0
  std::int32_t mask_frame = 0;    // +0x20
  static TelegraphDial read(const ExeImage& exe, std::uint32_t address);
};
// Returns the telegraph sprite frame shown for `order` (silent running
// shows ahead 1/3).
std::int32_t telegraph_frame(const TelegraphDial& dial, std::int32_t order);

// The helm strip (0x24 bytes, e.g. 0x108750): 15 zones (table 0x109330)
// for the helm steps -7..7; the selected zone's colours are swapped.
struct RudderDial {
  ui::Rect rect;
  std::array<std::uint8_t, 3> from{};  // +0x10, +0x12, +0x14
  std::array<std::uint8_t, 3> to{};    // +0x11, +0x13, +0x15
  static RudderDial read(const ExeImage& exe, std::uint32_t address);
};
// Returns the 15 zones {x, width} of the helm strip (SH.EXE 0x109330),
// relative to it.
std::array<std::array<int, 2>, 15> rudder_zones();
// Returns the helm step -7..7 of a click at `x` (relative to the strip),
// or nothing outside the zones.
std::optional<std::int32_t> rudder_step_at(int x);

// The course engage button (0x28 bytes, e.g. 0x1085f0).
struct EngageButton {
  ui::Rect rect;
  std::int32_t frame = 0;          // +0x14
  std::int32_t pressed_frame = 0;  // +0x18
  std::int32_t repeat = 0;         // +0x1c
  static EngageButton read(const ExeImage& exe, std::uint32_t address);
};

// The pitch bubble (0x30 bytes, e.g. 0x108774): the bubble moves along a
// table (0x1093a8) by the boat's pitch in degrees, 0..15 either way.
struct PitchDial {
  int x = 0, y = 0;
  ui::Rect rect;                    // +0x0c
  std::int32_t glass_frame = 0;     // +0x20 drawn over
  std::int32_t bubble_frame = 0;    // +0x24
  static PitchDial read(const ExeImage& exe, std::uint32_t address);
};
// The bubble's offset {x, y} and turn (tenths) for a pitch of `tenths`
// (0..3599), or nothing beyond 15 degrees.
struct BubblePlace {
  int dx = 0;
  int dy = 0;
  std::int32_t angle = 0;
};
std::optional<BubblePlace> pitch_bubble(std::int32_t tenths);

// A panel's template: the controls of `gauges_panel_open`'s arguments.
struct NavTemplate {
  CompassDial compass;
  TelegraphDial telegraph;
  RudderDial rudder;
  NeedleDial speed;
  NeedleDial depth;
  std::optional<EngageButton> engage;
  std::optional<PitchDial> pitch;
  // The test depth printed in BEARING.FNT on the station picture.
  std::optional<std::array<int, 2>> test_depth_at;
  std::uint8_t test_depth_colour = 0;
  std::string sprites;  // the ART .SHP of the site
  // The table the counters' DIGITS.FNT glyphs are drawn through (the
  // compass +0x40 and dials +0x3c pointer).
  GlyphColourTable digit_colours{};
};

// Returns the glyph colour table of the counters of `site`: the BSS table
// its station screen patches (bridge 0x11627c by `bridge_screen`, scope
// 0x126648 by `scope_screen`, TBT 0x1282f4 by `tbt_screen`, chart 0x117744
// by `chart_screen`), mapping DIGITS.FNT's cell shades 0xc0..0xca and digit
// shades 0xe1..0xef to the site's palette; other entries are 0 (black)
// and only some sites keep 0xff transparent. The bridge and TBT tapes
// share their site's table.
const GlyphColourTable& nav_digit_colours(NavPanelSite site);
// Returns the template of `site`, or of the gauges station.
NavTemplate nav_template(const ExeImage& exe, NavPanelSite site);
NavTemplate gauges_nav_template(const ExeImage& exe);

// The dialog a site shows the panel in (the `param_12` of
// `gauges_panel_open`); the full screen at (0, 0) when not given.
struct NavPanelFrame {
  int x = 0x16;
  int y = 0x59;
  int w = 0x10b;
  int h = 0x167;
  unsigned flags = ui::kDialogFramed;
  // Adds the frame's own controls before the dials (the chart's panel
  // frame and title bar).
  std::function<void(ui::Dialog&)> add_controls;
  // The picture the dials are composed over (0x11dfc8), in dialog
  // coordinates; null: the screen under the dialog when it opens.
  const Image8* background = nullptr;
};

class NavPanel : public StationPanel {
 public:
  // The panel of `layout` for `env` (which must outlive it), in `frame`
  // or full screen.
  NavPanel(StationEnv& env, NavTemplate layout, std::optional<NavPanelFrame> frame = std::nullopt);
  ~NavPanel() override;

  // `gauges_panel_open`: loads the site's sprites, prints the test depth,
  // takes the background and opens the dialog.
  void open() override;
  void event(const ui::Event& event) override;  // gauges_panel_event
  void process() override;                      // gauges_panel_process
  // `gauges_panel_update` (0x3a456): marks the dials whose values changed
  // (all when `full_redraw`), applies a depth ordered on the dial and says
  // a new course ("HELMnnnn.RAW").
  void update(bool full_redraw) override;
  void close() override;  // gauges_panel_close
  bool capturing() const override { return scrub_ && scrub_->capturing(); }

  // Sets the station's own controls (not in the original: the gauges'
  // SOUND), added after the dials when the panel opens.
  void set_station_controls(std::function<void(ui::Dialog&)> add) { station_controls_ = std::move(add); }

  // The dialog while open (the chart moves it), else null.
  ui::Dialog* dialog() { return dialog_.get(); }
  const NavTemplate& layout() const { return layout_; }
  // The depth ordered on the dial (0x11c67c), -1 none.
  std::int32_t dial_ordered_depth() const { return ordered_depth_; }
  // The precise setting of the compass, helm and depth dials (extended
  // `precise_dials`), null when off.
  const ui::DialScrubber* scrubber() const { return scrub_.get(); }

 private:
  class DialControl;
  // The original presses of the compass, helm and depth dials at `e`;
  // each returns whether the order changed.
  bool click_compass(const ui::Event& e);
  bool click_rudder(const ui::Event& e);
  bool click_depth(const ui::Event& e);
  // Registers the dials with `scrub_` (screen rectangles kept by
  // `place_scrub_targets`, the chart moving the panel).
  void add_scrub_targets();
  void place_scrub_targets();
  // Draws the readout of `text` in `out` inside `rect` (dialog
  // coordinates), on the side away from the press.
  void draw_readout(const Viewport& out, const ui::Rect& rect, const std::string& text);
  void draw_compass(const Viewport& out);
  void draw_telegraph(const Viewport& out);
  void draw_rudder(const Viewport& out);
  void draw_speed(const Viewport& out);
  void draw_depth(const Viewport& out);
  void draw_engage(const Viewport& out, bool pressed);
  void draw_pitch(const Viewport& out);
  void engage();  // gauges_compass_engage (0x3a6ea)

  StationEnv& env_;
  NavTemplate layout_;
  std::optional<NavPanelFrame> frame_;
  std::function<void(ui::Dialog&)> station_controls_;
  std::unique_ptr<ui::Dialog> dialog_;
  Shp sprites_;
  Image8 background_;
  Image8 telegraph_mask_;
  std::array<DialControl*, 7> controls_{};
  std::int32_t ordered_depth_ = -1;  // 0x11c67c
  std::int32_t shown_depth_ = 0;     // 0x11c680
  // The values last shown (0x11c96c..0x11c990).
  std::int32_t last_heading_ = -1, last_course_ = -1, last_telegraph_ = -1, last_helm_ = 99;
  std::int32_t last_speed_ = 0, last_log_ = -1, last_ordered_ = -2, last_depth_ = -1, last_pitch_ = -1;
  bool last_course_mode_ = false;
  std::int32_t engage_count_ = 0;
  // Extended `precise_dials`: the scrubber, its dials' indices (compass,
  // helm, depth) and the ghosts' colour.
  std::unique_ptr<ui::DialScrubber> scrub_;
  std::array<std::size_t, 3> scrub_index_{};
  std::uint8_t ghost_ = 0;
};

// Returns the panel of `site` (the site's control template and sprites);
// `env` must outlive it. `frame` places the chart's panel.
std::unique_ptr<NavPanel> make_nav_panel(StationEnv& env, NavPanelSite site,
                                         std::optional<NavPanelFrame> frame = std::nullopt);

}  // namespace sh
