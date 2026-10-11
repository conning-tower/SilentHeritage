// The torpedo types told apart by the extended variant (not in the
// original; `Features::torpedo_types`): the status page's torpedo pictures
// (STATUST.SHP frames 0xc + 2 x (id - 1), + 1 while moving), where the
// Mark 18-1 is recoloured steel blue with one white band and the 18-2
// orange with two (the original draws both alike), and the Mark 27 is the
// 18-1's shortened, dark, with green nose bands; and the type marking of
// the tube buttons: their bezel in the type's colour with one notch for
// the 18-1, two for the 18-2. Everything is built from the game's art in
// the nearest colours of the palette it is drawn with.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

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

// Returns the 8-bit marking colour of weapon `id` (its picture's body
// colour; a neutral grey for an unknown id).
Rgb torpedo_type_colour(std::uint32_t id);

// The tube button marking of weapon `id` at pixel (x, y) of a round `w` x
// `h` button: whether the pixel is on the bezel (the ring outside the
// lamp), and whether it is on a notch (white ticks at the bezel's top: one
// for the Mark 18-1, two for the 18-2).
bool on_button_bezel(int x, int y, int w, int h);
bool on_button_notch(std::uint32_t id, int x, int y, int w);

// Returns the short type label of the knob and the touch panel ("MK14",
// "MK18-2", "MK27": the WEAPONS.DAT short name with "MARK" shortened).
std::string torpedo_short_label(const std::string& name);

// Draws tube button `frame` with its hotspot at (x, y) of `vp`, its bezel
// recoloured by weapon `id`'s colour (darker when `dim`: a tube loading) in
// the nearest of the `allowed` entries of `palette`, with its notches.
void draw_marked_button(const Viewport& vp, const SpriteFrame& frame, int x, int y, std::uint32_t id, bool dim,
                        const VgaPalette& palette, std::span<const std::uint8_t> allowed);

// Writes the type colours (a dark and a light shade of the Mark 18-1,
// 18-2 and 27) into the TBT overlay palette's unused entries (TBT.PCX 223
// and 238..242, placeholders), which the TBT's markings may then use; an
// entry in use is left alone.
void add_tbt_type_colours(VgaPalette& palette);

}  // namespace sh
