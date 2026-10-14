// The chart's additions (not in the original), as `ChartStation` members
// (chart.h): with `Features::chart_tools` the CHART TOOLS panel and its
// annotations, the zooms, the full bar's panels; in every build the drag
// scroll of a finger.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "assets/exe_image.h"
#include "game/command_bar.h"
#include "game/mission.h"
#include "game/stations/chart.h"
#include "gfx/viewport.h"
#include "ui/draw.h"

namespace sh {

namespace {

constexpr std::int32_t kSfxClick = 2;
constexpr ui::CursorId kDefaultCursor = ui::CursorId::kPointer;
constexpr ui::CursorId kDragCursor = ui::CursorId::kMove;
constexpr ui::CursorId kToolCursor = ui::CursorId::kCross;

// The original's strings the toolbox reuses.
constexpr std::uint32_t kClearText = 0xf1b0a;   // the plotting panel's CLEAR
constexpr std::uint32_t kCancelText = 0xf32c7;  // the name dialogs' Cancel
constexpr std::uint32_t kAcceptText = 0xf32ce;  // and Accept

// The panels' first button row.
constexpr int kButtonRowY = 0x2b;

// The CHART TOOLS panel: the tools row, UNDO, the zoom group moved right,
// the inks and symbols row, the prompt line.
constexpr int kToolX = 0xb;
constexpr int kToolPitch = 22;
constexpr int kUndoX = 0xa7;
constexpr int kZoomDx = 8;
constexpr int kSmallRowY = 0x46;
constexpr int kInkX = 0xb;
constexpr int kSymbolX = 0x54;
constexpr int kSmallPitch = 17;
constexpr int kClearX = 0xd2;
constexpr int kClearW = 0x4a;
constexpr int kSwitchX = 0xe;
constexpr int kSwitchY = 0x5c;

// The wheel's continuous zoom: four notches double it.
constexpr double kNotchesPerDoubling = 4;

// The pencil keeps a point every few pixels of the pointer's path.
constexpr int kStrokeStep = 3;
// The eraser's and the line tool's end-point reach (pixels), with the
// mouse and with a finger.
constexpr int kPickRadius = 4;
constexpr int kTouchPickRadius = 10;

bool drag_tool(ChartTool tool) {
  return tool == ChartTool::kPencil || tool == ChartTool::kCircle || tool == ChartTool::kRuler;
}

}  // namespace

// A button of the toolbox drawn by `draw`: sunk while held or while
// `latched` says so.
class ChartStation::DrawnButton : public ui::Control {
 public:
  using DrawFn = std::function<void(const Viewport&, int x, int y, bool down)>;
  DrawnButton(ChartStation& chart, ui::Rect rect, DrawFn draw, std::function<bool()> latched,
              std::function<void()> action)
      : chart_(chart), rect_(rect), draw_(std::move(draw)), latched_(std::move(latched)), action_(std::move(action)) {}

  bool handle(ui::Dialog& dialog, ui::Msg msg, int, const ui::Event&) override {
    bool down = false;
    switch (msg) {
      case ui::Msg::kRegister:
        ui::add_control_hotspot(dialog, *this, rect_, 0, 0, ui::kFocusNone);
        return false;
      case ui::Msg::kPress:
        if (!pressed) {
          chart_.env_.sound.play_sfx(kSfxClick, 0x1e);
        }
        down = true;
        break;
      case ui::Msg::kRelease:
      case ui::Msg::kPaint:
        break;
      default:
        return false;
    }
    draw_(dialog.gfx(), rect_.x0, rect_.y0, down || (latched_ && latched_()));
    dialog.add_dirty(rect_);
    return true;
  }

  int activate(ui::Dialog& dialog, int, const ui::Event&) override {
    action_();
    dialog.invalidate();
    return 0;
  }

