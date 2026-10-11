// What fills a wide screen beside a 640-wide frame (`Features::wide_screen`;
// not in the original): the menus' brass, the desk wood, or pictures of the
// screen's own, in its palette, that continue its art.
#pragma once

#include <cstdint>

#include "core/image.h"

namespace sh {

struct SideFill {
  enum class Kind : std::uint8_t {
    kBrass,    // ART/MENUBGND.PCX, the menus' brass
    kWood,     // the desk wood of ART/WOOD.PCX
    kPicture,  // `left` and `right`
  };
  Kind kind = Kind::kBrass;
  // For `kPicture`: the columns left and right of the frame (480 rows),
  // shown through the frame's palette. `left` ends against the frame,
  // `right` starts there; what they do not cover is black.
  Image8 left;
  Image8 right;
};

}  // namespace sh
