// The editor's main screen (`sbchart_run` and module sbchart, SE
// 0x3b602..0x44908): the scenario's patrol map in SH's chart view (640x459)
// with the status bar under it, the five floating panels (TOOLBOX, PLOT
// COURSE, the pick panel, SELECT A CITY, REMOVE OBJECT), the click modes
// that select, move and place objects and plot their courses, the drawing
// of every object (slots +0x08 / +0x0c of the editor objects), the hover
// popup and the zoom box.
//
// One frame of the editor's loop (as `sbchart_run`): every pending event to
// `handle_event` (then `process`), then `update`. The TOOLBOX's object
// panels and add tools are the screen's `Toolbox` (`editor/toolbox`); the
// dialogs and the menu bar are reached through `ChartHooks`.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "assets/fnt.h"
#include "assets/pal.h"
#include "assets/shp.h"
#include "core/features.h"
#include "core/install.h"
#include "editor/options.h"
#include "editor/scenario.h"
#include "game/stations/chart_panels.h"
#include "game/stations/chart_view.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh::editor {

class Toolbox;

// The click modes (0xaacd4): what a click on the map does.
enum class ClickMode : std::int32_t {
  kSelect = 0,         // selects an object, or moves the selection there
  kAddShip = 1,        // places a ship of the chosen class
  kAddSub = 2,         // places an enemy submarine
  kRemove = 3,         // removes the object clicked
  kCourse = 4,         // a ship's open course
  kSubCourse = 5,      // an enemy submarine's open course
  kAircraftPath = 6,   // an aircraft's (looped) path
  kEscortCourse = 7,   // an escort's looped course
  kPickConvoy = 8,     // a ship of the convoy to join or escort
  kPatrolCenter = 9,   // an aircraft's patrol centre
  kPatrolRadius = 10,  // and radius
  kPickCity = 11,      // an airstrike's or photo recon's target
};

// The panels, in `chart_view_add_panel` order (0xa4748).
enum class EditorPanel : std::int32_t {
  kNone = -1,
  kToolbox = 0,
  kPlotCourse = 1,
  kPick = 2,  // ADD SHIP, ADD SUB, PICK CONVOY, SELECT POINT, SELECT RADIUS
  kSelectCity = 3,
  kRemove = 4,
};

// The panel prompts (`sbchart_format_prompt`, table 0x9e70c, 0xaacdc).
enum class Prompt : std::int32_t {
  kLocation = 0,
  kObject = 1,
  kWaypoint = 2,  // "Select waypoint #N"
  kConvoyShip = 3,
  kPatrolCenter = 4,
  kPatrolRadius = 5,
  kCityToBomb = 6,
  kCityToPhotograph = 7,
  kObjectToRemove = 8,
};

// The TOOLBOX icon buttons that add objects (`Toolbox::tool`).
enum class ToolboxTool { kAddShip, kAddAircraft, kAirstrike, kPhotoRecon };

// What the chart screen draws with and on (the editor's `EditorUi` holds
// them all).
struct ChartScreenEnv {
  const Install& install;
  ui::Gui& gui;
  // The screen palette 0xa4af0, which `open` builds.
  VgaPalette& palette;
  const Font& font1;      // FONT1.FNT (0xa5050)
  const Font& msg_font;   // MSG.FNT (0xa5054)
  const Font& menu_font;  // MENU_S.FNT (0xa5058)
  const Shp& sbchart;     // SBCHART.SHP (0xa505c)
  const Shp& cursors;     // SBCURSOR.SHP
  // The improvements beyond the original the editor runs with.
  Features features{};
};