 private:
  ChartStation& chart_;
  ui::Rect rect_;
  DrawFn draw_;
  std::function<bool()> latched_;
  std::function<void()> action_;
};

// --- The panels ----------------------------------------------------------------

void ChartStation::add_tools_button(ui::Dialog& d, ui::Rect r, int w, int h) {
  d.emplace<DrawnButton>(
      *this, r, [w, h](const Viewport& vp, int x, int y, bool down) { draw_chart_tools_button(vp, x, y, w, h, down); },
      nullptr,
      [this] { start_annotating(); });
}

std::unique_ptr<ui::Dialog> ChartStation::make_tools_panel() {
  // In the plotting panel's place, with its drag strip and zoom group. No
  // button has the focus: Enter must not press CLEAR.
  constexpr int kToolsPanelW = 0x129;
  auto d = std::make_unique<ui::Dialog>(env_.gui, panel_pos_.x, panel_pos_.y, kToolsPanelW, kChartToolsPanelH,
                                        ui::kDialogFramed | ui::kDialogNoFocus, &env_.screen());
  d->emplace<ui::OwnerDraw>([](const Viewport& gfx, ui::OwnerDraw&) {
    const int row_h = kChartToolButtonH + 2;
    const int small_h = kChartSmallButton + 2;
    ui::gui_fill_rect(gfx, kToolX - 1, kButtonRowY - 1, static_cast<int>(kChartToolCount) * kToolPitch + 1, row_h, 0);
    ui::gui_fill_rect(gfx, kUndoX - 1, kButtonRowY - 1, kChartToolButtonW + 2, row_h, 0);
    ui::gui_fill_rect(gfx, 0xba + kZoomDx, 0x2a, 0x5a, 0x17, 0);
    ui::gui_fill_rect(gfx, kInkX - 1, kSmallRowY - 1, static_cast<int>(kChartInkCount) * kSmallPitch + 1, small_h, 0);
    ui::gui_fill_rect(gfx, kSymbolX - 1, kSmallRowY - 1, static_cast<int>(kChartSymbolCount) * kSmallPitch + 1,
                      small_h, 0);
    ui::gui_fill_rect(gfx, kSwitchX - 1, kSwitchY - 1, 0x49 * 2 + 1, 0x17, 0);
  });
  d->emplace<ui::TitleBar>(std::string("CHART TOOLS"));
  for (std::size_t i = 0; i < kChartToolCount; ++i) {
    const auto tool = static_cast<ChartTool>(i);
    const int x = kToolX + static_cast<int>(i) * kToolPitch;
    d->emplace<DrawnButton>(
        *this, ui::Rect{x, kButtonRowY, x + kChartToolButtonW - 1, kButtonRowY + kChartToolButtonH - 1},
        [tool](const Viewport& vp, int bx, int by, bool down) { draw_chart_tool_button(vp, bx, by, tool, down); },
        [this, tool] { return tool_ == tool; }, [this, tool] { select_tool(tool); });
  }
  d->emplace<DrawnButton>(
      *this, ui::Rect{kUndoX, kButtonRowY, kUndoX + kChartToolButtonW - 1, kButtonRowY + kChartToolButtonH - 1},
      [](const Viewport& vp, int bx, int by, bool down) { draw_chart_undo_button(vp, bx, by, down); }, nullptr,
      [this] { undo_annotation(); });
  for (std::size_t i = 0; i < kChartInkCount; ++i) {
    const auto ink = static_cast<ChartInk>(i);
    const int x = kInkX + static_cast<int>(i) * kSmallPitch;
    d->emplace<DrawnButton>(
        *this, ui::Rect{x, kSmallRowY, x + kChartSmallButton - 1, kSmallRowY + kChartSmallButton - 1},
        [this, ink](const Viewport& vp, int bx, int by, bool down) {
          draw_chart_ink_swatch(vp, bx, by, inks_[ink], down);
        },
        [this, ink] { return ink_ == ink; }, [this, ink] { select_ink(ink); });
  }
  for (std::size_t i = 0; i < kChartSymbolCount; ++i) {
    const auto symbol = static_cast<ChartSymbol>(i);
    const int x = kSymbolX + static_cast<int>(i) * kSmallPitch;
    d->emplace<DrawnButton>(
        *this, ui::Rect{x, kSmallRowY, x + kChartSmallButton - 1, kSmallRowY + kChartSmallButton - 1},
        [symbol](const Viewport& vp, int bx, int by, bool down) {
          draw_chart_symbol_button(vp, bx, by, symbol, down);
        },
        [this, symbol] { return symbol_ == symbol; }, [this, symbol] { select_symbol(symbol); });
  }
  add_zoom_buttons(*d, kZoomDx);
  // The way to the other panels (EXIT is MAP CONTROLS).
  add_panel_switch(*d, kSwitchX, kSwitchY, ChartPanel::kMap);
  add_panel_switch(*d, kSwitchX + 0x49, kSwitchY, ChartPanel::kNavigation);
  d->emplace<ui::Button>(kClearX, kSmallRowY, kClearW, std::string(env_.exe.c_string(kClearText)), [this](int) {
    clear_annotations();
    return ui::kResultAccept;
  });
  for (int k = 0; k < 4; ++k) {
    d->emplace<ui::Line>(7, 0x75 + k, 0x120, 0x75 + k, k < 2 ? 3 : 2);
  }
  // The prompt's two lines.
  std::array<ui::TextField*, 2> lines{};
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const int y = 0x7c + static_cast<int>(i) * 0xc;
    lines[i] = &d->emplace<ui::TextField>(8, y, 0x11f, y + 0xa, [this, i] { return tools_prompt()[i]; },
                                          ui::FontSlot::kCustom, TextAlign::kCentre);
    lines[i]->use_colours = true;
    lines[i]->fill = 6;
    lines[i]->custom = &env_.assets.font1;
  }
  prompt_control_ = lines[0];
  prompt_hint_ = lines[1];
  return d;
}

