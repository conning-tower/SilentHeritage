#include "app/palettes.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <span>

#include "assets/pcx.h"
#include "assets/shp.h"
#include "assets/world.h"
#include "core/install.h"
#include "gfx/sprite.h"

namespace sh {

namespace {

struct NamedRecipe {
  std::string_view name;
  PaletteKind kind;
  std::string_view pcx;
};

// The screen each sprite set is drawn on (docs/formats.md, ".SHP").
constexpr std::array kSpriteRecipes = {
    NamedRecipe{"3DAIR", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"3DSHIPS", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"AWARDS", PaletteKind::kScreen, "WOOD.PCX"},
    NamedRecipe{"BOMB", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"BRDGNAV", PaletteKind::kScreen, "BRIDGE.PCX"},
    NamedRecipe{"BRIDGE", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"CHART", PaletteKind::kChart, ""},
    NamedRecipe{"CONN", PaletteKind::kScreen, "CONN.PCX"},
    NamedRecipe{"CREDITS", PaletteKind::kScreen, "SHCREDIT.PCX"},
    NamedRecipe{"DC", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"DECKGUN", PaletteKind::kWorld, "DECKGUN.PCX"},
    NamedRecipe{"EQUIP", PaletteKind::kScreen, "M8.PCX"},
    NamedRecipe{"EXPL-0", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"EXPL-1", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"FIRE-0", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"GAUGES", PaletteKind::kScreen, "GAUGES.PCX"},
    NamedRecipe{"GUNEXPL", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"GUNFIRE", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"HFLAGS", PaletteKind::kScreen, "M8.PCX"},
    NamedRecipe{"IDBOOK", PaletteKind::kScreen, "ID-1.PCX"},
    NamedRecipe{"MAPICON", PaletteKind::kChart, ""},
    NamedRecipe{"MENUPIC3", PaletteKind::kScreen, "M8.PCX"},
    NamedRecipe{"MENUPICS", PaletteKind::kScreen, "M8.PCX"},
    NamedRecipe{"OBS", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"P_BARACU", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_GATO", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_NARWHA", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_PCLASS", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_SALMON", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_SCLASS", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_TCLASS", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"P_TENCH", PaletteKind::kScreen, "PORTBACK.PCX"},
    NamedRecipe{"RADAR", PaletteKind::kScreen, "RADAR.PCX"},
    NamedRecipe{"RADAR1", PaletteKind::kScreen, "RADAR.PCX"},
    NamedRecipe{"RAFT", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"SCOPE", PaletteKind::kScreen, "SCOPE.PCX"},
    NamedRecipe{"SCOPENAV", PaletteKind::kScreen, "SCOPE.PCX"},
    NamedRecipe{"SCOPETDC", PaletteKind::kScreen, "SCOPE.PCX"},
    NamedRecipe{"SCOPTDC0", PaletteKind::kScreen, "SCOPE.PCX"},
    NamedRecipe{"SCOPTDC1", PaletteKind::kScreen, "SCOPE.PCX"},
    NamedRecipe{"SCOPTDC2", PaletteKind::kScreen, "SCOPE.PCX"},
    NamedRecipe{"SH-LOGO1", PaletteKind::kScreen, "M3.PCX"},
    NamedRecipe{"SHELL", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"SHIPICON", PaletteKind::kChart, ""},
    NamedRecipe{"SMOKE", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"SPOUT", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"STATUS", PaletteKind::kScreen, "STATUSBG.PCX"},
    NamedRecipe{"STATUST", PaletteKind::kScreen, "STATUSBG.PCX"},
    NamedRecipe{"SUBPICS", PaletteKind::kScreen, "M8.PCX"},
    NamedRecipe{"SUBTOUR", PaletteKind::kScreen, "B-GRND-1.PCX"},
    NamedRecipe{"SUBTOUR2", PaletteKind::kScreen, "B-GRND-1.PCX"},
    NamedRecipe{"TBT", PaletteKind::kWorld, "TBT.PCX"},
    NamedRecipe{"TBTID", PaletteKind::kScreen, "TBT.PCX"},
    NamedRecipe{"TBTNAV", PaletteKind::kScreen, "TBT.PCX"},
    NamedRecipe{"TBTTDC", PaletteKind::kScreen, "TDC.PCX"},
    NamedRecipe{"TBTTDC0", PaletteKind::kScreen, "TBT.PCX"},
    NamedRecipe{"TBTTDC1", PaletteKind::kScreen, "TBT.PCX"},
    NamedRecipe{"TBTTDC2", PaletteKind::kScreen, "TBT.PCX"},
    NamedRecipe{"WAKE", PaletteKind::kWorld, "BRIDGE.PCX"},
    NamedRecipe{"WAKE_F", PaletteKind::kWorld, "BRIDGE.PCX"},
};

// The default for sets SH.EXE never draws (scenario editor or unused).
constexpr std::string_view kDefaultScreen = "MENUBGND.PCX";

struct FontPalette {
  std::string_view font;
  std::string_view pcx;
};

// The screen each font is loaded
// for; the GUI and menu fonts use the menu palette.
constexpr std::array kFontPalettes = {
    FontPalette{"BEARING", "GAUGES.PCX"},   FontPalette{"CALEND1", "CALENDAR.PCX"},
    FontPalette{"CALEND2", "CALENDAR.PCX"}, FontPalette{"CALEND3", "CALENDAR.PCX"},
    FontPalette{"CHALK", "BBOARD.PCX"},     FontPalette{"DIGITS", "GAUGES.PCX"},
    FontPalette{"DIGITS2", "BRIDGE.PCX"},   FontPalette{"DIGITS3", "GAUGES.PCX"},
    FontPalette{"FONT1", "BRIDGE.PCX"},     FontPalette{"ID_1", "ID-1.PCX"},
    FontPalette{"ID_2", "ID-1.PCX"},        FontPalette{"ID_3", "ID-1.PCX"},
    FontPalette{"MFONT", "PAPER-2.PCX"},    FontPalette{"MFONT2", "PAPER-2.PCX"},
    FontPalette{"MSG", "BRIDGE.PCX"},       FontPalette{"SCRIPT", "LOG-1.PCX"},
    FontPalette{"SUBTOUR", "B-GRND-1.PCX"}, FontPalette{"TYPEX", "NAVYCIT.PCX"},
};

std::string upper(std::string_view s) {
  std::string out(s);
  std::ranges::transform(out, out.begin(),
                         [](char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); });
  return out;
}

// Copies the 6-bit colours of entries first..last of `from` into `to`.
void copy_range(const VgaPalette& from, VgaPalette& to, int first, int last) {
  std::copy(from.begin() + 3 * first, from.begin() + 3 * (last + 1), to.begin() + 3 * first);
}

}  // namespace

