// The chart's annotation tools (not in the original; `Features::chart_tools`):
// the captain's pencil work on the map, kept in world coordinates so that it
// follows the view at every zoom.
// - `ChartAnnotations`: the strokes, lines, circles, marks and notes, with a
//   one-step undo, and their "CHRT" save section;
// - `ChartInks`: the ink colours, the palette's nearest matches;
// - `draw_chart_annotations`, `draw_chart_ruler`, `pick_chart_annotation`:
//   their pictures over the map of a `ChartView` and the eraser's hit test;
// - `chart_range_text`, `chart_ruler_text`: the readings, in yards below 10
//   nautical miles, else in nautical miles;
// - `ChartPointer`: tells a click on the map from a drag;
// - the toolbox buttons, in the style of the CHART.SHP buttons.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/fnt.h"
#include "assets/pal.h"
#include "game/stations/chart_view.h"
#include "gfx/viewport.h"
#include "sim/geometry.h"

namespace sh {

// The tools of the toolbox, in button order.
enum class ChartTool : std::uint8_t { kPencil, kLine, kCircle, kMark, kNote, kRuler, kEraser };
inline constexpr std::size_t kChartToolCount = 7;

enum class ChartInk : std::uint8_t { kBlack, kRed, kYellow, kWhite };
inline constexpr std::size_t kChartInkCount = 4;

// The symbols of the mark tool.
enum class ChartSymbol : std::uint8_t { kCross, kCircle, kTriangle, kSquare };
inline constexpr std::size_t kChartSymbolCount = 4;

// The longest note.
inline constexpr std::size_t kChartNoteMaxLength = 31;

struct ChartAnnotation {
  // kStroke: a freehand pencil line; kLine: a polyline, labelled with its
  // segments' ranges and bearings; kCircle: the centre then a point of the
  // circle; kMark, kNote: one point.
  enum class Kind : std::uint8_t { kStroke, kLine, kCircle, kMark, kNote };
  Kind kind = Kind::kStroke;
  ChartInk ink = ChartInk::kBlack;
  ChartSymbol symbol = ChartSymbol::kCross;  // kMark
  std::vector<geom::WorldXZ> points;
  std::string text;  // kNote

  bool operator==(const ChartAnnotation&) const = default;
};

// The annotations of a chart, oldest first (drawn in that order), and the
// state before the last change, which `undo` puts back.
class ChartAnnotations {
 public:
  const std::vector<ChartAnnotation>& items() const { return items_; }
  bool empty() const { return items_.empty(); }

  void add(ChartAnnotation a);
  // Removes item `index` (ignored out of range).
  void erase(std::size_t index);
  // Removes every item (ignored when there is none).
  void clear();
  // Puts back the state before the last add, erase or clear; returns false
  // when there is nothing to undo. Only one step is kept.
  bool undo();
  bool can_undo() const { return undo_.has_value(); }
  // Replaces the items, forgetting the undo step (a restored save).
  void assign(std::vector<ChartAnnotation> items);

  // Returns the "CHRT" save section: u32 version 1, u32 count, then per
  // item u8 kind, ink and symbol, u32 point count, i32 x and z per point,
  // u8 text length and the text.
  std::vector<std::uint8_t> encode() const;
  // Returns the items of a "CHRT" section. Throws `DataError` when it is
  // truncated or malformed.
  static std::vector<ChartAnnotation> decode(std::span<const std::uint8_t> data);

 private:
  std::vector<ChartAnnotation> items_;
  std::optional<std::vector<ChartAnnotation>> undo_;
};

// The palette indices of the inks: the colours nearest to them in the
// chart's palette.
struct ChartInks {
  std::array<std::uint8_t, kChartInkCount> index{};

