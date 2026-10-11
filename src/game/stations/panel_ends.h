// The ends of the bridge's and the TBT's gauge panels on a wide screen (not
// in the original): the panel art stops at the 640 border, which cuts the
// mounts of its two edge gauges. Beyond it come the cut parts of those
// mounts, taken from the same side of the other edge gauge (the two are
// alike and lit alike, from the top left) within the mount's outline, and
// the panel's plain plate, a clean plate section copied verbatim and
// repeated. Everything is made of
// the panel's own pixels, in its palette.
#pragma once

#include "core/image.h"
#include "ui/skin.h"

namespace sh {

// Where the parts of the extension come from, in panel coordinates (the
// 640 columns, row 0 its top).
struct PanelEndsLayout {
  // Rows 0..`band_rows - 1` (the bands above the mounts) continue the
  // panel's two border columns, keeping their dither.
  int band_rows = 0;
  // The left edge gauge's centre column (times two: it falls between
  // columns) and row; the right one is `gauge_shift` columns right of it,
  // and the two are alike. A pixel left of column 0 is the one
  // `gauge_shift` columns right of it (the right gauge's left side), one
  // right of column 639 the one `gauge_shift` columns left of it (the left
  // gauge's right side), when it is on the gauge's mount.
  int centre_x2 = 0;
  int centre_y = 0;
  int gauge_shift = 0;
  // The mount: within `mount_radius` of the centre (the bezel and the
  // mount around it) or `lug_radius` of a bolt, `bolt_dx2 / 2` columns from the
  // centre's on rows `bolt_top` and `bolt_bottom`.
  int mount_radius = 0;
  int lug_radius = 0;
  int bolt_dx2 = 0;
  int bolt_top = 0;
  int bolt_bottom = 0;
  // The plain plate sections repeated beyond the mounts and below the bands
  // (in the `plate` picture; even sizes, so that the art's dither pattern
  // runs on).
  ui::Rect left_plate;
  ui::Rect right_plate;
};

// The two extensions: `left` ends against the panel's column 0, `right`
// starts after its column 639; both as tall as the panel.
struct PanelEnds {
  Image8 left;
  Image8 right;
};

// Returns the extensions of `panel` (640 columns) `left_width` and
// `right_width` wide, the plate taken from `plate` (the panel itself, or
// another panel in the same palette).
PanelEnds make_panel_ends(const Image8& panel, const Image8& plate, int left_width, int right_width,
                          const PanelEndsLayout& layout);

}  // namespace sh