// The calls into the other parts of the editor; all optional.
struct ChartHooks {
  // `editor_menu_handle_event`: the menu bar on Esc / the right button,
  // Alt-Q, Alt-M while the toolbox is up. Returns true when it took the
  // event.
  std::function<bool(const ui::Event&)> menu_event;
  // `ask_yes_no(title, three lines, "YES", "NO")` with the palette greyed
  // at 70 % around it (`palette_grey_out(0x46, 0xff)`): true for YES.
  // Without a hook the answer is YES.
  std::function<bool(std::string_view title, const std::array<std::string_view, 3>& lines)> ask_yes_no;
  // `show_message_box(title, three lines)`, greyed likewise.
  std::function<void(std::string_view title, const std::array<std::string_view, 3>& lines)> message;
  // The toolbox's dialogs (module guiuser), which grey the palette
  // themselves; the chart closes its panel around them. Without a hook a
  // dialog counts as cancelled.
  // `select_sub_type`: the SUBS.DAT class and boat name in and out;
  // returns the dialog result (-2 cancelled).
  std::function<int(std::int32_t& sub_class, ScnChars<31>& name)> select_sub_type;
  // `select_sub_name` for a SUBS.DAT class: returns the result (-2
  // cancelled).
  std::function<int(std::int32_t sub_class, ScnChars<31>& name)> select_sub_name;
  // `select_ship_type`: the SHIPS.DAT class in and out (-1 cancelled).
  std::function<void(std::int32_t& ship_class)> select_ship_type;
  // `select_aircraft_type`: the AIRCRAFT.DAT type in and out; returns the
  // result (-2 cancelled).
  std::function<int(std::int32_t& aircraft_type)> select_aircraft_type;
  // `orders_popup_run`: the entries, the greyed ones, the one selected;
  // returns the chosen index or -3.
  std::function<int(const std::vector<std::string>& labels, const std::vector<bool>& disabled, int initial)>
      orders_popup;
  // The first frame's `palette_fade_in` of the screen palette.
  std::function<void(const VgaPalette&)> fade_in;
};

// A city of SELECT A CITY: its base index and name.
struct CityChoice {
  std::size_t base = 0;
  std::string name;
};

// Returns the scenario editor's chart palette (`sbchart_open` after the
// title box, docs/editor-assets.md "Palettes"): M8.PCX when SE
// was started by SH (else black), SBCHART frame 0, the map's chart colours
// 128-239, SBINTRO frame 0, SBCHART frame 0 again and SHIPICON frame 0's
// colours 16-63.
VgaPalette se_chart_palette(const Install& install, const World& world, bool started_by_game);

// Returns the 60 points of a circle of `radius` world units around
// `centre` (`chart_build_circle_points`, 0x13015): 15 per quadrant, from
// east counterclockwise, the cosines and sines of 0..84 degrees.
std::vector<WorldPoint> chart_circle_points(WorldPoint centre, std::int32_t radius);

class ChartScreen {
 public:
  // The view covers (0, 0)..(0x27f, 0x1ca); the status bar is below.
  static constexpr int kViewX1 = 0x27f;
  static constexpr int kViewY1 = 0x1ca;
  static constexpr int kStatusY = 0x1cb;
  // The zoom from which objects draw their scaled class icons, and the one
  // under which bases are drawn.
  static constexpr std::int32_t kIconZoom = 16;
  static constexpr std::int32_t kBaseZoom = 8;
  // A course click this close to the start (pixels on both axes, exclusive)
  // closes the loop.
  static constexpr std::int32_t kLoopClosePixels = 11;

  // `scenario` and `options` must outlive the screen. `started_by_game`:
  // SE was spawned by SH (M8.PCX is in the palette).
  ChartScreen(ChartScreenEnv env, Scenario& scenario, const EditorOptions& options, ChartHooks hooks,
              bool started_by_game);
  ~ChartScreen();
  ChartScreen(const ChartScreen&) = delete;
  ChartScreen& operator=(const ChartScreen&) = delete;

  // ------------------------------------------------------------ frame

  // `sbchart_open` (0x3b668): the palette, the view, the status bar, the
  // panels (TOOLBOX requested), click mode 0.
  void open();
  // `sbchart_handle_event` (0x3c03a): Esc / the right button end a
  // panel's mode; with the toolbox up, the menu hook, else the view.
  void handle_event(const ui::Event& event);
  // `sbchart_process` (0x3c0eb).
  void process();
  // `sbchart_update` (0x3c13f); `first` on the first frame.
  void update(bool first);
  // `sbchart_close` (0x3bf93).
  void close();
  // After New, a load or a patrol area change (`sb_setup_world`, the world
  // possibly loaded again): the view of the scenario's world at zoom 1 on
  // the map's centre, its palette, the city list emptied, a redraw.
  void world_changed();

