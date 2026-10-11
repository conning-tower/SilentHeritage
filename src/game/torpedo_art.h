// The torpedo types told apart by the extended variant (not in the
// original; `Features::torpedo_types`): the status page's torpedo pictures
// (STATUST.SHP frames 0xc + 2 x (id - 1), + 1 while moving), where the
// Mark 18-1 is recoloured steel blue with one white band and the 18-2
// orange with two (the original draws both alike), and the Mark 27 is the
// 18-1's shortened, dark, with green nose bands. They are built from the
// game's art in the nearest colours of the page's palette.
#pragma once

#include <array>
#include <cstdint>

#include "assets/pal.h"
#include "assets/shp.h"
#include "core/image.h"
#include "gfx/viewport.h"

namespace sh {

// WEAPONS.DAT ids of the Mark 10 and 18s (the Mark 14's `kMark14Id` is in
// sim/torpedo.h, the Mark 27's `kMark27Id` in assets/dat_tables.h).
inline constexpr std::uint32_t kMark10Id = 1;
inline constexpr std::uint32_t kMark18_1Id = 3;
inline constexpr std::uint32_t kMark18_2Id = 4;

// A torpedo picture: pixels and opacity, (0, 0) at the box corner (x0, y0)
// relative to the hotspot, as `SpriteFrame::decode` gives them.
struct TorpedoPicture {
  Image8 image;
  Image8 opaque;
  int x0 = 0;
  int y0 = 0;
};

// The status page's torpedo pictures of the Mark 10, 14, 18-1, 18-2 and 27.
class TorpedoArt {
 public:
  static constexpr std::uint32_t kTypes = 5;

  // Builds the pictures from STATUST.SHP and the page's palette
  // (STATUSBG.PCX); the recolourings never use the original types' ramps
  // (colours 16..63).
  TorpedoArt(const Shp& statust, const VgaPalette& palette);

  // Returns the picture of weapon `id` (1..5), still or `moving`; throws
  // `std::out_of_range` for another id.
  const TorpedoPicture& picture(std::uint32_t id, bool moving) const;

  // Draws `p` with its hotspot at (x, y) of `vp`, clipped; `mirror` (the
  // stern side) draws it as the original's mirrored frames are
  // (`sprite_draw_transformed` with a -1 x scale, through `scratch`).
  static void draw(const Viewport& vp, const TorpedoPicture& p, int x, int y, bool mirror, Image8& scratch);

 private:
  std::array<std::array<TorpedoPicture, 2>, kTypes> pictures_;
};

}  // namespace sh