std::array<std::string, 2> ChartStation::tools_prompt() const {
  if (ruler_) {
    return {"RANGE AND BEARING", chart_ruler_text((*ruler_)[0], (*ruler_)[1])};
  }
  const bool touch = touch_screen();
  const std::string click = touch ? "TAP" : "CLICK";
  // How to scroll the chart when the tool takes the drags.
  const std::string scroll = touch ? "TWO FINGERS SCROLL AND ZOOM" : "THE MIDDLE BUTTON SCROLLS";
  switch (tool_) {
    case ChartTool::kPencil:
      return {"DRAG TO DRAW ON THE CHART", scroll};
    case ChartTool::kLine:
      if (draft_) {
        return {click + " OR DRAG THE NEXT POINT",
                touch ? "TAP THE LAST POINT TO END" : "RIGHT CLICK OR CLICK THE LAST TO END"};
      }
      return {click + " THE FIRST POINT, OR DRAG", scroll};
    case ChartTool::kCircle:
      return {"DRAG FROM THE CENTRE TO THE EDGE", scroll};
    case ChartTool::kMark:
      return {click + " TO PUT THE MARK", "DRAG TO SCROLL THE CHART"};
    case ChartTool::kNote:
      return {click + " WHERE THE NOTE GOES", "DRAG TO SCROLL THE CHART"};
    case ChartTool::kRuler:
      return {"DRAG FROM ONE POINT TO ANOTHER", scroll};
    case ChartTool::kEraser:
      return {touch ? "TAP A DRAWING TO ERASE IT" : "CLICK WHAT TO ERASE (FRAMED)", "DRAG TO SCROLL THE CHART"};
  }
  return {};
}

void ChartStation::tools_prompt_changed() {
  for (ui::Control* line : {prompt_control_, prompt_hint_}) {
    if (line != nullptr) {
      line->dirty = true;
    }
  }
}