  // ------------------------------------------------------------ state

  const ChartView& view() const { return *view_; }
  ChartView& view() { return *view_; }
  ClickMode click_mode() const { return click_mode_; }
  ClickMode previous_click_mode() const { return previous_mode_; }
  EditorPanel panel() const { return static_cast<EditorPanel>(panels_.requested()); }
  EditorPanel shown_panel() const { return static_cast<EditorPanel>(panels_.active()); }
  std::int32_t zoom_box() const { return zoom_box_.value(); }
  // The object under the pointer (0xaace0), from the last hit test.
  EditorObject* hovered() const { return hovered_; }
  Prompt prompt() const { return prompt_; }
  // The panel prompt text with its number.
  std::string prompt_text() const;
  // The pick panel's title (0xada4c) and whether its EXIT works (0x9e770).
  const std::string& pick_title() const { return pick_title_; }
  bool pick_exit_enabled() const { return pick_exit_; }
  // The info popup's lines while it shows, else empty.
  std::vector<std::string> popup_text() const { return popup_.text(); }
  std::array<int, 2> popup_position() const { return popup_.position(); }
  // The status bar texts of the last pointer move: the latitude, the
  // longitude, the depth ("N ft", empty on land) or the zoom box message,
  // and the right-aligned hint.
  const std::string& status_latitude() const { return status_latitude_; }
  const std::string& status_longitude() const { return status_longitude_; }
  const std::string& status_depth() const { return status_depth_; }
  const std::string& status_hint() const { return status_hint_; }
  // The cursor frame of SBCURSOR.SHP last set (`chart_set_cursor`).
  std::size_t cursor() const { return cursor_; }
  // The SELECT A CITY list (0xae0dc / 0xaad78) and its selection (0xae104).
  const std::vector<CityChoice>& cities() const { return cities_; }
  std::int32_t city_selection() const { return city_selection_; }

  // ------------------------------------------------------------ view

  // The world point under screen pixel (`x`, `y`)
  // (`chart_view_screen_to_world`).
  WorldPoint screen_to_world(std::int32_t x, std::int32_t y) const;
  // The screen pixel of a world point, when within 32000 pixels
  // (`chart_point_on_screen`).
  std::optional<std::array<std::int32_t, 2>> world_to_screen(WorldPoint p) const;
  // The zoom box keys and buttons (`chart_zoom_box_grow` / `_shrink`).
  void zoom_in();
  void zoom_out();
  // `sbchart_button_center_on_object` (0x3e475).
  void center_on_selection();
  // `sb_center_on_player` (0x451f0, after New and loads): the player
  // selected and centred.
  void center_on_player();

  // ------------------------------------------------------------ objects

