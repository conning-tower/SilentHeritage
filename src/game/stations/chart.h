// The chart (module chart, station vtable 0x102bf8): the map of the patrol
// area (`chart_view`) with the own boat and, zoomed in to 8x and more, the
// ships around, the course plotted to the waypoints, and a movable panel:
// MAP CONTROLS (zoom, centre, follow, clock, scale), NAVIGATION (the shared
// nav panel) or the waypoint plotting controls. Pressing Z / X or the zoom
// buttons opens the zoom box, a magnifier the next click zooms to.
//
// Not in the original (chart_extended.cpp): in every build a finger
// dragging the map scrolls it, a tap acting on its release. With
// `Features::chart_tools`: the CHART TOOLS panel and its annotations
// (chart_tools.h), the continuous wheel and pinch zoom, the full command
// bar's panels, and the mouse dragging the map too (its clicks then act on
// the release).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/shp.h"
#include "game/station.h"
#include "game/station_env.h"
#include "game/stations/chart_panels.h"
#include "game/stations/chart_tools.h"
#include "game/stations/chart_view.h"
#include "game/stations/nav_panel.h"
#include "sim/tdc.h"
#include "sim/line_of_sight.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh {

class Aircraft;
class AirGroup;
class ContactGroups;
struct ContactGroup;
class Debris;
class DepthCharge;
struct MotionState;
class DownedPilot;
class EnemySub;
class IntelReport;
class Shell;
class Ship;
class Splash;
class TorpedoVehicle;
class Vehicle;
struct ShipClass;
struct ShipSightingRecord;
struct ShipSonar;

// The chart's panels (0x118a8c), -1 none; the toolbox is not in the
// original (`Features::chart_tools`).
enum class ChartPanel : std::int32_t { kNone = -1, kMap = 0, kNavigation = 1, kPlot = 2, kTools = 3 };

// What a click on the map does (0x117950).
enum class ChartClickMode : std::int32_t {
  kSelectTarget = 0,  // picks the TDC target under the pointer
  kPlot = 1,          // adds a waypoint
  kMoveSub = 2,       // the /SS305 cheat: moves the boat there
  kAnnotate = 3,      // uses the toolbox's tool (not in the original)
};

// The scale value and unit of the MAP CONTROLS panel for `zoom_int`
// (`chart_format_scale_value` 0x21799, `chart_draw_scale_legend` 0x217e4):
// 176000 / zoom yards, in tens of yards below 10000, else in statute
// miles.
std::string chart_scale_value(std::int32_t zoom_int);
std::string_view chart_scale_legend(std::int32_t zoom_int);

// Returns "<degrees>\x1a<minutes>\x1b<hemisphere>" (`chart_format_latlon`,
// 0x2a124; glyphs 0x1a / 0x1b are the degree and minute marks).
std::string chart_format_latlon(const GeoAngle& angle);

class ChartStation : public Station, private PanelSuspender {
 public:
  // The chart of `env.mission` (`chart_reset_view`, 0x20868): 1x, centred
  // on the boat, the MAP CONTROLS panel.
  explicit ChartStation(StationEnv& env);
  ~ChartStation() override;

  void open() override;     // chart_screen (0x20979)
  void event(const ui::Event& event) override;  // chart_station_event (0x20e8f)
  void process() override;  // chart_view_process (0x2a843)
  void update() override;   // chart_station_update (0x20f26)
  void close() override;    // chart_station_close (0x20de5)
  bool capturing() const override { return nav_panel_ && nav_panel_->capturing(); }
  bool wide_layout() const override { return true; }

  // The view and the state the tests look at.
  const ChartView& view() const { return view_; }
  ChartView& view() { return view_; }
  ChartPanel panel() const { return static_cast<ChartPanel>(panels_.requested()); }
  ChartClickMode click_mode() const { return click_mode_; }
  // The zoom box's zoom (0x118a78), 0 when closed.
  std::int32_t zoom_box() const { return zoom_box_.value(); }
  // The info popup's lines while it shows (the hover info), else empty.
  std::vector<std::string> popup_text() const { return popup_.text(); }
  // The panel's dialog position (map / plot panels share theirs).
  std::array<int, 2> panel_position() const { return {panel_pos_.x, panel_pos_.y}; }

  // Redraws the whole view on the next update (`0x118a6c`), e.g. after
  // the view was moved from outside.
  void invalidate() { dirty_ = true; }