bool ChartStation::touch_screen() const { return env_.gui.mouse().edges() == ui::PointerEdges::kNone; }

int ChartStation::pick_radius() const { return touch_screen() ? kTouchPickRadius : kPickRadius; }

bool ChartStation::on_line_end(std::int32_t x, std::int32_t y) const {
  if (!draft_ || draft_->kind != ChartAnnotation::Kind::kLine) {
    return false;
  }
  const geom::WorldXZ last = draft_->points.back();
  std::int32_t lx = 0;
  std::int32_t ly = 0;
  return view_.point_on_screen(last.x, last.z, lx, ly) && std::abs(lx - x) <= pick_radius() &&
         std::abs(ly - y) <= pick_radius();
}

// --- Commands ----------------------------------------------------------------

void ChartStation::start_annotating() {
  saved_click_mode_ = click_mode_;
  click_mode_ = ChartClickMode::kAnnotate;
  set_cursor(kToolCursor);
  panels_.request(static_cast<int>(ChartPanel::kTools));
  env_.bar.set_escape_passthrough(true);
}

void ChartStation::exit_annotating() {
  finish_line();
  panels_.request(static_cast<int>(ChartPanel::kMap));
  std::swap(click_mode_, saved_click_mode_);
  set_cursor(kDefaultCursor);
  env_.bar.set_escape_passthrough(false);
  ruler_.reset();
  draft_.reset();
  highlight_ = -1;
  dirty_ = true;
}

void ChartStation::select_tool(ChartTool tool) {
  finish_line();
  tool_ = tool;
  if (highlight_ != -1) {
    highlight_ = -1;
    dirty_ = true;
  }
  tools_prompt_changed();
}

void ChartStation::select_ink(ChartInk ink) { ink_ = ink; }

void ChartStation::select_symbol(ChartSymbol symbol) { symbol_ = symbol; }

void ChartStation::undo_annotation() {
  draft_.reset();
  if (annotations_.undo()) {
    highlight_ = -1;
    dirty_ = true;
  }
}

void ChartStation::clear_annotations() {
  finish_line();
  if (annotations_.empty()) {
    return;
  }
  env_.bar.ask_yes_no("CLEAR THE CHART", "Erase every line, mark and note", "drawn on this chart?", [this](bool yes) {
    if (yes) {
      annotations_.clear();
      highlight_ = -1;
    }
    dirty_ = true;
  });
}

void ChartStation::restore_annotations(std::vector<ChartAnnotation> items) {
  annotations_.assign(std::move(items));
  draft_.reset();
  highlight_ = -1;
  dirty_ = true;
}

void ChartStation::wheel_zoom(std::int32_t x, std::int32_t y, int notches) {
  zoom_about(x, y, chart_scaled_zoom(view_.zoom(), std::exp2(notches / kNotchesPerDoubling)));
}

void ChartStation::zoom_about(std::int32_t x, std::int32_t y, std::int32_t zoom) {
  if (zoom == view_.zoom()) {
    return;
  }
  if (env_.options.chart_follow_sub != 0) {
    // The boat's chart position before zooming in, while it is on the
    // screen's 16.16 range.
    const fx::Vec3 sub = env_.mission.player().position();
    const std::array<std::int32_t, 2> c = view_.world_to_chart(sub.x, sub.z);
    view_.set_zoom(zoom);
    view_.set_center(c[0], c[1]);
  } else {
    const std::array<std::int32_t, 2> before = view_.screen_to_chart(view_.x0() + x, view_.y0() + y);
    view_.set_zoom(zoom);
    const std::array<std::int32_t, 2> after = view_.screen_to_chart(view_.x0() + x, view_.y0() + y);
    view_.set_center(view_.center_x() + before[0] - after[0], view_.center_y() + before[1] - after[1]);
  }
  dirty_ = true;
}