  // Slot +0x0c (`*_hit_rect`): the screen rectangle the pointer selects
  // `object` in, at the current zoom; none off screen.
  std::optional<ui::Rect> hit_rect(const EditorObject& object) const;
  // `sbchart_pick_object_at` (0x3dab1): the first object in list order
  // whose hit rectangle holds (`x`, `y`) (not the player in mode 3, only
  // convoy ships in mode 8, not the selection in mode 0); sets the hovered
  // object and the cursor (4 over an object, else 1).
  EditorObject* pick_object_at(std::int32_t x, std::int32_t y);
  // The radii (world units) of the range circles around the player
  // (option 0xa50f8): the selected object's sighting range, then the
  // player's surface radar range; none when the first is 0.
  std::vector<std::int32_t> range_circle_radii() const;
  // The TOOLBOX arrows (`sbchart_toolbox_next_object` / `_prev_object`).
  void next_object();
  void previous_object();
  // Selects `object` and reopens the toolbox for it (a click in mode 0).
  void select(EditorObject& object);
  // Closes and reopens the toolbox (0xaace8, after a ship <-> sub change).
  void request_toolbox_reopen() { toolbox_reopen_ = true; }
  // `object` becomes the selection when the next panel opens (0xaad28).
  void select_on_panel_open(EditorObject* object) { select_on_open_ = object; }
  // Asks for the objects to be drawn again (0xaad1c).
  void request_overlay_redraw() { overlay_redraw_ = true; }
  // `sbchart_toolbox_redraw` (0x3c28f): the toolbox's dirty controls
  // drawn, then all of it marked dirty.
  void redraw_toolbox();
  // `chart_view_close_panel` + `chart_view_open_panel`: the requested panel
  // opened again at once.
  void reopen_panel();
  // Runs `modal` (a dialog over the chart) with the panel and the popup
  // closed, then opens the panel again.
  void with_panel_closed(const std::function<void()>& modal);
  // `sbchart_on_panel_hover(-1)` as the menu bar and Alt-Q call it: the
  // pointer cursor, the popup and the course line gone, the status bar
  // black.
  void leave_view() { panel_hover(true); }
  // `sbchart_confirm_remove_leader` (0x443c6): "REMOVE CONVOY LEADER?"
  // YES / NO; true for YES.
  bool confirm_remove_leader();
  // The toolbox of the panel (its object panels and add tools).
  Toolbox& toolbox() { return *toolbox_; }
  // The open panel's dialog, or nullptr.
  const ui::Dialog* panel_dialog() const { return panel_dialog_.get(); }

  // ------------------------------------------------------------ clicks

  // `sbchart_on_click` (0x3c8fc) at view pixel (`x`, `y`).
  void click(std::int32_t x, std::int32_t y);
  // `sbchart_on_mouse_move` (0x3c371): hover, rubber band, status bar.
  void mouse_moved(std::int32_t x, std::int32_t y);
  // `sbchart_set_click_mode` / `sbchart_restore_click_mode` (0x43c27 /
  // 0x43c22): the previous mode is remembered / swapped back.
  void set_click_mode(ClickMode mode);
  void restore_click_mode();
  // `sbchart_mode_finish` (0x3d693): ends the mode (DONE, EXIT, Esc, a
  // pick, the 24th point) and goes on to the next one or back to the
  // toolbox. Returns false when the mode cannot end (a course of fewer
  // than 2 points).
  bool finish_mode();
  // `sbchart_clear_course` (0x3d950): CLEAR of PLOT COURSE.
  void clear_course();

  // The mode starts of the toolbox and its sub-panels:
  // `sbchart_tool_add_ship` after SELECT SHIP TYPE (class type 11 gives a
  // submarine): ADD SHIP / ADD SUB.
  void start_add_vessel(std::size_t ship_class);
  // `sbchart_tool_remove_object`: "CANNOT DELETE OBJECT" with the player
  // alone, else REMOVE OBJECT.
  void start_remove();
  // The ship orders' course (`toolbox_ship_orders_button`, modes 4, 5, 7):
  // the selected vessel's course emptied, PLOT COURSE.
  void start_course(ClickMode mode);
  // `sbchart_tool_add_aircraft` after its dialogs: PLOT COURSE for a
  // Follow Path aircraft (mode 6, the path emptied), else SELECT POINT
  // (mode 9). `aircraft` becomes the selection when the panel opens.
  void start_aircraft(Aircraft& aircraft);
  // PICK CONVOY for the selection (mode 8).
  void start_pick_convoy();
  // SELECT RADIUS for the selected aircraft's new patrol mission (mode 10,
  // `toolbox_air_mission_button`).
  void start_patrol_radius();
  // PLOT COURSE for the selected aircraft's new Follow Path mission (mode
  // 6): the path restarts at the aircraft's position.
  void start_aircraft_path(Aircraft& aircraft);
  // SELECT A CITY for the selected airstrike (`photo_recon` false) or
  // photo recon (mode 11); `restore` is selected again on cancel (0xaad24;
  // nullptr, from an existing mission's Location button, keeps the last
  // one).
  void start_pick_city(bool photo_recon, EditorObject* restore);
  // `sbchart_city_button_cancel` (0x40cf0): CANCEL / Esc of SELECT A CITY.
  void cancel_city();
  // `sbchart_remove_button_current` (0x40d5f): REMOVE OBJECT's CURRENT.
  void remove_current();