  // The commands of the panels and keys.
  void zoom_box_grow();    // chart_zoom_box_grow (0x2bdc7)
  void zoom_box_shrink();  // chart_zoom_box_shrink (0x2be2c)
  void center_button();    // chart_button_center (0x21863)
  void toggle_follow();    // chart_toggle_follow_sub (0x21965)
  void start_plotting();   // chart_button_plot_course (0x21882)
  void exit_plotting();    // chart_button_exit_plot (0x21c6f)
  void clear_course();     // chart_button_clear_course (0x21caa)
  // `chart_add_waypoint` (0x211b1) at screen pixel (`x`, `y`): only in
  // water deeper than twice the draft (world units) with no land from the
  // previous point; returns whether it was added.
  bool add_waypoint(std::int32_t x, std::int32_t y);
  // `chart_on_click` (0x210f0).
  void click(std::int32_t x, std::int32_t y);

  // The annotation tools (not in the original; `Features::chart_tools`).
  // Opens / closes the CHART TOOLS panel and its click mode.
  void start_annotating();
  void exit_annotating();
  ChartTool tool() const { return tool_; }
  void select_tool(ChartTool tool);
  void select_ink(ChartInk ink);
  void select_symbol(ChartSymbol symbol);
  // UNDO: puts back the annotations before their last change.
  void undo_annotation();
  // CLEAR: erases every annotation once the player confirms.
  void clear_annotations();
  const ChartAnnotations& annotations() const { return annotations_; }
  // Replaces the annotations (a restored save).
  void restore_annotations(std::vector<ChartAnnotation> items);
  // The ruler shown (from, to), if any.
  const std::optional<std::array<geom::WorldXZ, 2>>& ruler() const { return ruler_; }
  // The line being drawn with the line tool, if any.
  const std::optional<ChartAnnotation>& draft() const { return draft_; }
  // Returns the toolbox's two prompt lines: how to use the tool, with the
  // mouse or on a touch screen, or the ruler's reading.
  std::array<std::string, 2> tools_prompt() const;
  // Zooms continuously by `notches` of the wheel (four double the zoom,
  // negative zooms out) about viewport pixel (`x`, `y`), which keeps its
  // chart position.
  void wheel_zoom(std::int32_t x, std::int32_t y, int notches);

 private:
  class SpriteButton;
  class FollowButton;
  class DrawnButton;
  // PanelSuspender
  void suspend_panels() override;  // chart_suspend_panels (0x21643)
  void resume_panels() override;   // chart_resume_panels (0x21662)