bool ChartStation::pinch() {
  const std::vector<ui::Touch>& touches = env_.gui.mouse().touches();
  if (touches.size() < 2 || zoom_box_.is_open()) {
    pinch_.reset();
    return false;
  }
  // The first two fingers: their middle keeps its chart point, their
  // distance scales the zoom.
  const double mx = (touches[0].x + touches[1].x) / 2.0 - view_.x0();
  const double my = (touches[0].y + touches[1].y) / 2.0 - view_.y0();
  const double distance = std::max(std::hypot(touches[1].x - touches[0].x, touches[1].y - touches[0].y), 1.0);
  const int x = static_cast<int>(std::lround(mx));
  const int y = static_cast<int>(std::lround(my));
  if (!pinch_) {
    // The finger that pressed first is no click or drag.
    pointer_.cancel();
    panning_ = false;
    extending_ = false;
    if (env_.options.chart_follow_sub != 0) {
      toggle_follow();
    }
    pinch_ = Pinch{.distance = distance, .zoom = view_.zoom(),
                   .anchor = view_.screen_to_chart(view_.x0() + x, view_.y0() + y)};
    return true;
  }
  const std::int32_t cx = view_.center_x();
  const std::int32_t cy = view_.center_y();
  const std::int32_t zoom = view_.zoom();
  view_.set_zoom(chart_scaled_zoom(pinch_->zoom, distance / pinch_->distance));
  const std::array<std::int32_t, 2> under = view_.screen_to_chart(view_.x0() + x, view_.y0() + y);
  view_.set_center(view_.center_x() + pinch_->anchor[0] - under[0], view_.center_y() + pinch_->anchor[1] - under[1]);
  dirty_ = dirty_ || view_.zoom() != zoom || view_.center_x() != cx || view_.center_y() != cy;
  return true;
}

// --- The pointer ---------------------------------------------------------------

bool ChartStation::tools_event(const ui::Event& event) {
  // Esc or a right click ends the line being drawn, else the toolbox.
  if (!zoom_box_.is_open() && panel() == ChartPanel::kTools &&
      ((event.is_key() && event.key == ui::kKeyEsc) || (event.flags & ui::kEventRightUp) != 0)) {
    if (draft_) {
      finish_line();
    } else {
      exit_annotating();
    }
    return true;
  }
  const int x = event.x - view_.x0();
  const int y = event.y - view_.y0();
  const bool on_map = !panels_.hovered() && event.x >= view_.x0() && event.x <= view_.x1() &&
                      event.y >= view_.y0() && event.y <= view_.y1();
  if (event.wheel != 0 && !event.is_key()) {
    if (zoom_box_.is_open()) {
      // The wheel grows and shrinks the zoom box like Z and X.
      for (int i = 0; i < std::abs(event.wheel); ++i) {
        event.wheel > 0 ? zoom_box_grow() : zoom_box_shrink();
      }
    } else if (on_map && !panels_.locked()) {
      wheel_zoom(x, y, event.wheel);
    }
    return true;
  }
  // The middle button drags the map whatever the tool.
  if ((event.flags & ui::kEventMiddleDown) != 0 && !zoom_box_.is_open() && on_map) {
    pointer_.press(x, y, ui::kButtonMiddle);
    return true;
  }
  return false;
}

void ChartStation::press_map(std::int32_t x, std::int32_t y) { pointer_.press(x, y, ui::kButtonLeft); }