 private:
  // Panel slots.
  void add_panels();
  void open_toolbox();   // sbchart_toolbox_open (0x3b92c)
  void close_toolbox();  // sbchart_toolbox_close
  std::unique_ptr<ui::Dialog> make_toolbox();
  std::unique_ptr<ui::Dialog> make_mode_panel(EditorPanel which);
  void add_view_controls(ui::Dialog& d, int scale_y);
  void add_sprite_button(ui::Dialog& d, ui::Rect r, std::size_t up, std::size_t down, std::function<void()> action);
  void open_panel_dialog(EditorPanel which);
  void close_panel_dialog();
  void repaint_panel_dialog();
  void panel_hover(bool over);  // sbchart_on_panel_hover (0x3dc66)

  // The view (chart_view_* with the sbchart callbacks).
  void build_view();
  void view_update();      // chart_view_update
  void redraw_map();       // chart_view_redraw_if_dirty
  void redraw_overlays();  // chart_view_redraw_overlays
  void repaint_panels();
  void draw_contents();  // sbchart_draw_contents (0x40e41)
  void draw_object(const EditorObject& object, bool pass_courses,
                   bool selected);  // slot +0x08
  std::optional<ui::Rect> draw_player(const PlayerSub& p, bool pass_courses);
  std::optional<ui::Rect> draw_vessel(const Vessel& v, bool pass_courses, bool selected);
  std::optional<ui::Rect> draw_aircraft(const Aircraft& a, bool pass_courses, bool selected);
  std::optional<ui::Rect> draw_mission(const SpecialMission& m, bool pass_courses);
  // sbchart_draw_route (0x4360c): `n` points, closed when entry `n` of
  // `xs` is -1; markers at the points when the colours differ.
  void draw_route(std::int32_t n, const std::int32_t* xs, const std::int32_t* ys, std::uint8_t colour,
                  std::uint8_t marker);
  void draw_selection_box(ui::Rect r);               // 0x432c5
  void draw_convoy_icon(bool leader, WorldPoint p);  // 0x4357c
  void draw_leader_cross(WorldPoint p);              // 0x43b76
  // The class icon of SHIPICON.SHP scaled to the zoom, or the SBCHART mark
  // below 16x; the drawn rectangle.
  std::optional<ui::Rect> draw_unit_icon(WorldPoint p, std::optional<std::size_t> icon, std::size_t mark,
                                         std::int32_t heading);
  std::optional<ui::Rect> unit_hit_rect(WorldPoint p, std::optional<std::size_t> icon, std::size_t mark) const;
  Viewport view_viewport();

  // Clicks and modes.
  void add_vessel_at(std::int32_t x,
                     std::int32_t y);                                  // sbchart_add_ship_at (0x3cd5f)
  void add_course_point(std::int32_t x, std::int32_t y, bool looped);  // 0x3d035
  void add_aircraft_point(std::int32_t x, std::int32_t y);             // 0x3d2a2
  void set_patrol_area(std::int32_t x, std::int32_t y, bool radius);   // 0x3d394
  void remove_hovered();                                               // 0x3d3ee
  void pick_convoy_hovered();                                          // 0x3d5d2
  bool pick_city_at(std::int32_t x, std::int32_t y);                   // 0x3dbc4
  std::optional<ui::Rect> city_hit_rect(std::size_t base) const;       // 0x3e085
  void build_city_list();                                              // sbchart_city_panel_open's list
  // Closes the panel's dialog, kept until the frame ends.
  void retire_panel_dialog();
  // Sets PLOT COURSE's DONE and CLEAR disabled or not, on the open panel
  // too.
  void set_plot_buttons_disabled(bool disabled);
  // The draft (ft) of `vessel`'s class (SHIPS.DAT +0xb8), 0 when unknown.
  std::int32_t vessel_draft(const Vessel& vessel) const;
  void set_cursor(std::size_t frame);

