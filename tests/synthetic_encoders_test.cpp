// The synthetic install's format writers (synthetic/encoders.h) read back
// through the game's parsers.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "assets/bases.h"
#include "assets/cat.h"
#include "assets/dat_tables.h"
#include "assets/ephemeris.h"
#include "assets/mel.h"
#include "assets/patrol_zones.h"
#include "assets/routes.h"
#include "assets/compressed.h"
#include "assets/environment.h"
#include "assets/fnt.h"
#include "assets/objects.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "assets/world.h"
#include "core/byte_reader.h"
#include "game_data.h"
#include "synthetic/encoders.h"
#include "synthetic/table_encoders.h"

namespace sh::test {
namespace {

// Returns `n` bytes of a pattern with runs, the RLE90 mark and noise.
std::vector<std::uint8_t> pattern(std::size_t n, std::uint32_t seed) {
  std::vector<std::uint8_t> out;
  std::uint32_t s = seed;
  while (out.size() < n) {
    s = s * 1103515245u + 12345u;
    const std::uint8_t v = static_cast<std::uint8_t>(s >> 16);
    const std::size_t run = (s >> 8) % 7 == 0 ? (s >> 4) % 600 : 1;
    out.insert(out.end(), std::min(run, n - out.size()), (s & 0x1f) == 3 ? std::uint8_t{0x90} : v);
  }
  return out;
}

Image8 picture(int w, int h, std::uint32_t seed) {
  Image8 img(w, h);
  const std::vector<std::uint8_t> p = pattern(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), seed);
  std::ranges::copy(p, img.pixels().begin());
  return img;
}

TEST_CASE("Compressed blocks round-trip through the LZW and RLE90 readers", "[synthetic]") {
  for (const std::size_t n : {std::size_t{1}, std::size_t{7}, std::size_t{300}, std::size_t{5000}, std::size_t{70000}}) {
    const std::vector<std::uint8_t> data = pattern(n, static_cast<std::uint32_t>(n));
    CHECK(rle90_decode(rle90_encode(data)) == data);
    std::vector<std::uint8_t> block = encode_compressed(data);
    block.push_back(0xab);  // what follows the block
    ByteReader r(block, "block");
    CHECK(read_compressed(r) == data);
    CHECK(r.u8() == 0xab);
  }
}

TEST_CASE("PCX pictures round-trip", "[synthetic]") {
  const Image8 img = picture(33, 9, 5);
  VgaPalette pal{};
  for (std::size_t i = 0; i < pal.size(); ++i) {
    pal[i] = static_cast<std::uint8_t>(i % 64);
  }
  const PcxImage back = decode_pcx(encode_pcx(img, pal), "test");
  CHECK(back.image.pixels().size() == img.pixels().size());
  CHECK(std::ranges::equal(back.image.pixels(), img.pixels()));
  CHECK(back.palette == pal);
}

TEST_CASE("Sprite sets round-trip with transparency, hotspots and palettes", "[synthetic]") {
  std::vector<SpriteImage> frames(3);
  frames[0].pixels = picture(40, 12, 1);
  frames[0].transparent = frames[0].pixels.at(0, 0);
  frames[0].hotspot_x = 20;
  frames[0].hotspot_y = 6;
  frames[0].palette = {{16, 1, 2, 3}, {17, 4, 5, 6}};
  frames[1].pixels = picture(300, 2, 2);  // runs and literals past 127
  frames[2].pixels = Image8(0, 0);        // an empty frame
  const Shp shp = Shp::parse(encode_shp(frames), "test");
  REQUIRE(shp.frame_count() == 3);
  for (std::size_t i = 0; i < 2; ++i) {
    const SpriteFrame& f = shp.frame(i);
    CHECK(f.x0 == -frames[i].hotspot_x);
    CHECK(f.y0 == -frames[i].hotspot_y);
    const DecodedFrame d = f.decode();
    for (int y = 0; y < frames[i].pixels.height(); ++y) {
      for (int x = 0; x < frames[i].pixels.width(); ++x) {
        const std::uint8_t v = frames[i].pixels.at(x, y);
        const bool opaque = !frames[i].transparent || v != *frames[i].transparent;
        CHECK(d.opaque.at(x, y) == (opaque ? 1 : 0));
        if (opaque) {
          CHECK(d.image.at(x, y) == v);
        }
      }
    }
  }
  CHECK(shp.frame(0).palette.size() == 2);
  CHECK(shp.frame(0).palette[1].b == 6);
  CHECK(shp.frame(2).empty());
}

TEST_CASE("Fonts and archives round-trip", "[synthetic]") {
  std::vector<GlyphImage> glyphs(4);
  for (std::uint32_t i = 0; i < glyphs.size(); ++i) {
    glyphs[i].width = i + 1;
    glyphs[i].pixels.assign((i + 1) * 5, static_cast<std::uint8_t>(i));
  }
  const Font font = Font::parse(encode_fnt(glyphs, 5), "test");
  CHECK(font.height() == 5);
  CHECK(font.glyph_width(3) == 4);

  const std::vector<CatMember> members = {{"A.SHP", {1, 2, 3}}, {"LONGNAME.SHP", {4}}};
  const CatArchive cat = CatArchive::parse(encode_cat(members), "test");
  CHECK(std::ranges::equal(cat.member("longname.shp"), members[1].data));
  CHECK(std::ranges::equal(cat.member("A.SHP"), members[0].data));
}

TEST_CASE("Maps, map objects and environments round-trip", "[synthetic]") {
  World w;
  w.version = 7;
  w.region_name = "Test Sea";
  w.scale_shift = 2;
  w.locations = {Location{.type = LocationType::kBuoy, .x = 10, .z = 0, .y = 20, .heading = Angle10(900)}};
  w.grid = IndexedRaster{.width = 31, .height = 17, .pixels = pattern(31 * 17, 9)};
  w.grid_dac[5] = 7;
  w.chart = IndexedRaster{.width = 61, .height = 23, .pixels = pattern(61 * 23, 10)};
  w.chart_dac[3] = 9;
  w.coast_tiles_shp = {1, 2, 3};
  w.bounds = MapBounds{Fixed16::from_raw(1), Fixed16::from_raw(2), Fixed16::from_raw(3), Fixed16::from_raw(4)};
  const World back = parse_world(encode_world(w), "test");
  CHECK(back.region_name == w.region_name);
  CHECK(back.locations.size() == 1);
  CHECK(back.locations[0].heading.tenths() == 900);
  CHECK(back.grid.pixels == w.grid.pixels);
  CHECK(back.chart.width == w.chart.width);
  CHECK(back.chart.pixels == w.chart.pixels);
  CHECK(back.grid_dac == w.grid_dac);
  CHECK(back.chart_dac == w.chart_dac);
  CHECK(back.coast_tiles_shp == w.coast_tiles_shp);
  REQUIRE(back.bounds);
  CHECK(back.bounds->lon_east.raw() == 4);

  const std::vector<MapObject> objects = {MapObject{.render_type = ObjectRenderType::kOriented, .size = 3,
                                                    .shp = pattern(500, 4)}};
  const std::vector<MapObject> objects_back = parse_objects(encode_objects(objects), "test");
  REQUIRE(objects_back.size() == 1);
  CHECK(objects_back[0].shp == objects[0].shp);
  CHECK(objects_back[0].size == 3);

  Environment env;
  env.version = 9;
  env.header[0x1153] = 24;
  env.sky_pcx = pattern(1000, 3);
  env.sun[7] = 1;
  env.moon_phases.resize(4);
  env.moon_phases[2][5] = 2;
  const Environment env_back = parse_environment(encode_environment(env), "test");
  CHECK(env_back.ambient_minimum_pct() == 24);
  CHECK(env_back.sky_pcx == env.sky_pcx);
  CHECK(env_back.sun == env.sun);
  CHECK(env_back.moon_phases[2] == env.moon_phases[2]);
}

}  // namespace
}  // namespace sh::test