void ChartStation::update_map_pointer() {
  ui::Mouse& mouse = env_.gui.mouse();
  if (zoom_box_.is_open()) {
    pointer_.cancel();
    return;
  }
  if (env_.ui.features.chart_tools && pinch()) {
    return;
  }
  const int x = mouse.x() - view_.x0();
  const int y = mouse.y() - view_.y0();
  const bool on_map = !panels_.hovered() && mouse.x() >= view_.x0() && mouse.x() <= view_.x1() &&
                      mouse.y() >= view_.y0() && mouse.y() <= view_.y1();
  const ChartPointer::Step step = pointer_.update(x, y, mouse.buttons());
  if (step == ChartPointer::Step::kClick) {
    // The original's press, on the release.
    click(pointer_.start_x(), pointer_.start_y());
  } else if (step != ChartPointer::Step::kNone) {
    drag_map(step, x, y);
  }
  if (click_mode_ == ChartClickMode::kAnnotate) {
    int highlight = -1;
    if (tool_ == ChartTool::kEraser && on_map && !pointer_.active()) {
      if (const std::optional<std::size_t> i =
              pick_chart_annotation(view_, annotations_, env_.assets.font1, x, y, pick_radius())) {
        highlight = static_cast<int>(*i);
      }
    }
    if (highlight != highlight_) {
      highlight_ = highlight;
      dirty_ = true;
    }
    // The line tool's rubber band follows the pointer.
    if (draft_ && tool_ == ChartTool::kLine && tools_mouse_ != std::array{x, y}) {
      force_overlays_ = true;
    }
  }
  tools_mouse_ = {x, y};
}

void ChartStation::update_tool_cursor() {
  if (click_mode_ == ChartClickMode::kSelectTarget || pointer_.active() || panels_.dragging() ||
      env_.modals.modal_active()) {
    return;
  }
  const ui::Mouse& mouse = env_.gui.mouse();
  const bool on_map = !panels_.hovered() && mouse.x() >= view_.x0() && mouse.x() <= view_.x1() &&
                      mouse.y() >= view_.y0() && mouse.y() <= view_.y1();
  // The plot's cursor is the tools' cross too.
  set_cursor(on_map ? kToolCursor : kDefaultCursor);
}

