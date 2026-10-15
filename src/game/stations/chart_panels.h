// The parts of the chart view engine (module chart0, `chart_view_*` at
// 0x2a5c8..0x2c531) shared by the game's chart station and the scenario
// editor's chart screen, which SE.EXE links unchanged:
// - `ChartPanelHost`: the floating panels (`chart_view_add_panel` slots),
//   the panel switch, the pointer over a panel and the panel drag;
// - `ChartZoomBox`: the Z / X zoom box and its magnifier;
// - `ChartRubberBand`: the inverted line drawn to the next waypoint;
// - `ChartInfoPopup` and `chart_place_info_popup`: the hover info box;
// - `chart_edge_scroll`: scrolling with the pointer on a screen edge.
//
// The view's own state (the map, its dirty flag, the overlays) stays with
// the screen using them.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "assets/fnt.h"
#include "core/image.h"
#include "game/stations/chart_view.h"
#include "gfx/viewport.h"
#include "ui/controls.h"
#include "ui/dialog.h"

namespace sh {

// The position of a panel's dialog (its +0 / +4), which the panel drag
// moves. Several panels may share one.
struct ChartPanelPosition {
  int x = 0;
  int y = 0;
};

// One `chart_view_add_panel` slot: where the panel is and its callbacks
// (open, close, event, process, update, redraw).
struct ChartPanelSlot {
  ChartPanelPosition* position = nullptr;
  int width = 0;
  int height = 0;
  // Opens / closes the panel's dialog at `position`.
  std::function<void()> open;
  std::function<void()> close;
  // Feeds an event to the open panel / runs its dialog for a frame.
  std::function<void(const ui::Event&)> event;
  std::function<void()> process;
  // Called every frame while the panel is open (optional).
  std::function<void()> update;
  // Invalidates and repaints the open panel.
  std::function<void()> redraw;
};

// The cursors the panel drag selects.
enum class ChartCursor { kDefault, kDrag };

// The panels of a chart view (0x118a8c requested, 0x118a90 active, 0x118a70
// open, 0x118a94 pointer over it, 0x118a7c / 0x118b44 drag), shown one at a
// time and moved by dragging their title strip.
class ChartPanelHost {
 public:
  static constexpr std::size_t kMaxPanels = 5;
  static constexpr int kNone = -1;
  // The strip at the top of a panel that drags it.
  static constexpr int kDragStrip = 0x23;

  struct Hooks {
    // The panel-hover callback (0x118b84): the pointer entered (true) or
    // left (false) the open panel.
    std::function<void(bool over)> hover;
    // The mouse-move callback (0x118b88): the pointer moved over the view
    // (or `force_mouse_move` was called).
    std::function<void(int x, int y)> mouse_move;
    std::function<void(ChartCursor)> cursor;
  };

  // A host for a view covering `bounds` (the panels are kept inside it).
  ChartPanelHost(ui::Gui& gui, ui::Rect bounds, Hooks hooks);

  // Changes the view's rectangle (the wide-screen layout; not in the
  // original), moving the panels back inside it.
  void set_bounds(ui::Rect bounds);

  // `chart_view_add_panel`: returns the slot index, or `kNone` when the five
  // slots are taken (the original ignores the call).
  int add(ChartPanelSlot slot);
  // The view's dialog, whose first hotspot (the map area) is disabled while
  // the pointer is over a panel.
  void set_view_dialog(const ui::Dialog* dialog) { view_dialog_ = dialog; }
  // The state reset of `chart_view_init`: no panel shown or open, no hover,
  // no drag, the pointer position forgotten.
  void reset();

  void request(int panel) { requested_ = panel; }
  int requested() const { return requested_; }
  int active() const { return active_; }
  // Forgets the active panel, so that the next `switch_panel` reopens the
  // requested one.
  void deactivate() { active_ = kNone; }
  bool is_open() const { return open_; }
  bool hovered() const { return hovered_; }
  bool dragging() const { return dragging_; }
  // Whether a drag holds the view (0x118a7c): no zoom box, scroll or
  // follow meanwhile.
  bool locked() const { return locked_; }
  // The active panel's screen rectangle (inclusive), open or not.
  std::optional<ui::Rect> active_rect() const;