  void build_palette();
  // The open callbacks of the panel slots.
  void open_map_panel();
  void open_nav_panel();
  void open_plot_panel();
  void close_panel_dialogs();  // the panels' close callback
  void repaint_panel_dialog();  // the panels' redraw callback
  void panel_hover(bool over);  // chart_on_panel_hover (0x212cf)
  std::unique_ptr<ui::Dialog> make_map_panel();
  std::unique_ptr<ui::Dialog> make_plot_panel();
  // The extended chart (not in the original; chart_extended.cpp).
  void open_tools_panel();
  std::unique_ptr<ui::Dialog> make_tools_panel();
  // Adds the zoom, centre and follow buttons of the panels, `dx` pixels
  // right of their place.
  void add_zoom_buttons(ui::Dialog& d, int dx);
  // Whether the chart shows the whole command bar (the extended chart):
  // MAP CONTROLS and NAVIGATION then have no clock, time compression or
  // CONN, and TOOLS takes CONN's place.
  bool full_bar() const { return env_.ui.features.chart_tools; }
  // Returns the panels' dialog flags: framed and, in the extended chart,
  // with no focused control, so that Enter and Space press no panel button
  // (in the original the plotting panel's EXIT has the focus).
  unsigned panel_flags() const {
    return ui::kDialogFramed | (env_.ui.features.chart_tools ? ui::kDialogNoFocus : 0u);
  }
  // MAP CONTROLS with the full bar.
  std::unique_ptr<ui::Dialog> make_bar_map_panel();
  // Moves the panels' shared place left so that a panel `width` wide stays
  // on the map.
  void keep_panel_on_map(int width);
  // Adds the TOOLS button in `r`, a `w` x `h` plate, and the toolbox's
  // buttons that leave it for MAP CONTROLS or NAVIGATION at (`x`, `y`).
  void add_tools_button(ui::Dialog& d, ui::Rect r, int w, int h);
  void add_panel_switch(ui::Dialog& d, int x, int y, ChartPanel panel);
  // Has the toolbox's prompt redrawn.
  void tools_prompt_changed();
  // Whether the pointer is a finger (no hover, no right button).
  bool touch_screen() const;
  // The reach of a click on a drawing or on the line's last point (wider
  // for a finger).
  int pick_radius() const;
  // Returns whether viewport pixel (`x`, `y`) is on the last point of the
  // line being drawn.
  bool on_line_end(std::int32_t x, std::int32_t y) const;
  // Returns whether `event` was the extended chart's: the wheel, the
  // middle button on the map, or Esc and a right click in the toolbox.
  bool tools_event(const ui::Event& event);
  // A press of the left button on the map at viewport pixel (`x`, `y`) in
  // the extended chart, or of a finger in any (not in the original): it
  // clicks on the release unless it moves and scrolls the map.
  void press_map(std::int32_t x, std::int32_t y);
  // Every frame while the extended chart or a finger press needs it: the
  // pointer's click or drag, the pinch, the eraser's highlight and the
  // line tool's rubber band.
  void update_map_pointer();
  // Every frame on the extended chart while plotting or annotating: the
  // cross over the map, the ordinary pointer over the open panel and the
  // command bar (the original shows the cross over its bar, which has no
  // button). Left alone during a drag or a modal dialog.
  void update_tool_cursor();
  void tools_click(std::int32_t x, std::int32_t y);
  // Sets `zoom` keeping the chart point under viewport pixel (`x`, `y`),
  // or centred on the boat when following it.
  void zoom_about(std::int32_t x, std::int32_t y, std::int32_t zoom);
  // Two fingers on a touch screen zoom and scroll the map; returns whether
  // they do (the pointer then does nothing else).
  bool pinch();
  // A drag on the map: the tool's, else the scroll.
  void drag_map(ChartPointer::Step step, std::int32_t x, std::int32_t y);
  void finish_line();
  void ask_note(geom::WorldXZ at);
  // Draws the annotations under the overlays / the ruler and the drawing in
  // progress over them.
  void draw_annotations();
  void draw_tool_overlays();
  // The scale controls, from x 7 to `right`.
  void add_scale_controls(ui::Dialog& d, int line_y, int text_y, int right = 0x120);
  // Draws the panels' scale bar at `y`: CHART.SHP's, or with
  // `Features::chart_tools` the nautical one (`chart_nautical_scale`).
  void draw_scale_bar(const Viewport& gfx, int y, int right = 0x120) const;
  // The longest scale bar of controls ending at `right` (256 pixels in the
  // original's panels).
  static int scale_bar_length(int right) { return right - 0x20; }
  void repaint_panel();
  void view_update();       // chart_view_update (0x2a875)
  void redraw_map();        // chart_view_redraw_if_dirty (0x2abce)
  void redraw_overlays();   // chart_view_redraw_overlays (0x2b35c)
  // Returns the open panel's rectangle clipped to the map, if any.
  std::optional<ui::Rect> panel_on_map() const;
  // Returns whether the overlays just drawn over the clean map reach into
  // the open panel.
  bool overlays_reach_panel() const;
  bool poll();              // chart_view_poll (0x210be)
  void follow_sub();        // chart_follow_sub (0x21679)
  // Whether the view is held (0x118a80): by the zoom box or a panel drag.
  bool view_held() const { return zoom_hold_ || panels_.locked(); }
  void draw_contacts();     // chart_draw_contacts (0x21df4)
  void draw_own_sub(bool tactical);  // chart_draw_own_sub / player_chart_draw
  void draw_ship(Ship& ship);  // ship_chart_draw (0x22445)
  void draw_enemy_sub(const EnemySub& sub);  // enemy_sub_chart_draw (0x233d2)
  // The common body of the ship and enemy-sub draws: the class icon when
  // `seen`, the sinking mark, else the last-known marker; at the true
  // position or the last-known one. A ship's `sonar` beam pinged since the
  // last draw shows as a wedge under it and is cleared.
  void draw_hull_contact(const Vehicle& v, const ShipClass* cls, const ShipSightingRecord& record, bool seen,
                         bool true_position, bool sinking, ShipSonar* sonar = nullptr);
  // The weapons overlay of realism option 0x40 (realistic charts off).
  bool weapons_shown() const;
  // The sonar wedge of a ship at world (`wx`, `wz`), screen (`x`, `y`),
  // whose beam points to `bearing` (true, tenths).
  void draw_sonar_wedge(std::int32_t wx, std::int32_t wz, std::int32_t x, std::int32_t y, Angle10 bearing);
  void draw_torpedo(const TorpedoVehicle& torpedo);  // torpedo_chart_draw (0x24820)
  void draw_depth_charge(const DepthCharge& dc);     // dc_chart_draw (0x24aa1)
  void draw_shell(const Shell& shell);               // shell_chart_draw (0x24d73)
  void draw_splash(const Splash& splash);            // splash_chart_draw (0x24dee)
  void draw_debris(const Debris& debris);            // debris_chart_draw (0x250b3)
  void draw_plane_icon(std::int32_t x, std::int32_t y, std::size_t frame, Angle10 heading, std::int32_t scale_div,
                       std::int32_t box_below_zoom);
  void draw_aircraft(const Aircraft& a);         // aircraft_chart_draw (0x240d9)
  void draw_air_group(const AirGroup& g);        // airgroup_chart_draw (0x2447b)
  void draw_air_group_icon(const AirGroup& g);   // airgroup_chart_icon (0x262ed)
  void draw_pilot(const DownedPilot& pilot);     // pilot_chart_draw (0x2537d)
  void draw_pilot_icon(const DownedPilot& pilot);  // pilot_chart_icon (0x2564a)
  void draw_report(const IntelReport& report);   // intel_report_chart_draw / _icon
  void draw_course();       // chart_draw_course (0x270c1)
  // The strategic contact groups of a career patrol, null otherwise.
  ContactGroups* contact_groups() const;
  // Returns the contact groups shown (list 0x119248): a career patrol's,
  // or a single mission's scenario group.
  std::vector<ContactGroup*> chart_groups() const;
  // Returns whether `v` belongs to `g` (its `+0xc8`).
  bool group_member(const ContactGroup& g, const Vehicle& v) const;
  void draw_base_sprites();  // chart_draw_contacts: bases flagged 0x40
  void draw_groups();        // contacts_chart_draw (0x30eea)
  void draw_group(ContactGroup& g);  // the kinds' slot +0x14
  void draw_track(const MotionState& m);  // chart_draw_track_history (0x26ce0)
  void draw_sound_bearings();  // sound_contacts_next / chart_draw_sound_bearing (0x26eca)
  void draw_target_markers();  // chart_draw_target_markers (0x26b15)
  // `sprite_frame_bbox` (0xac13d) of CHART.SHP frame `frame` at (x, y):
  // the box of its drawn pixels, x1 exclusive.
  ui::Rect frame_box(std::size_t frame, std::int32_t x, std::int32_t y) const;
  // The screen box of a vehicle's collision corners 0, 1, 4, 5, 4 pixels
  // larger (the zoomed-in hit rectangles).
  ui::Rect hull_box(const Vehicle& v) const;
  // `contacts_chart_click` (0x31287): the first group whose symbol is under
  // (x, y), with its report in `lines`.
  const ContactGroup* pick_group(std::int32_t x, std::int32_t y, std::vector<std::string>& lines);
  // `chart_pick_vehicle_or_port` (0x21f4b): the first vehicle whose hit
  // rectangle (slot +0x44) holds (x, y), else zoomed out a port, with its
  // info in `lines`; null when none.
  const void* pick_vehicle_or_port(bool tactical, std::int32_t x, std::int32_t y, std::vector<std::string>& lines);
  // The crew's estimate of `v` (`tdc_estimate_solution` with its sighting
  // range and tracking time).
  SolutionEstimate estimate(const Vehicle& v, std::int32_t range_yd, std::int32_t track_time) const;
  void mouse_moved(std::int32_t x, std::int32_t y);     // chart_on_mouse_move (0x21377)
  void course_line_draw(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1);
  void course_line_erase();
  void compact_waypoints();  // chart_compact_waypoints (0x218c1)
  void select_target(std::int32_t x, std::int32_t y);  // tdc_select_target_at_chart_xy (0x7c33b)
  void hover_info(std::int32_t x, std::int32_t y);     // chart_hover_info (0x2158e)
  // `chart_pick_own_sub` (0x29c60) with `chart_own_sub_info` (0x29eac):
  // the boat's info lines when (x, y) is on its marker.
  std::optional<std::vector<std::string>> own_sub_info(std::int32_t x, std::int32_t y) const;
  void close_popup();  // chart_view_close_popup (0x2b002)
  void set_cursor(ui::CursorId id);
  std::int32_t waypoint_count() const;
  // The world point of waypoint `i` (1-based like the original's arrays).
  geom::WorldXZ waypoint(std::int32_t i) const;

