// The NEW EQUIPMENT letter's picture of the extended ST periscope radar
// (not in the original), composed from EQUIP.SHP: the SD's frame and mast
// with a periscope head in place of its antenna, and its label's "D"
// replaced by the "T" of the Mark 18-1's "Torpedo": "ST Radar".
#pragma once

#include "assets/shp.h"
#include "core/image.h"

namespace sh {

// Returns the ST's picture (the SD picture's size, transparent pixels
// black) composed from `equip` (EQUIP.SHP). A set whose frames do not have
// the original's sizes gives the SD's picture unchanged.
Image8 st_radar_picture(const Shp& equip);

}  // namespace sh