  // `chart_view_switch_panel`: closes the active panel and opens the
  // requested one when they differ.
  void switch_panel();
  // `chart_view_open_panel` / `chart_view_close_panel`.
  void open_panel();
  void close_panel();
  // `chart_view_handle_mouse`: the pointer over the panel (hover callback,
  // view hotspot disabled), the drag, else the mouse-move callback when the
  // pointer moved.
  void handle_mouse();
  // Forces the next `handle_mouse` to call the mouse-move callback
  // (0x118a88).
  void force_mouse_move() { last_mouse_.reset(); }
  // Updates the hover from a press at (`x`, `y`) before it is handled (not
  // in the original, whose pointer always moved there first): a finger
  // lands away from where it last tapped, so a press on the map just after
  // a tap on the panel would find the view's hotspot still disabled.
  void hover_at_press(int x, int y);

  // The open panel's event / process / update / redraw callbacks.
  void event(const ui::Event& event);
  void process();
  void update();
  void redraw();

 private:
  void drag(int mx, int my);  // chart_view_drag_panel
  void set_hovered(bool hovered);

  ui::Gui& gui_;
  ui::Rect bounds_;
  Hooks hooks_;
  const ui::Dialog* view_dialog_ = nullptr;
  std::vector<ChartPanelSlot> slots_;
  int requested_ = 0;
  int active_ = kNone;
  bool open_ = false;
  bool hovered_ = false;
  bool dragging_ = false;
  bool locked_ = false;
  int drag_dx_ = 0, drag_dy_ = 0;  // 0x118b3c / 0x118b40
  std::optional<std::array<int, 2>> last_mouse_;  // 0x103704 / 0x103708
};

// The zoom box (0x118a78): the zoom the next click on the map applies,
// shown as a magnifier around the pointer.
class ChartZoomBox {
 public:
  // The box's zoom, 0 when closed.
  std::int32_t value() const { return value_; }
  bool is_open() const { return value_ != 0; }
  void close() { value_ = 0; }

  // `chart_zoom_box_grow`: doubles the box (or the view's zoom), up to 64
  // times the view's zoom and below `max_zoom`.
  void grow(const ChartView& view, std::int32_t max_zoom);
  // `chart_zoom_box_shrink`: halves the box while above the view's zoom;
  // without a box, halves the view's zoom. Returns whether the view's zoom
  // changed.
  bool shrink(ChartView& view);

  // `chart_view_update`'s magnifier: draws it at (`x`, `y`) of `screen` when
  // the pointer or the box changed since the last call.
  void track(Image8& screen, const ChartView& view, int x, int y);
  // `chart_view_free_magnifier`: puts the screen under the magnifier back.
  void free_magnifier(Image8& screen);
  // Frees the magnifier and forgets where it was.
  void reset(Image8& screen);

 private:
  void draw_magnifier(Image8& screen, const ChartView& view, int mx, int my);  // 0x2b838

  std::int32_t value_ = 0;
  std::int32_t shown_box_ = 0;               // 0x118b08
  std::optional<std::array<int, 2>> at_;     // 0x118b00 / 0x118b04
  Image8 saved_;                             // 0x118ad8: the screen under the box
  int saved_x0_ = 0, saved_y0_ = 0;
};

// The rubber band (0x118c20..): a line inverted on the screen, erased by
// inverting it again.
class ChartRubberBand {
 public:
  // `chart_course_line_draw`: inverts the line from (`x0`, `y0`) to (`x1`,
  // `y1`) of `vp`.
  void draw(const Viewport& vp, int x0, int y0, int x1, int y1);
  // `chart_course_line_erase`: inverts the drawn line back, if any.
  void erase();
  // `chart_course_line_reset`: forgets the line (the screen under it was
  // redrawn).
  void forget() { drawn_ = false; }
  bool drawn() const { return drawn_; }

 private:
  bool drawn_ = false;
  Viewport vp_;
  int x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
};

// The distance from a screen edge, in 640x480 game pixels, within which the
// pointer scrolls the chart. It deliberately differs from the original,
// whose zone is the outermost row or column only (a margin of 1: x < 1,
// x > 0x27e, y < 1, y > 0x1de), so that a pointer the window does not
// capture, which may leave it at any speed, reaches it without aiming at a
// single row: 4 pixels, 8 window pixels at the default double size,
// about the width of a window's resize border. Over the open panel the
// original's margin is kept (a panel may be dragged to an edge). The
// bottom zone lies on the bar under the map, which has no button on either
// chart (the game's bar only shows texts there, the editor's is its status
// bar).
inline constexpr int kChartEdgeScrollMargin = 4;

// The screen edges the pointer is on, as `chart_edge_scroll` sees them.
struct ChartEdges {
  bool left = false;
  bool right = false;
  bool top = false;
  bool bottom = false;