void ChartStation::drag_map(ChartPointer::Step step, std::int32_t x, std::int32_t y) {
  if (step == ChartPointer::Step::kDragBegin) {
    const bool tool = pointer_.button() == ui::kButtonLeft && click_mode_ == ChartClickMode::kAnnotate;
    extending_ = tool && tool_ == ChartTool::kLine;
    panning_ = !tool || (!drag_tool(tool_) && !extending_);
    if (extending_ && !on_line_end(pointer_.start_x(), pointer_.start_y())) {
      // Away from the line's end: a new line from the press.
      finish_line();
      const std::array<std::int32_t, 2> s = view_.screen_to_world(pointer_.start_x(), pointer_.start_y());
      draft_ = ChartAnnotation{.kind = ChartAnnotation::Kind::kLine, .ink = ink_, .points = {{s[0], s[1]}}, .text = {}};
      tools_prompt_changed();
    }
    if (panning_) {
      drag_centre_ = {view_.center_x(), view_.center_y()};
      if (env_.options.chart_follow_sub != 0) {
        toggle_follow();
      }
      set_cursor(kDragCursor);
    }
  }
  if (panning_) {
    // The drag scroll: the chart point under the press stays under the
    // pointer.
    if (step == ChartPointer::Step::kDragEnd) {
      panning_ = false;
      set_cursor(click_mode_ == ChartClickMode::kSelectTarget ? kDefaultCursor : kToolCursor);
      return;
    }
    const std::int32_t cx = view_.center_x();
    const std::int32_t cy = view_.center_y();
    view_.set_center(drag_centre_[0] - (x - pointer_.start_x()) * view_.step(),
                     drag_centre_[1] - (y - pointer_.start_y()) * view_.step());
    dirty_ = dirty_ || view_.center_x() != cx || view_.center_y() != cy;
    return;
  }
  if (extending_) {
    // The rubber band follows the pointer; the release places the point.
    if (step == ChartPointer::Step::kDragEnd) {
      extending_ = false;
      if (draft_ && draft_->kind == ChartAnnotation::Kind::kLine && !on_line_end(x, y)) {
        const std::array<std::int32_t, 2> p = view_.screen_to_world(x, y);
        draft_->points.push_back({p[0], p[1]});
        tools_prompt_changed();
      }
    }
    force_overlays_ = true;
    return;
  }
  if (step == ChartPointer::Step::kDragEnd && !draft_ && !ruler_) {
    return;
  }
  const std::array<std::int32_t, 2> p = view_.screen_to_world(x, y);
  const geom::WorldXZ at{p[0], p[1]};
  if (step == ChartPointer::Step::kDragBegin) {
    const std::array<std::int32_t, 2> s = view_.screen_to_world(pointer_.start_x(), pointer_.start_y());
    const geom::WorldXZ start{s[0], s[1]};
    switch (tool_) {
      case ChartTool::kPencil:
        draft_ = ChartAnnotation{.kind = ChartAnnotation::Kind::kStroke, .ink = ink_, .points = {start, at}, .text = {}};
        break;
      case ChartTool::kCircle:
        draft_ = ChartAnnotation{.kind = ChartAnnotation::Kind::kCircle, .ink = ink_, .points = {start, at}, .text = {}};
        break;
      default:
        ruler_ = std::array{start, at};
        break;
    }
  } else if (tool_ == ChartTool::kRuler && ruler_) {
    (*ruler_)[1] = at;
  } else if (draft_ && draft_->kind == ChartAnnotation::Kind::kStroke) {
    std::int32_t lx = 0;
    std::int32_t ly = 0;
    const geom::WorldXZ last = draft_->points.back();
    const bool near = view_.point_on_screen(last.x, last.z, lx, ly) && std::abs(lx - x) < kStrokeStep &&
                      std::abs(ly - y) < kStrokeStep;
    if (!near || step == ChartPointer::Step::kDragEnd) {
      draft_->points.push_back(at);
    }
  } else if (draft_ && draft_->kind == ChartAnnotation::Kind::kCircle) {
    draft_->points[1] = at;
  }
  if (step == ChartPointer::Step::kDragEnd && draft_) {
    if (draft_->points.front() != draft_->points.back()) {
      annotations_.add(std::move(*draft_));
      dirty_ = true;
    }
    draft_.reset();
  }
  if (ruler_) {
    tools_prompt_changed();
  }
  force_overlays_ = true;
}

void ChartStation::tools_click(std::int32_t x, std::int32_t y) {
  const std::array<std::int32_t, 2> p = view_.screen_to_world(x, y);
  const geom::WorldXZ at{p[0], p[1]};
  switch (tool_) {
    case ChartTool::kLine: {
      if (!draft_) {
        draft_ = ChartAnnotation{.kind = ChartAnnotation::Kind::kLine, .ink = ink_, .points = {at}, .text = {}};
      } else {
        // A click on the last point ends the line.
        if (on_line_end(x, y)) {
          finish_line();
        } else {
          draft_->points.push_back(at);
        }
      }
      tools_prompt_changed();
      force_overlays_ = true;
      break;
    }
    case ChartTool::kMark:
      annotations_.add({.kind = ChartAnnotation::Kind::kMark, .ink = ink_, .symbol = symbol_, .points = {at}, .text = {}});
      dirty_ = true;
      break;
    case ChartTool::kNote:
      ask_note(at);
      break;
    case ChartTool::kRuler:
      if (ruler_) {
        ruler_.reset();
        tools_prompt_changed();
        force_overlays_ = true;
      }
      break;
    case ChartTool::kEraser:
      if (const std::optional<std::size_t> i =
              pick_chart_annotation(view_, annotations_, env_.assets.font1, x, y, pick_radius())) {
        annotations_.erase(*i);
        highlight_ = -1;
        dirty_ = true;
      }
      break;
    case ChartTool::kPencil:
    case ChartTool::kCircle:
      break;
  }
}

