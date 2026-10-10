// The palettes the asset viewer shows each file with. The sprite and font
// files carry no complete palette: their pixels index the palette of the
// screen that draws them, as traced from the code that draws each set
// (docs/formats.md, ".SHP" palettes).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

#include "assets/environment.h"
#include "assets/pal.h"
#include "game/shpath.h"

namespace sh {

class Install;

// How a palette is put together.
enum class PaletteKind : std::uint8_t {
  kScreen,  // a screen's PCX palette
  kWorld,   // the 3D view: a station PCX shaded with the `.ENV` world colours
  kChart,   // the chart screen (`chart_screen` + `chart_view_init`)
};

// A palette recipe.
struct PaletteRecipe {
  PaletteKind kind = PaletteKind::kScreen;
  std::string pcx;     // ART file of the screen or station (unused for kChart)
  bool traced = true;  // false: a default for sets SH.EXE never draws
};

// Returns the recipe of the sprite set `set_name` (a `.SHP` or `.CAT` stem,
// or "OBS" for the map objects), case-insensitive.
PaletteRecipe sprite_palette_recipe(std::string_view set_name);

// Returns the ART PCX whose palette the font `font_name` (a `.FNT` stem) is
// drawn with.
std::string_view font_palette_pcx(std::string_view font_name);

// Builds palettes from the install, caching the files it reads.
class PaletteBuilder {
 public:
  // The `.ENV` file of the 3D view recipe (`init_select_env_file` picks one
  // by time and weather; ENV-01 is the canonical choice).
  static constexpr std::string_view kDefaultEnv = "ENV-01.ENV";
  static constexpr std::string_view kDefaultWorld = "WORLD-1.WLD";

  // `install` and `paths` must outlive the builder.
  PaletteBuilder(const Install& install, const ShPaths& paths);

  // Returns the palette of `recipe`; `world_file` (a DATA file name) gives
  // the chart colours of kChart.
  VgaPalette build(const PaletteRecipe& recipe, std::string_view world_file = kDefaultWorld);
  // Returns a description of `recipe` for the status line.
  static std::string describe(const PaletteRecipe& recipe, std::string_view world_file = kDefaultWorld);

  // Returns the palette of the ART PCX `pcx` (`load_pcx` into the screen
  // palette).
  VgaPalette screen(std::string_view pcx);
  // Returns the 3D view palette on station `station_pcx` at full daylight
  // (`world_shade_screen_palette`): the station PCX with 16-63 and 80-129
  // from the `env_file` header and 130-159 from its `state` sea colours.
  VgaPalette world(std::string_view station_pcx, std::string_view env_file = kDefaultEnv,
                   SeaState state = SeaState::kCalm);
  // Returns the chart palette with `world_file`'s chart colours: CONN.PCX
  // 0-15 (and 80, 255), SHIPICON frame 0 16-63, CHART frame 0x29 64-127 and
  // 240-254, the map's chart colours 128-239.
  VgaPalette chart(std::string_view world_file);

  // Returns the parsed `.ENV` file `env_file` of DATA.
  const Environment& environment(std::string_view env_file);

 private:
  const Install& install_;
  const ShPaths& paths_;
  std::map<std::string, VgaPalette, std::less<>> screens_;
  std::map<std::string, Environment, std::less<>> environments_;
  std::map<std::string, VgaPalette, std::less<>> charts_;
};

}  // namespace sh
