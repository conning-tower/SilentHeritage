// The original drawing target: a bitmap plus a clip rectangle whose top-left
// corner is also the origin of the drawing coordinates.
#pragma once

#include "core/image.h"

namespace sh {

// An inclusive rectangle in bitmap coordinates.
struct ClipRect {
  int left = 0;
  int top = 0;
  int right = -1;
  int bottom = -1;

  bool empty() const { return right < left || bottom < top; }
  bool contains(int x, int y) const { return x >= left && x <= right && y >= top && y <= bottom; }
};

// A viewport `{bitmap*, x0, y0, x1, y1}` as made by `viewport_new`: drawing
// coordinates are relative to (x0, y0), and pixels are clipped to the
// inclusive rectangle x0..x1, y0..y1 intersected with the bitmap.
struct Viewport {
  Image8* bitmap = nullptr;
  int x0 = 0;
  int y0 = 0;
  int x1 = -1;
  int y1 = -1;

  // Returns a viewport covering the whole of `image`.
  static Viewport full(Image8& image) { return {&image, 0, 0, image.width() - 1, image.height() - 1}; }

  // Returns false when the bitmap is missing or has no pixels (the
  // original's "bad bitmap", error -1).
  bool valid() const { return bitmap != nullptr && bitmap->width() > 0 && bitmap->height() > 0; }

  // Returns the clip rectangle in bitmap coordinates (requires `valid()`).
  ClipRect clip() const {
    return {x0 < 1 ? 0 : x0, y0 < 1 ? 0 : y0, x1 < bitmap->width() - 1 ? x1 : bitmap->width() - 1,
            y1 < bitmap->height() - 1 ? y1 : bitmap->height() - 1};
  }
};

// The status codes of the original blitters.
enum class DrawResult : int {
  kOk = 0,
  kBadBitmap = -1,   // no target pixels
  kEmptyClip = -2,   // the clip rectangle is empty
  kOutside = -3,     // nothing of the shape is inside the clip rectangle
  kEmptyFrame = -4,  // the sprite frame has nothing to draw
};

}  // namespace sh