  bool any() const { return left || right || top || bottom; }
  bool operator==(const ChartEdges&) const = default;
};

// Returns the edges the pointer at (`x`, `y`) scrolls toward: within
// `kChartEdgeScrollMargin` of them, or of 1 with the pointer `over_panel`
// (the original's). Not in the original: with `edges` kExact (fullscreen),
// or from row `bar_top` down (the extended chart's full command bar), only
// the edge rows and columns themselves; with kNone (touch screens), none.
// `screen_w` is the screen's width (wider for the wide screen).
ChartEdges chart_edge_scroll_edges(int x, int y, bool over_panel, ui::PointerEdges edges = ui::PointerEdges::kBand,
                                   int bar_top = kScreenHeight, int screen_w = kScreenWidth);

// The 100 Hz ticks over which a pointer held on an edge scrolls a chart by
// one of the original's steps (not in the original, whose pointer was moved
// back from the edge at each step): 500 screen pixels a second.
inline constexpr std::uint32_t kChartEdgeHoldTicks = 10;

// The pace of a chart's edge scroll while the pointer is held on an edge
// (`ui::EdgeScrollPointer::kHold`): the tick of the last frame it scrolled
// on, none while the pointer is off the edges.
struct ChartEdgeHold {
  std::optional<std::uint32_t> since;
};

// `chart_view_edge_scroll`: the pointer on a screen edge (see
// `chart_edge_scroll_edges`) scrolls the view by 50 / zoom chart pixels;
// when `dirty` (set by a scroll that moved the view) the pointer is moved
// back 30 pixels. The zones follow the mouse's `edges` and `bar_top`.
// Not in the original, after the mouse's `edge_scroll_pointer`: a pointer
// held on the edge stays there, and the view scrolls by that step every
// `kChartEdgeHoldTicks` from the frame the pointer reached the edge on
// (`ticks`, the GUI's clock, kept in `hold`), spread over the frames; a
// pointer away scrolls nothing.
void chart_edge_scroll(ChartView& view, ui::Mouse& mouse, bool over_panel, bool& dirty, ChartEdgeHold& hold,
                       std::uint32_t ticks, int bar_top = kScreenHeight, int screen_w = kScreenWidth);

// Returns the top-left corner of a `w` x `h` info box for the pointer at
// (`x`, `y`) (`chart_show_info_popup` 0x21a0c, SE `sbchart_place_info_popup`
// 0x3dce8): searched on circles of growing radius from the side facing the
// screen's centre, on the screen above the status bar and off `avoid`; the
// panel is ignored after 0x98 rounds, and a box that fits nowhere goes to
// (0, 0) after two more passes (the original searches forever).
std::array<int, 2> chart_place_info_popup(int x, int y, int w, int h, std::optional<ui::Rect> avoid,
                                          int screen_w = kScreenWidth);

// The hover info box (dialog 0x1186c0, SE 0xae590): up to six lines.
class ChartInfoPopup {
 public:
  static constexpr std::size_t kLines = 6;

  ChartInfoPopup(ui::Gui& gui, const Font& font) : gui_(gui), font_(font) {}

  // Opens the box at the place `chart_place_info_popup` finds for `lines`;
  // when it is open, only changes its lines.
  void show(int x, int y, const std::vector<std::string>& lines, std::optional<ui::Rect> avoid);
  // Closes the box; returns whether it was open.
  bool close();
  bool is_open() const { return dialog_ != nullptr; }
  // The non-empty lines of the open box.
  std::vector<std::string> text() const;
  // The open box's screen position.
  std::array<int, 2> position() const;
  // Invalidates and repaints the open box.
  void repaint();

 private:
  ui::Gui& gui_;
  const Font& font_;
  std::unique_ptr<ui::Dialog> dialog_;
  std::array<ui::Label*, kLines> lines_{};
};

}  // namespace sh