PaletteRecipe sprite_palette_recipe(std::string_view set_name) {
  const std::string name = upper(set_name);
  for (const NamedRecipe& r : kSpriteRecipes) {
    if (r.name == name) {
      return {r.kind, std::string(r.pcx), true};
    }
  }
  // SUB-1..8, SPAC_A..S: the status screen and the home port.
  if (name.starts_with("SUB-")) {
    return {PaletteKind::kScreen, "STATUSBG.PCX", true};
  }
  if (name.starts_with("SPAC_")) {
    return {PaletteKind::kScreen, "M8.PCX", true};
  }
  return {PaletteKind::kScreen, std::string(kDefaultScreen), false};
}

std::string_view font_palette_pcx(std::string_view font_name) {
  const std::string name = upper(font_name);
  for (const FontPalette& f : kFontPalettes) {
    if (f.font == name) {
      return f.pcx;
    }
  }
  return kDefaultScreen;
}

PaletteBuilder::PaletteBuilder(const Install& install, const ShPaths& paths) : install_(install), paths_(paths) {}

VgaPalette PaletteBuilder::build(const PaletteRecipe& recipe, std::string_view world_file) {
  switch (recipe.kind) {
    case PaletteKind::kScreen:
      return screen(recipe.pcx);
    case PaletteKind::kWorld:
      return world(recipe.pcx);
    case PaletteKind::kChart:
      return chart(world_file);
  }
  return screen(kDefaultScreen);
}

std::string PaletteBuilder::describe(const PaletteRecipe& recipe, std::string_view world_file) {
  switch (recipe.kind) {
    case PaletteKind::kScreen:
      return recipe.traced ? recipe.pcx : recipe.pcx + " (default)";
    case PaletteKind::kWorld:
      return std::format("{} + {}", recipe.pcx, kDefaultEnv);
    case PaletteKind::kChart:
      return std::format("chart ({})", world_file);
  }
  return {};
}

VgaPalette PaletteBuilder::screen(std::string_view pcx) {
  if (const auto it = screens_.find(pcx); it != screens_.end()) {
    return it->second;
  }
  const std::string path = paths_.path(GameDir::kArt, pcx);
  const VgaPalette pal = pcx_palette(install_.read(path), path);
  screens_.emplace(std::string(pcx), pal);
  return pal;
}

const Environment& PaletteBuilder::environment(std::string_view env_file) {
  if (const auto it = environments_.find(env_file); it != environments_.end()) {
    return it->second;
  }
  const std::string path = paths_.path(GameDir::kData, env_file);
  return environments_.emplace(std::string(env_file), parse_environment(install_.read(path), path)).first->second;
}

VgaPalette PaletteBuilder::world(std::string_view station_pcx, std::string_view env_file, SeaState state) {
  VgaPalette pal = screen(station_pcx);
  environment(env_file).apply_world_palette(pal, state);
  return pal;
}

VgaPalette PaletteBuilder::chart(std::string_view world_file) {
  if (const auto it = charts_.find(world_file); it != charts_.end()) {
    return it->second;
  }
  // chart_screen (0x20979) then chart_view_init (0x2a5c8); 80 and 255 keep
  // the previous station's colours (CONN.PCX).
  const VgaPalette conn = screen("CONN.PCX");
  VgaPalette pal = conn;
  sprite_apply_palette(Shp::load(install_, paths_.path(GameDir::kArt, "CHART.SHP")).frame(0x29), pal);
  copy_range(conn, pal, 0, 15);
  VgaPalette icons = pal;
  sprite_apply_palette(Shp::load(install_, paths_.path(GameDir::kArt, "SHIPICON.SHP")).frame(0), icons);
  copy_range(icons, pal, 16, 63);
  const std::string path = paths_.path(GameDir::kData, world_file);
  const World w = parse_world(install_.read(path), path);
  std::ranges::copy(w.chart_dac, pal.begin() + 3 * kChartDacFirstIndex);
  charts_.emplace(std::string(world_file), pal);
  return pal;
}

}  // namespace sh
