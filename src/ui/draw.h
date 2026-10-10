// The drawing primitives the GUI skins use, as the original library has
// them: `gfx_draw_line_clipped` (0xa9f61), `gfx_fill_polygon` (0xaf760) for
// rectangles and triangles, `gfx_put_pixel` (0xa9db0), `gfx_fill_ellipse`
// (0xad2e9), and the GUI helpers built on them (`gui_fill_rect`,
// `gui_draw_raised_box`, `gui_draw_focus_rect`). Coordinates are relative to
// the viewport origin and everything is clipped to the viewport.
#pragma once

#include <cstdint>
#include <functional>

#include "gfx/viewport.h"

namespace sh::ui {

// Plots one pixel (`gfx_put_pixel`).
void put_pixel(const Viewport& vp, int x, int y, std::uint8_t colour);

// Draws the inclusive line (x0, y0)-(x1, y1) in `colour`, from the first
// point to the second (mode 0 of `gfx_draw_line_clipped`): the minor
// coordinate follows a 32.32 DDA rounded to nearest.
void draw_line(const Viewport& vp, int x0, int y0, int x1, int y1, std::uint8_t colour);

// The same line calling `plot` for each pixel inside the clip rectangle, in
// drawing order, with viewport-relative coordinates (mode 2).
void draw_line(const Viewport& vp, int x0, int y0, int x1, int y1, const std::function<void(int, int)>& plot);

// Fills the inclusive rectangle (x0, y0)-(x1, y1) (`gfx_fill_polygon` with
// four axis-aligned vertices).
void fill_rect(const Viewport& vp, int x0, int y0, int x1, int y1, std::uint8_t colour);

// A polygon vertex.
struct Point {
  int x = 0;
  int y = 0;
};

// Fills the convex triangle (`gfx_fill_polygon` with three vertices):
// scanlines from the top vertex, each spanning the left and right edges
// stepped in 16.16, vertices inclusive.
void fill_triangle(const Viewport& vp, Point a, Point b, Point c, std::uint8_t colour);

// Fills the ellipse of radii rx, ry centred on (cx, cy) by the midpoint
// algorithm (`gfx_fill_ellipse`).
void fill_ellipse(const Viewport& vp, int cx, int cy, int rx, int ry, std::uint8_t colour);

// Fills the w x h rectangle at (x, y) (`gui_fill_rect`, 0x42bd8).
void gui_fill_rect(const Viewport& vp, int x, int y, int w, int h, std::uint8_t colour);

// Draws a w x h box filled with `fill` and a 2-pixel bevel: `left` on the
// left and top edges, `right` on the right and bottom ones
// (`gui_draw_raised_box`, 0x42d91, once its colours are chosen).
void gui_raised_box(const Viewport& vp, int x, int y, int w, int h, std::uint8_t fill, std::uint8_t left,
                    std::uint8_t right);

// Draws the outline of the w x h rectangle at (x, y), solid or dotted
// (`gui_draw_focus_rect`, 0x42c4a): the dotted pattern plots every other
// pixel along the four edges in turn, top, right, bottom then left, each
// edge including both of its corners.
void gui_focus_rect(const Viewport& vp, int x, int y, int w, int h, std::uint8_t colour, bool dotted);

}  // namespace sh::ui
