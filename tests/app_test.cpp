#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "app/command_line.h"
#include "app/palettes.h"
#include "app/ui_text.h"
#include "game/shpath.h"
#include "game_data.h"
#include "platform/screenshot.h"

namespace sh {
namespace {

std::array<std::uint8_t, 3> entry(const VgaPalette& pal, int index) {
  const auto i = static_cast<std::size_t>(3 * index);
  return {pal[i], pal[i + 1], pal[i + 2]};
}

std::uint32_t le32(std::span<const std::uint8_t> b, std::size_t at) {
  return b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (std::uint32_t{b[at + 3]} << 24);
}

TEST_CASE("SHPATH.INI is read like load_shpath_ini", "[app]") {
  SECTION("defaults") {
    const ShPaths p;
    CHECK(p.dir(GameDir::kData) == "DATA\\");
    CHECK(p.dir(GameDir::kCustom) == "CUSTOM\\");
    CHECK(p.dir_name(GameDir::kSfx) == "SFX");
    CHECK(p.path(GameDir::kArt, "M8.PCX") == "ART\\M8.PCX");
  }
  SECTION("keys are case-insensitive and paths get their backslash") {
    const ShPaths p = ShPaths::parse("data = D:\\SH\\DATA\r\nSpeech =VOICES\\\r\nFOO = BAR\r\n");
    CHECK(p.dir(GameDir::kData) == "D:\\SH\\DATA\\");
    CHECK(p.dir(GameDir::kSpeech) == "VOICES\\");
    CHECK(p.dir(GameDir::kSfx) == "SFX\\");
  }
  SECTION("'%s' takes a '=' glued to the key: the line is not an assignment") {
    const ShPaths p = ShPaths::parse("SFX= SOUNDS\n");
    CHECK(p.dir(GameDir::kSfx) == "SFX\\");
  }
  SECTION("a key without '= path' takes the previous path (fscanf partial match)") {
    const ShPaths p = ShPaths::parse("ART = PICS\nCINES MOVIES\n");
    CHECK(p.dir(GameDir::kArt) == "PICS\\");
    CHECK(p.dir(GameDir::kCines) == "PICS\\");
    // "MOVIES" is then read as a key, which is unknown.
    CHECK(p.dir(GameDir::kData) == "DATA\\");
  }
  SECTION("the shipped file") {
    SH_REQUIRE_INSTALL(install);
    const ShPaths p = ShPaths::load(install);
    CHECK(p.dir(GameDir::kBud) == "BUD\\");
    CHECK(p.dir(GameDir::kSave) == "SAVE\\");
    CHECK(p.dir(GameDir::kAlt) == "ALT\\");  // not in the file: default
  }
}

TEST_CASE("The command line starts the game, the viewer or the editor", "[app]") {
  using Args = std::vector<std::string_view>;
  const CommandLine game = parse_command_line(Args{"--data", "source", "--nointro"});
  CHECK(game.data_dir == "source");
  CHECK_FALSE(game.intro);
  CHECK_FALSE(game.editor);
  CHECK_FALSE(game.viewer);
  CHECK_FALSE(game.faithful);
  CHECK(parse_command_line(Args{"--faithful"}).faithful);
  CHECK(parse_command_line(Args{"--no-orders-button"}).no_orders_button);
  CHECK(!parse_command_line(Args{"--faithful"}).no_orders_button);

  const CommandLine editor = parse_command_line(Args{"--editor", "--data", "source", "--save", "/tmp/sh"});
  CHECK(editor.editor);
  CHECK(editor.intro);
  CHECK(editor.save_dir == "/tmp/sh");

  // A right click for the editor's menu bar, then a left one.
  const CommandLine keys = parse_command_line(Args{"--editor", "--keys", "rclick:10:20,click:70:13,-2"});
  REQUIRE(keys.keys.size() == 6);
  CHECK(keys.keys[0].type == EventType::kMouseDown);
  CHECK(keys.keys[0].button == MouseButton::kRight);
  CHECK(keys.keys[1].type == EventType::kMouseUp);
  CHECK(keys.keys[2].button == MouseButton::kLeft);
  CHECK(keys.keys[2].x == 70);
  CHECK(keys.keys[5].type == EventType::kKeyUp);

  CHECK_THROWS_AS(parse_command_line(Args{"--editor", "--viewer"}), std::runtime_error);
  CHECK_THROWS_AS(parse_command_line(Args{"--keys", "rclick:10"}), std::runtime_error);
  CHECK_THROWS_AS(parse_command_line(Args{"--data"}), std::runtime_error);
}

TEST_CASE("Screenshots are 8-bit BMP files", "[app]") {
  Image8 image(3, 2);
  image.set(0, 0, 1);  // top-left
  image.set(2, 1, 2);  // bottom-right
  Palette palette;
  palette.colors[1] = {10, 20, 30};
  const auto bmp = encode_bmp(image, palette);
  REQUIRE(bmp.size() == 14 + 40 + 1024 + 2 * 4);
  CHECK(bmp[0] == 'B');
  CHECK(bmp[1] == 'M');
  CHECK(le32(bmp, 2) == bmp.size());
  CHECK(le32(bmp, 10) == 14 + 40 + 1024);
  CHECK(le32(bmp, 18) == 3);  // width
  CHECK(le32(bmp, 22) == 2);  // height, bottom-up
  CHECK(bmp[28] == 8);        // bits per pixel
  // Palette entry 1 as BGR0.
  CHECK(bmp[54 + 4] == 30);
  CHECK(bmp[54 + 5] == 20);
  CHECK(bmp[54 + 6] == 10);
  // The bottom row comes first, each padded to 4 bytes.
  const std::size_t data = 14 + 40 + 1024;
  CHECK(bmp[data + 2] == 2);
  CHECK(bmp[data + 4] == 1);
}

TEST_CASE("UI colours skip the transparent glyph colour", "[app]") {
  Palette palette;
  palette.colors[255] = {255, 255, 255};
  palette.colors[7] = {250, 250, 250};
  CHECK(nearest_colour(palette, {255, 255, 255}) == 7);
}

TEST_CASE("Sprite palette recipes follow the traced screens", "[app]") {
  CHECK(sprite_palette_recipe("p_gato").pcx == "PORTBACK.PCX");
  CHECK(sprite_palette_recipe("BRIDGE").kind == PaletteKind::kWorld);
  CHECK(sprite_palette_recipe("SHIPICON").kind == PaletteKind::kChart);
  CHECK(sprite_palette_recipe("SUB-3").pcx == "STATUSBG.PCX");
  CHECK(sprite_palette_recipe("SPAC_K").pcx == "M8.PCX");
  const PaletteRecipe unused = sprite_palette_recipe("SBCHART");
  CHECK(unused.pcx == "MENUBGND.PCX");
  CHECK_FALSE(unused.traced);
  CHECK(font_palette_pcx("chalk") == "BBOARD.PCX");
  CHECK(font_palette_pcx("GUI_S") == "MENUBGND.PCX");
}

TEST_CASE("Palette builder reproduces the 3D view and chart palettes", "[app][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const ShPaths paths = ShPaths::load(install);
  PaletteBuilder builder(install, paths);

  // world_shade_screen_palette: station PCX outside 16-63/80-159.
  const VgaPalette calm = builder.world("BRIDGE.PCX");
  CHECK(entry(calm, 255) == std::array<std::uint8_t, 3>{63, 63, 63});  // BRIDGE.PCX
  CHECK(entry(calm, 70) == std::array<std::uint8_t, 3>{63, 24, 0});    // BRIDGE.PCX
  CHECK(entry(calm, 20) == std::array<std::uint8_t, 3>{17, 20, 16});   // ENV header
  CHECK(entry(calm, 135) == std::array<std::uint8_t, 3>{16, 20, 13});  // calm sea
  const VgaPalette heavy = builder.world("BRIDGE.PCX", PaletteBuilder::kDefaultEnv, SeaState::kHeavy);
  CHECK(entry(heavy, 135) == std::array<std::uint8_t, 3>{37, 38, 38});

  // chart_screen + chart_view_init.
  const VgaPalette chart = builder.chart("WORLD-1.WLD");
  CHECK(entry(chart, 5) == std::array<std::uint8_t, 3>{47, 47, 47});    // CONN.PCX beats CHART frame 0x29
  CHECK(entry(chart, 80) == std::array<std::uint8_t, 3>{0, 63, 0});     // left over from CONN.PCX
  CHECK(entry(chart, 20) == std::array<std::uint8_t, 3>{56, 52, 43});   // SHIPICON frame 0
  CHECK(entry(chart, 100) == std::array<std::uint8_t, 3>{34, 28, 13});  // CHART frame 0x29
  CHECK(entry(chart, 130) == std::array<std::uint8_t, 3>{40, 52, 63});  // the map's chart colours
}

}  // namespace
}  // namespace sh