  static ChartInks nearest(const VgaPalette& palette);
  std::uint8_t operator[](ChartInk ink) const { return index[static_cast<std::size_t>(ink)]; }
};

// Returns a range as the tools show it: tens of yards below 10 nautical
// miles ("4210 YDS"), else nautical miles and tenths ("12.4 NM").
std::string chart_range_text(double yards);
// Returns the range and true bearing from `from` to `to` ("8450 YDS 047"
// and the degree glyph).
std::string chart_ruler_text(geom::WorldXZ from, geom::WorldXZ to);

// Draws `annotations` into `vp` (the map's viewport) as `view` shows them:
// lines with the range and bearing of each segment long enough to hold it
// and a polyline's total by its last point, circles with their radius by
// their north-east point. Parts off the view are clipped. Item `highlight`
// (the eraser's, -1 none) is framed.
void draw_chart_annotations(const Viewport& vp, const ChartView& view, const ChartAnnotations& annotations,
                            const ChartInks& inks, const Font& font, int highlight = -1);
// Draws `a` alone as `draw_chart_annotations` does (the drawing in
// progress).
void draw_chart_annotation(const Viewport& vp, const ChartView& view, const ChartAnnotation& a, const ChartInks& inks,
                           const Font& font);

// Returns the newest annotation drawn within `radius` pixels of viewport
// pixel (`x`, `y`): near a line, a circle's outline, a mark, or a note's
// point or text.
std::optional<std::size_t> pick_chart_annotation(const ChartView& view, const ChartAnnotations& annotations,
                                                 const Font& font, int x, int y, int radius = 4);

// Draws the ruler from `from` to `to`: a dashed line between two rings,
// its reading boxed by its middle.
void draw_chart_ruler(const Viewport& vp, const ChartView& view, geom::WorldXZ from, geom::WorldXZ to,
                      std::uint8_t colour, const Font& font);

// Draws mark `symbol` centred on (x, y) of `vp`.
void draw_chart_symbol(const Viewport& vp, int x, int y, ChartSymbol symbol, std::uint8_t colour);

// The scale bar of the extended chart's panels: the longest round range
// (1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6 or 8 times a power of ten) that the
// original's 256-pixel bar holds (176000 / zoom yards), in nautical miles
// from 10, else in yards.
struct ChartScale {
  int length = 0;      // pixels
  int value = 0;       // the number at its end: nautical miles, or tens of yards
  bool yards = false;  // "(x10) YARDS", else "NAUTICAL MILES"

  bool operator==(const ChartScale&) const = default;
};
ChartScale chart_nautical_scale(std::int32_t zoom_int);
// Draws a scale bar `length` pixels long at (x, y) of `vp` as the original's
// CHART.SHP bar is drawn: four blocks of five divisions in the panel's two
// blues, ticks above.
void draw_chart_scale_bar(const Viewport& vp, int x, int y, int length);

// The distance a press must move to become a drag (pixels, either axis).
inline constexpr int kChartDragThreshold = 4;

// Tells a click on the map from a drag: `press` starts tracking a button,
// then `update` is called every frame with the pointer until it reports the
// click or the end of the drag.
class ChartPointer {
 public:
  enum class Step : std::uint8_t { kNone, kClick, kDragBegin, kDragMove, kDragEnd };

  void press(int x, int y, std::uint8_t button);
  // Returns what the pointer at (`x`, `y`) with `buttons` held did since
  // the last call: kClick (at the press point) on a release without
  // having moved, kDragBegin once it moved `kChartDragThreshold` pixels
  // from the press, kDragMove after each later move, kDragEnd on the
  // release after a drag.
  Step update(int x, int y, std::uint8_t buttons);
  // Forgets the press.
  void cancel() { button_ = 0; }

  bool active() const { return button_ != 0; }
  bool dragging() const { return button_ != 0 && dragging_; }
  std::uint8_t button() const { return button_; }
  int start_x() const { return start_x_; }
  int start_y() const { return start_y_; }

 private:
  std::uint8_t button_ = 0;
  bool dragging_ = false;
  int start_x_ = 0;
  int start_y_ = 0;
  int last_x_ = 0;
  int last_y_ = 0;
};

// The size of a toolbox button.
inline constexpr int kChartToolButtonW = 21;
inline constexpr int kChartToolButtonH = 20;
// The size of the symbol buttons and ink swatches.
inline constexpr int kChartSmallButton = 16;
// The width of the MAP CONTROLS button that opens the toolbox.
inline constexpr int kChartToolsButtonW = 42;

// Draws the button of `tool` at (x, y) of `vp`: a grey plate with the tool
// engraved, sunk when `down`.
void draw_chart_tool_button(const Viewport& vp, int x, int y, ChartTool tool, bool down);
// Draws the undo button (a hooked arrow).
void draw_chart_undo_button(const Viewport& vp, int x, int y, bool down);
void draw_chart_symbol_button(const Viewport& vp, int x, int y, ChartSymbol symbol, bool down);
// Draws an ink swatch, framed when `selected`.
void draw_chart_ink_swatch(const Viewport& vp, int x, int y, std::uint8_t colour, bool selected);
// Draws the MAP CONTROLS button that opens the toolbox (dividers and a
// pencil).
void draw_chart_tools_button(const Viewport& vp, int x, int y, bool down);

}  // namespace sh