  // The hover popup.
  void hover_poll();                                        // sbchart_hover_poll
  void update_hover_popup(std::int32_t x, std::int32_t y);  // 0x3da61
  void close_popup();
  // sbchart_info_text_at (0x3df1d): the text and its owner (an object or a
  // base record), none when nothing is under the pointer.
  std::optional<std::pair<const void*, DescribeText>> info_text_at(std::int32_t x, std::int32_t y) const;
  DescribeText city_info_text(std::size_t base) const;  // 0x3e15e

  ChartScreenEnv env_;
  Scenario& scenario_;
  const EditorOptions& options_;
  ChartHooks hooks_;
  bool started_by_game_;
  Shp ship_icons_;                      // SHIPICON.SHP (0xaacc8)
  std::optional<ChartView> view_;       // 0xa4e30
  std::unique_ptr<ui::Dialog> dialog_;  // the view's dialog
  std::unique_ptr<ui::Dialog> panel_dialog_;
  // Closed panel dialogs: a control's callback may close its own panel
  // (the original's dialogs are static), so they live until `update`.
  std::vector<std::unique_ptr<ui::Dialog>> retired_dialogs_;
  std::unique_ptr<Toolbox> toolbox_;
  ChartInfoPopup popup_;  // 0xae590
  std::array<ChartPanelPosition, 5> panel_pos_{};
  ChartPanelHost panels_;
  ChartZoomBox zoom_box_;
  ChartRubberBand course_line_;
  Image8 clean_map_;
  bool dirty_ = true;               // 0xa4728
  bool force_overlays_ = false;     // 0xa4820
  bool view_suspended_ = false;     // 0xa473c: a zoom box holds the edge scroll
  std::int32_t city_zoom_ = 0x800;  // 0x982e4

  ClickMode click_mode_ = ClickMode::kSelect;     // 0xaacd4
  ClickMode previous_mode_ = ClickMode::kSelect;  // 0xaacd8
  Prompt prompt_ = Prompt::kLocation;             // 0xaacdc
  std::int32_t prompt_number_ = 0;                // 0xaad6c[0]
  std::string pick_title_;                        // 0xada4c
  bool pick_exit_ = true;                         // 0x9e770
  std::size_t add_class_ = 0;                     // 0xaacd0
  bool city_photo_recon_ = false;                 // 0xaace4
  EditorObject* hovered_ = nullptr;               // 0xaace0
  EditorObject* restore_selection_ = nullptr;     // 0xaad24
  EditorObject* select_on_open_ = nullptr;        // 0xaad28
  WorldPoint anchor_;                             // 0xaacfc / 0xaad00
  bool overlay_redraw_ = false;                   // 0xaad1c
  bool objects_drawn_ = false;                    // 0xaad20
  bool toolbox_reopen_ = false;                   // 0xaace8
  std::size_t object_count_ = 0;                  // 0xaad14
  std::int32_t shown_memory_ = 0;                 // 0xaad04
  std::int32_t last_requested_ = 0;               // 0xaad18
  const void* popup_owner_ = nullptr;             // 0xaad8c
  std::size_t cursor_ = 1;                        // 0xaaccc
  std::vector<CityChoice> cities_;
  std::int32_t city_selection_ = 0;  // 0xae104
  std::int32_t city_top_ = 0;        // 0xae100: the list's first row shown
  ui::List* city_list_ = nullptr;
  std::string status_latitude_, status_longitude_, status_depth_, status_hint_;
  ui::Button* prev_button_ = nullptr;
  ui::Button* next_button_ = nullptr;
  ui::Control* gauge_ = nullptr;
  // PLOT COURSE's DONE and CLEAR are disabled (0xad90c / 0xad948).
  bool plot_buttons_disabled_ = true;
  ui::Button* done_button_ = nullptr;
  ui::Button* clear_button_ = nullptr;
};

}  // namespace sh::editor