  StationEnv& env_;
  WorldTerrainGrid grid_;
  ChartView view_;
  Shp chart_sprites_;  // CHART.SHP (0x117720)
  Shp ship_icons_;     // SHIPICON.SHP (0x117724)
  std::unique_ptr<ui::Dialog> dialog_;  // the view's dialog 0x118c4c
  std::unique_ptr<ui::Dialog> panel_dialog_;
  std::unique_ptr<NavPanel> nav_panel_;
  ChartInfoPopup popup_;                    // 0x1186c0
  const void* popup_object_ = nullptr;      // 0x117968: what the popup shows
  Image8 nav_background_;  // 0x117948: the NAVIGATION panel's picture
  Image8 clean_map_;       // 0x118b0c: the map under the overlays
  // Whether the overlays last drawn reach into the open panel, which must
  // then be repainted once they are restored.
  bool overlays_in_panel_ = true;
  ChartPanelPosition panel_pos_{0x16, 300};  // 0x117b68 (and 0x118278)
  ChartPanelPosition nav_pos_{0x16, 0x59};   // 0x117fe4
  // The panels (slots in `ChartPanel` order), MAP CONTROLS first.
  ChartPanelHost panels_;
  bool suspended_open_ = false;             // 0x11794c
  bool dirty_ = true;                       // 0x118a6c
  bool force_overlays_ = false;             // 0x118b64
  ChartZoomBox zoom_box_;                   // 0x118a78 and the magnifier
  // 0x118a80 as the zoom box sets it: the view is held (no edge scroll, no
  // following the boat) from the zoom box on until the next panel switch
  // (the panel drag's share of the flag is `panels_.locked()`).
  bool zoom_hold_ = false;
  bool zoom_message_ = false;               // 0x117974
  ChartClickMode click_mode_ = ChartClickMode::kSelectTarget;  // 0x117950
  ChartClickMode saved_click_mode_ = ChartClickMode::kSelectTarget;  // 0x117954
  ChartClickMode cheat_saved_mode_ = ChartClickMode::kSelectTarget;  // 0x117958
  ChartRubberBand course_line_;             // 0x118c20..
  std::int32_t waypoint_shown_ = 1;         // 0x11797c: the prompt's number
  std::int32_t shown_compression_ = 0;      // 0x117730
  std::array<std::int32_t, 3> shown_clock_{};  // 0x117734..
  ui::Control* compression_control_ = nullptr;
  ui::Control* clock_control_ = nullptr;
  ui::Control* prompt_control_ = nullptr;
  // The toolbox's second prompt line.
  ui::Control* prompt_hint_ = nullptr;
  // The extended chart (not in the original).
  ChartAnnotations annotations_;
  ChartInks inks_;
  ChartTool tool_ = ChartTool::kPencil;
  ChartInk ink_ = ChartInk::kBlack;
  ChartSymbol symbol_ = ChartSymbol::kCross;
  std::optional<std::array<geom::WorldXZ, 2>> ruler_;
  // The stroke, line or circle being drawn.
  std::optional<ChartAnnotation> draft_;
  // The annotation under the eraser, -1 none.
  int highlight_ = -1;
  ChartPointer pointer_;
  // The pointer position the overlays were last drawn for.
  std::array<int, 2> tools_mouse_{-1, -1};
  // The view's centre when a drag began (the drag scroll).
  std::array<std::int32_t, 2> drag_centre_{};
  bool panning_ = false;
  // The line tool's drag under way, which places the line's next point.
  bool extending_ = false;
  // The pinch under way: the fingers' distance and the zoom when it began,
  // and the chart point under their middle.
  struct Pinch {
    double distance = 0;
    std::int32_t zoom = 0;
    std::array<std::int32_t, 2> anchor{};
  };
  std::optional<Pinch> pinch_;

 public:
  // The panel and the panel positions of the state files (0x118a8c, then
  // 0x117b68.., 0x117fe4.., 0x118278..: map, navigation, plot).
  struct SavedPanels {
    ChartPanel panel = ChartPanel::kMap;
    std::array<std::int32_t, 6> xy{0x16, 300, 0x16, 0x59, 0x16, 300};
  };
  SavedPanels saved_panels() const;
  // Restores them (player_read_state); the plot panel, whose click mode
  // is not saved, comes back as MAP CONTROLS, and the plot panel shares the
  // map panel's position.
  void restore_panels(const SavedPanels& saved);
};

}  // namespace sh