void ChartStation::finish_line() {
  if (!draft_ || draft_->kind != ChartAnnotation::Kind::kLine) {
    return;
  }
  if (draft_->points.size() >= 2) {
    annotations_.add(std::move(*draft_));
    dirty_ = true;
  }
  draft_.reset();
  tools_prompt_changed();
  force_overlays_ = true;
}

void ChartStation::ask_note(geom::WorldXZ at) {
  // A name dialog of the game's kind; the mission waits meanwhile. Placed
  // like the in-game menus, for the 640-wide screen (moved to the middle of
  // a wide one when it opens).
  constexpr int kW = 0x140;
  constexpr int kH = 0x8a;
  auto d = std::make_unique<ui::Dialog>(env_.gui, (kScreenWidth - kW) / 2, 0x80, kW, kH,
                                        ui::kDialogTitled | ui::kDialogFramed | ui::kDialogCancel);
  d->emplace<ui::TitleBar>(std::string("CHART NOTE"));
  auto& entry = d->emplace<ui::TextEntry>(0xc, 0x46, kW - 0x18, static_cast<int>(kChartNoteMaxLength) + 1,
                                          std::string(),
                                          [](std::string& text, int) { return text.empty() ? 0 : ui::kResultAccept; });
  d->emplace<ui::Button>(0x1a, 0x6a, 100, std::string(env_.exe.c_string(kCancelText)), ui::return_cancel);
  d->emplace<ui::Button>(kW - 0x1a - 100, 0x6a, 100, std::string(env_.exe.c_string(kAcceptText)),
                         [&entry](int) { return entry.text.empty() ? 0 : ui::kResultAccept; });
  ui::TextEntry* text = &entry;
  const ChartInk ink = ink_;
  // The ordinary pointer over the dialog (`update_tool_cursor` puts the
  // cross back after it).
  set_cursor(kDefaultCursor);
  env_.modals.run_modal(std::move(d), ModalFrame::kGrey, [this, text, at, ink](int result) {
    if (result == ui::kResultAccept && !text->text.empty()) {
      annotations_.add({.kind = ChartAnnotation::Kind::kNote, .ink = ink, .points = {at}, .text = text->text});
    }
    dirty_ = true;
  });
}

// --- The pictures --------------------------------------------------------------

void ChartStation::draw_annotations() {
  Image8& screen = env_.screen();
  draw_chart_annotations(Viewport{&screen, view_.x0(), view_.y0(), view_.x1(), view_.y1()}, view_, annotations_,
                         inks_, env_.assets.font1, highlight_);
}

void ChartStation::draw_tool_overlays() {
  Image8& screen = env_.screen();
  const Viewport vp{&screen, view_.x0(), view_.y0(), view_.x1(), view_.y1()};
  if (draft_) {
    ChartAnnotation shown = *draft_;
    if (shown.kind == ChartAnnotation::Kind::kLine && !panels_.hovered() && (extending_ || !touch_screen())) {
      // The rubber band to the pointer (a finger's only while it drags).
      const ui::Mouse& mouse = env_.gui.mouse();
      const std::array<std::int32_t, 2> p = view_.screen_to_world(mouse.x() - view_.x0(), mouse.y() - view_.y0());
      shown.points.push_back({p[0], p[1]});
    }
    if (shown.points.size() >= 2) {
      draw_chart_annotation(vp, view_, shown, inks_, env_.assets.font1);
    } else {
      // The first point of a line, the pointer over the panel.
      std::int32_t x = 0;
      std::int32_t y = 0;
      if (view_.point_on_screen(shown.points[0].x, shown.points[0].z, x, y)) {
        ui::fill_rect(vp, x - 1, y - 1, x + 1, y + 1, inks_[shown.ink]);
      }
    }
  }
  if (ruler_) {
    draw_chart_ruler(vp, view_, (*ruler_)[0], (*ruler_)[1], inks_[ChartInk::kWhite], env_.assets.font1);
  }
}

}  // namespace sh