namespace sh::test {
namespace {

// The table writers are exact inverses: the original's files written back
// from their parsed records are byte-equal.
TEST_CASE("The table writers write the original's tables back byte for byte", "[synthetic]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto same = [&](std::string_view path, const std::vector<std::uint8_t>& written) {
    const std::vector<std::uint8_t> original = install.read(path);
    const auto [w, o] = std::ranges::mismatch(written, original);
    INFO(path << ": " << written.size() << " bytes written, " << original.size() << " original, first difference at "
              << (w - written.begin()));
    CHECK((w == written.end() && o == original.end()));
  };
  same("DATA/SHIPS.DAT", encode_ships_dat(parse_ships_dat(install.read("DATA/SHIPS.DAT"))));
  same("DATA/SUBS.DAT", encode_subs_dat(parse_subs_dat(install.read("DATA/SUBS.DAT"))));
  same("DATA/AIRCRAFT.DAT", encode_aircraft_dat(parse_aircraft_dat(install.read("DATA/AIRCRAFT.DAT"))));
  same("DATA/WEAPONS.DAT", encode_weapons_dat(parse_weapons_dat(install.read("DATA/WEAPONS.DAT"))));
  same("DATA/CARSUB.DAT", encode_carsub_dat(parse_carsub_dat(install.read("DATA/CARSUB.DAT"))));
  same("DATA/IJNSHIPS.DAT", encode_ijnships_dat(parse_ijnships_dat(install.read("DATA/IJNSHIPS.DAT"))));
  same("DATA/CMDLIST.DAT", encode_cmdlist_dat(parse_cmdlist_dat(install.read("DATA/CMDLIST.DAT"))));
  same("DATA/HOMEPORT.DAT", encode_homeport_dat(parse_homeport_dat(install.read("DATA/HOMEPORT.DAT"))));
  same("DATA/RMSG.DAT", encode_rmsg_dat(parse_rmsg_dat(install.read("DATA/RMSG.DAT"))));
  same("DATA/PZ.DAT", encode_patrol_zones(parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT")));
  for (const std::string& path : install.list("DATA", ".CR")) {
    same(path, encode_routes(parse_routes(install.read(path), path)));
  }
  for (int map = 1; map <= 15; ++map) {
    const std::string path = "DATA/" + base_file_name(map);
    same(path, encode_bases(parse_bases(install.read(path), path)));
  }
  for (const std::string& path : install.list("SFX", ".MEL")) {
    same(path, encode_mel(parse_mel(install.read(path), path)));
  }
  // The bank keeps a stale word the parser does not read: same records.
  const std::vector<BankEntry> bank = parse_bnk(install.read("SFX/1.BNK"));
  const std::vector<BankEntry> bank_back = parse_bnk(encode_bnk(bank));
  REQUIRE(bank_back.size() == bank.size());
  for (std::size_t i = 0; i < bank.size(); ++i) {
    CHECK(bank_back[i].sample_id == bank[i].sample_id);
    CHECK(bank_back[i].header.rate == bank[i].header.rate);
  }
  const Ephemeris eph = Ephemeris::load(install);
  std::array<const PlanetTheory*, kPlanetBodies> bodies{};
  bodies[static_cast<std::size_t>(PlanetBody::kSun)] = &eph.sun;
  bodies[static_cast<std::size_t>(PlanetBody::kEarth)] = &eph.earth;
  same("SUN.DAT", encode_planet_theory(eph.sun));
  same("EARTH.DAT", encode_planet_theory(eph.earth));
  // The index of the other bodies' theories is not reproduced, only theirs.
  const auto ndx = parse_planets_ndx(encode_planets_ndx(bodies));
  const auto original = parse_planets_ndx(install.read("PLANETS.NDX"));
  for (const PlanetBody b : {PlanetBody::kSun, PlanetBody::kEarth}) {
    for (std::size_t c = 0; c < kEphemerisCoords; ++c) {
      for (std::size_t p = 0; p < kEphemerisPowers; ++p) {
        CHECK(ndx[static_cast<std::size_t>(b)][c][p].count == original[static_cast<std::size_t>(b)][c][p].count);
      }
    }
  }
}

// The art and map writers pack differently, so the original's files read
// back the same rather than byte for byte.
TEST_CASE("The art and map writers read back the original's files the same", "[synthetic]") {
  SH_REQUIRE_GAME_DATA(install);
  for (const std::string path : {"DATA/WORLD-1.WLD", "DATA/WORLD-14.WLD"}) {
    INFO(path);
    const World w = parse_world(install.read(path), path);
    const World back = parse_world(encode_world(w), path);
    CHECK(back.grid.pixels == w.grid.pixels);
    CHECK(back.chart.pixels == w.chart.pixels);
    CHECK(back.locations.size() == w.locations.size());
    CHECK(back.coast_tiles_shp == w.coast_tiles_shp);
  }
  const std::vector<MapObject> objects = parse_objects(install.read("DATA/WORLD-1.OBS"), "OBS");
  const std::vector<MapObject> objects_back = parse_objects(encode_objects(objects), "OBS");
  REQUIRE(objects_back.size() == objects.size());
  for (std::size_t i = 0; i < objects.size(); ++i) {
    CHECK(objects_back[i].shp == objects[i].shp);
  }
  const Environment env = parse_environment(install.read("DATA/ENV-01.ENV"), "ENV");
  CHECK(encode_environment(env) == install.read("DATA/ENV-01.ENV"));
  const PcxImage pcx = load_pcx(install, "ART/CONN.PCX");
  const PcxImage pcx_back = decode_pcx(encode_pcx(pcx.image, pcx.palette), "CONN");
  CHECK(std::ranges::equal(pcx_back.image.pixels(), pcx.image.pixels()));
  CHECK(pcx_back.palette == pcx.palette);
}

}  // namespace
}  // namespace sh::test
