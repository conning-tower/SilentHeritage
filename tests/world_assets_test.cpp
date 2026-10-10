// World and scenario formats: compressed blocks, .WLD, .OBS, .ENV, .SCN, .CR,
// PZ.DAT and MAP<n>BASE.DAT. Expected values were cross-checked with
// independent reference parsers.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "assets/bases.h"
#include "assets/compressed.h"
#include "assets/environment.h"
#include "assets/objects.h"
#include "assets/patrol_zones.h"
#include "assets/routes.h"
#include "assets/scenario.h"
#include "assets/world.h"
#include "core/byte_reader.h"
#include "core/error.h"
#include "game_data.h"

namespace sh {

namespace {

// Packs LZW `codes` the way the game's reader consumes them: a `max_bits`
// byte, then the codes LSB-first with the widths the decoder expects (9 bits,
// growing with the dictionary, back to 9 on a reset), padded to whole words.
std::vector<std::uint8_t> pack_lzw(std::uint8_t max_bits, const std::vector<std::uint32_t>& codes) {
  std::vector<std::uint8_t> out{max_bits};
  std::uint64_t acc = 0;
  int pending = 0;
  std::uint32_t bits = 9, mask = 0x1ff, next = 0x100;
  for (const std::uint32_t code : codes) {
    acc |= static_cast<std::uint64_t>(code) << pending;
    pending += static_cast<int>(bits);
    while (pending >= 8) {
      out.push_back(static_cast<std::uint8_t>(acc & 0xff));
      acc >>= 8;
      pending -= 8;
    }
    if (++next > mask) {
      ++bits;
      mask = mask << 1 | 1;
      if (bits > max_bits) {
        bits = 9;
        mask = 0x1ff;
        next = 0x100;
      }
    }
  }
  if (pending > 0) {
    out.push_back(static_cast<std::uint8_t>(acc & 0xff));
  }
  if (out.size() % 2 != 0) {  // the max_bits byte, the first code byte, then words
    out.push_back(0);
  }
  return out;
}

std::uint64_t sum(const std::vector<std::uint8_t>& v) {
  return std::accumulate(v.begin(), v.end(), std::uint64_t{0});
}

}  // namespace

TEST_CASE("LZW decodes dictionary codes across a reset, with RLE90 runs and escapes", "[lzw]") {
  // With max_bits 9 the 256th code fills the dictionary and resets it; the
  // first entry after the reset still chains to the last code before it.
  std::vector<std::uint32_t> codes{'A', 'B', 257, 259};  // 259: the KwKwK case
  codes.insert(codes.end(), 252, 'C');
  for (const std::uint32_t c : {std::uint32_t{'D'}, 256u, std::uint32_t{'X'}, 0x90u, 5u, 0x90u, 0u, std::uint32_t{'E'}}) {
    codes.push_back(c);
  }
  const auto stream = pack_lzw(9, codes);
  REQUIRE(stream.size() == 298);

  std::string expected = "ABABABA" + std::string(252, 'C') + "DCD" + "XXXXX" + "\x90" + "E";
  auto padded = stream;
  padded.insert(padded.end(), {0xaa, 0xbb, 0xcc, 0xdd});
  const DecodedStream full = lzw_rle90_decode(padded, expected.size());
  CHECK(std::string(full.bytes.begin(), full.bytes.end()) == expected);
  CHECK(full.consumed == 298);

  // Decoding stops after `size` bytes, having read only the words it needed.
  const DecodedStream head = lzw_rle90_decode(stream, 5);
  CHECK(std::string(head.bytes.begin(), head.bytes.end()) == "ABABA");
  CHECK(head.consumed == 6);

  CHECK_THROWS_AS(lzw_rle90_decode(stream, expected.size() + 100), DataError);
}

TEST_CASE("LZW widens its codes up to max_bits", "[lzw]") {
  const auto stream = pack_lzw(11, std::vector<std::uint32_t>(300, 'A'));
  CHECK(stream.size() == 344);  // 256 codes of 9 bits, 44 of 10
  const DecodedStream d = lzw_rle90_decode(stream, 300);
  CHECK(d.bytes == std::vector<std::uint8_t>(300, 'A'));
  CHECK(d.consumed == 344);
}

TEST_CASE("read_compressed reads a block and leaves the reader after it", "[lzw]") {
  std::vector<std::uint8_t> file{0, 0, 0, 0};  // an empty block
  file.insert(file.end(), {3, 0, 0, 0});
  const auto stream = pack_lzw(11, {'x', 'y', 'z'});
  file.insert(file.end(), stream.begin(), stream.end());
  file.push_back(0x5a);
  ByteReader r(file, "test");
  CHECK(read_compressed(r).empty());
  CHECK(read_compressed(r) == std::vector<std::uint8_t>{'x', 'y', 'z'});
  CHECK(r.u8() == 0x5a);
}

TEST_CASE("RLE90 expands runs and escaped marks", "[lzw]") {
  const std::vector<std::uint8_t> packed{'a', 0x90, 4, 0x90, 0, 'b'};
  CHECK(rle90_decode(packed) == std::vector<std::uint8_t>{'a', 'a', 'a', 'a', 0x90, 'b'});
  CHECK_THROWS_AS(rle90_decode(std::vector<std::uint8_t>{0x90, 3}), DataError);
  CHECK_THROWS_AS(rle90_decode(std::vector<std::uint8_t>{'a', 0x90}), DataError);
}

TEST_CASE("Every .WLD map parses with its region, grid, chart and bounds", "[wld]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto files = install.list("DATA", ".WLD");
  REQUIRE(files.size() == 15);
  for (const auto& file : files) {
    INFO(file);
    const World w = parse_world(install.read(file), file);
    CHECK(w.version == 7);
    CHECK(w.scale_shift == 0);
    CHECK(w.chart.width == w.grid.width * 2 - 1);
    CHECK(w.chart.height == (w.grid.height - 1) * 2);
    CHECK(w.bounds.has_value());
    CHECK_FALSE(w.coast_tiles_shp.empty());
  }

  const World w = parse_world(install.read("DATA/WORLD-1.WLD"), "WORLD-1.WLD");
  CHECK(w.region_name == "Southern Japan");
  CHECK(w.grid.width == 601);
  CHECK(w.grid.height == 401);
  CHECK(w.chart.width == 1201);
  CHECK(w.chart.height == 800);
  REQUIRE(w.locations.size() == 197);
  CHECK(w.locations[0].type == LocationType::kCity);
  CHECK(w.locations[0].x == 18414846);
  CHECK(w.locations[0].y == 18182328);
  CHECK(w.locations.back().x == 28740614);
  CHECK(w.locations.back().y == 24182787);
  int moored = 0;
  for (const Location& loc : w.locations) {
    if (loc.type == LocationType::kMooredSubmarine) {
      ++moored;
      CHECK(loc.heading.tenths() == 1800);
    }
  }
  CHECK(moored == 2);
  CHECK(sum(w.grid.pixels) == 4236411);
  CHECK(sum(w.chart.pixels) == 156422617);
  CHECK(w.grid.at(300, 200) == 38);
  CHECK(w.chart.at(600, 400) == 188);
  CHECK(w.coast_tiles_shp.size() == 85960);
  REQUIRE(w.bounds.has_value());
  CHECK(w.bounds->lat_north.raw() == 2348373);  // 35.83
  CHECK(w.bounds->lat_south.raw() == 1987925);  // 30.33
  CHECK(w.bounds->lon_west.raw() == 128 << 16);
  CHECK(w.bounds->lon_east.raw() == 9033045);   // 137.83

  const World w8 = parse_world(install.read("DATA/WORLD-8.WLD"), "WORLD-8.WLD");
  CHECK(w8.region_name == "Caroline Islands");
  CHECK(w8.locations.size() == 27);
  CHECK(w8.locations[0].type == LocationType::kTown);
}

TEST_CASE("World coordinates map to grid cells and lat/lon like the original", "[wld]") {
  SH_REQUIRE_GAME_DATA(install);
  const World w = parse_world(install.read("DATA/WORLD-8.WLD"), "WORLD-8.WLD");
  CHECK(w.cell_size() == 0x10000);
  // y counts north from the bottom row; an exact multiple of a cell falls in
  // the row above (truncating division of `grid_h * cell - y`).
  CHECK(w.cell_at(0x8000, 0x8000) == GridCell{0, 400});
  CHECK(w.cell_at(0x10000, 0x10000) == GridCell{1, 400});
  CHECK(w.cell_at(0x18000, 0x18000) == GridCell{1, 399});
  CHECK(w.cell_at(600 * 0x10000 + 1, 401 * 0x10000 - 1) == GridCell{600, 0});
  CHECK_FALSE(w.cell_at(0, 0).has_value());  // row 401
  CHECK_FALSE(w.cell_at(601 * 0x10000, 0x8000).has_value());

  // The HIST01 start position (Skate, Caroline Islands).
  const GeoAngle lon = w.longitude_at(6291456);
  CHECK(lon.degrees.raw() == 9613400);
  CHECK(lon.whole_degrees == 146);
  CHECK(lon.minutes == 41);
  CHECK(lon.hemisphere == 'E');
  const GeoAngle lat = w.latitude_at(19562496);
  CHECK(lat.degrees.raw() == 571602);
  CHECK(lat.whole_degrees == 8);
  CHECK(lat.minutes == 43);
  CHECK(lat.hemisphere == 'N');
  CHECK(w.latitude_at(0).degrees == w.bounds->lat_south);
  CHECK(w.latitude_at(401 * 0x10000).degrees == w.bounds->lat_north);

  const World w6 = parse_world(install.read("DATA/WORLD-6.WLD"), "WORLD-6.WLD");
  CHECK(w6.latitude_at(0).hemisphere == 'S');
  CHECK(w6.latitude_at(0).whole_degrees == 10);
}

TEST_CASE("Terrain levels convert to heights", "[wld]") {
  CHECK(terrain_level_to_height(2, 0) == 2 * 0x15e0);
  CHECK(terrain_level_to_height(2, 1) == 2 * 0xaf0);
  CHECK(terrain_level_to_height(-30, 0) == -30 * 400);
  CHECK(terrain_level_to_height(-31, 0) == -31 * 800);
}

TEST_CASE("WORLD-1.OBS holds the nine landmark objects", "[obs]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto objects = parse_objects(install.read("DATA/WORLD-1.OBS"), "WORLD-1.OBS");
  REQUIRE(objects.size() == 9);
  const std::vector<std::size_t> sizes{618, 9745, 9887, 12659, 5665, 2889, 11210, 6730, 3310};
  for (std::size_t i = 0; i < objects.size(); ++i) {
    INFO(i);
    CHECK(objects[i].shp.size() == sizes[i]);
    CHECK(objects[i].render_type ==
          (i == 6 ? ObjectRenderType::kOriented : ObjectRenderType::kBillboard));
  }
  CHECK(objects[0].size == 0xb54);
  CHECK(objects[1].size == 0x9ef0);
  CHECK(std::string(objects[0].shp.begin(), objects[0].shp.begin() + 4) == "1.00");
}

TEST_CASE("Every .ENV parses; ENV-01 header fields", "[env]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto files = install.list("DATA", ".ENV");
  REQUIRE(files.size() == 14);
  for (const auto& file : files) {
    INFO(file);
    const Environment env = parse_environment(install.read(file), file);
    CHECK(env.version == 9);
    CHECK(env.moon_phases.size() == 4);
    REQUIRE(env.sky_pcx.size() > 4);
    CHECK(env.sky_pcx[0] == 0x0a);  // PCX signature
  }

  const Environment env = parse_environment(install.read("DATA/ENV-01.ENV"), "ENV-01.ENV");
  CHECK(env.sky_pcx.size() == 24754);
  CHECK(env.ambient_minimum_pct() == 24);
  const std::vector<std::uint32_t> thresholds{0, 32000, 48000, 64000, 96000, 128000};
  for (int i = 0; i < 6; ++i) {
    CHECK(env.distance_threshold(i) == thresholds[static_cast<std::size_t>(i)]);
  }
  CHECK(env.distance_remap(5)[3] == 3);
  CHECK(env.sea_color_ramp(SeaState::kCalm, 0)[0] == 130);
  CHECK(env.flat_sea_row_bands()[0] == 5);
  CHECK(env.flat_sea_band_colors()[0] == 93);

  std::array<std::uint8_t, 0x300> pal{};
  env.apply_world_palette(pal, SeaState::kModerate);
  CHECK(pal[15 * 3] == 0);  // outside the ENV ranges: untouched
  CHECK(pal[16 * 3] == 8);
  CHECK(pal[16 * 3 + 1] == 10);
  CHECK(pal[16 * 3 + 2] == 7);
  CHECK(pal[130 * 3] == 32);  // the moderate sea table
  CHECK(pal[130 * 3 + 1] == 28);
  CHECK(pal[130 * 3 + 2] == 19);
}

TEST_CASE("Every .SCN parses; HIST01 and HIST57 contents", "[scn]") {
  SH_REQUIRE_GAME_DATA(install);
  auto files = install.list("SCEN", ".SCN");
  REQUIRE(files.size() == 60);
  const auto custom = install.list("CUSTOM", ".SCN");
  files.insert(files.end(), custom.begin(), custom.end());
  for (const auto& file : files) {
    INFO(file);
    const Scenario s = parse_scenario(install.read(file), file);
    CHECK(s.player_sub.has_value());
    CHECK_FALSE(s.ships.empty());
    CHECK(s.bomber_groups.empty());
    CHECK(s.photo_recons.empty());
  }

  const Scenario h1 = parse_scenario(install.read("SCEN/HIST01.SCN"), "HIST01.SCN");
  CHECK(h1.version == 3);
  CHECK(h1.briefing.starts_with("Bud Gruner and the Agano"));
  CHECK(h1.header.world == "WORLD-8.WLD");
  CHECK(h1.header.full_year() == 1944);
  CHECK(h1.header.month == 1);
  CHECK(h1.header.day == 16);
  CHECK(h1.header.hour == 16);
  CHECK(h1.header.weather == Weather::kClear);
  CHECK(h1.header.end_on_lost_contact == -1);  // absent before version 4
  CHECK(h1.header.required_memory == -1);
  CHECK(h1.header.tube_torpedoes[0] == 2);
  CHECK(h1.header.reloads[0].type == 1);
  CHECK(h1.header.reloads[1].forward == 10);
  CHECK(h1.header.reloads[3].type == -1);
  CHECK(h1.header.equipment == 45);
  CHECK(h1.header.has(Equipment::kSdAirRadar));
  CHECK(h1.header.has(Equipment::kBathythermograph));
  CHECK_FALSE(h1.header.has(Equipment::kSjaRadar));
  REQUIRE(h1.player_sub);
  CHECK(h1.player_sub->sub_class == "BALAO");
  CHECK(h1.player_sub->name == "(SS-305) Skate");
  CHECK(h1.player_sub->x == 6291456);
  CHECK(h1.player_sub->y == 19562496);
  CHECK(h1.player_sub->heading == 217);
  CHECK(h1.player_sub->crew == CrewQuality::kVeteran);
  REQUIRE(h1.ships.size() == 3);
  const ShipRecord& agano = h1.ships[0];
  CHECK(agano.ship_class == "AGANO");
  CHECK(agano.orders == ShipOrders::kConvoyLeader);
  CHECK(agano.speed == 12);
  CHECK(agano.waypoints.size() == 3);
  CHECK(agano.waypoints.x[2] == 3743744);
  CHECK(agano.waypoints.y[2] == 25657344);
  CHECK(agano.waypoints.x[4] == 808465457);  // stale editor memory is kept
  CHECK(h1.ships[1].orders == ShipOrders::kEscort);
  CHECK(h1.ships[1].convoy_group == 1);

  const Scenario h57 = parse_scenario(install.read("SCEN/HIST57.SCN"), "HIST57.SCN");
  CHECK(h57.version == 7);
  CHECK(h57.header.weather == Weather::kPartlyCloudy);
  CHECK(h57.header.randomize == 0);
  CHECK(h57.header.saved_time == 860011284);
  CHECK(h57.header.password.empty());
  CHECK(h57.header.required_memory == 594755);
  CHECK(h57.ships.size() == 8);
  REQUIRE(h57.enemy_subs.size() == 1);
  const EnemySubRecord& kaidai = h57.enemy_subs[0];
  CHECK(kaidai.sub_class == "KAIDAI TYPE");
  CHECK(kaidai.orders == ShipOrders::kFollowPath);
  CHECK(kaidai.crew == CrewQuality::kElite);
  CHECK(kaidai.waypoints.size() == 4);
  CHECK(kaidai.waypoints.x[3] == 10024960);
  CHECK(kaidai.target_x == -1);
  CHECK(kaidai.side == 1);
  REQUIRE(h57.aircraft.size() == 1);
  const AircraftRecord& val = h57.aircraft[0];
  CHECK(val.aircraft_type == "VAL");
  CHECK(val.mission == AircraftMission::kSearchOrbit);
  CHECK(val.orbit_radius == 297663);
  CHECK(val.bomb_load == 1);
  CHECK(val.waypoints.size() == 0);
  CHECK(val.escort_group == 1);
  CHECK(val.relaunch == 1);

  const Scenario h60 = parse_scenario(install.read("SCEN/HIST60.SCN"), "HIST60.SCN");
  CHECK(h60.aircraft.size() == 2);
  CHECK(h60.ships.size() == 5);
}

TEST_CASE("Malformed scenarios are rejected", "[scn]") {
  SH_REQUIRE_GAME_DATA(install);
  auto data = install.read("SCEN/HIST01.SCN");
  auto truncated = data;
  truncated.resize(truncated.size() - 10);
  CHECK_THROWS_AS(parse_scenario(truncated, "truncated"), DataError);
  auto bad_version = data;
  bad_version[5] = 1;
  CHECK_THROWS_AS(parse_scenario(bad_version, "v1"), DataError);
  auto bad_tag = data;
  bad_tag.back() = 9;  // the end tag
  CHECK_THROWS_AS(parse_scenario(bad_tag, "tag"), DataError);
}

TEST_CASE("Every .CR parses; SJAPAN route 0", "[cr]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto files = install.list("DATA", ".CR");
  REQUIRE(files.size() == 15);
  for (const auto& file : files) {
    INFO(file);
    const auto routes = parse_routes(install.read(file), file);
    CHECK_FALSE(routes.empty());
  }

  const auto routes = parse_routes(install.read("DATA/SJAPAN.CR"), "SJAPAN.CR");
  REQUIRE(routes.size() == 19);
  std::size_t boxes = 0;
  for (const ConvoyRoute& r : routes) {
    boxes += r.waypoints.size();
    CHECK_FALSE(r.coastal());
  }
  CHECK(boxes == 100);
  const ConvoyRoute& r0 = routes[0];
  CHECK(r0.first.month == 12);
  CHECK(r0.first.day == 1);
  CHECK(r0.first.year == 1941);
  CHECK(r0.last.month == 9);
  CHECK(r0.last.day == 30);
  CHECK(r0.last.year == 1944);
  CHECK(r0.frequency[0] == 10);
  CHECK(r0.frequency[11] == 9);
  CHECK(r0.frequency[28] == 4);
  CHECK(r0.convoy_pct[campaign_month_index(1942, 1)] == 10);
  CHECK(r0.convoy_pct[campaign_month_index(1945, 8)] == 100);
  REQUIRE(r0.waypoints.size() == 8);
  CHECK(r0.waypoints[0].x0 == 17632307);
  CHECK(r0.waypoints[0].z0 == 130744);
  CHECK(r0.waypoints[0].x1 == 20249347);
  CHECK(r0.waypoints[1].z1 == 12142849);
}

TEST_CASE("PZ.DAT lists the 15 patrol zones", "[pz]") {
  SH_REQUIRE_GAME_DATA(install);
  const auto zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  REQUIRE(zones.size() == 15);
  const PatrolZone& z = zones[0];
  CHECK(z.name == "Southern Japan");
  CHECK(z.world == "WORLD-1.WLD");
  CHECK(z.routes == "SJAPAN.CR");
  CHECK(z.bases == "MAP1BASE.DAT");
  CHECK(z.first_year == 1941);
  CHECK(z.first_month == 12);
  CHECK(z.last_year == 1945);
  CHECK(z.last_month == 8);
  CHECK(z.year_weight == std::array<std::int32_t, 5>{10, 10, 10, 10, 20});
  CHECK(z.command == Command::kComSubPac);
  CHECK(z.orders_rect == std::array<std::int32_t, 4>{118, 96, 158, 126});
  CHECK(z.start_edges == std::array<std::int32_t, 4>{1, 0, 0, 1});
  CHECK(z.sampan_density == 100);
  CHECK(z.fog_pct[3] == 15);
  CHECK(z.weather_pct[0] == std::array<std::int32_t, 4>{20, 25, 25, 30});
  CHECK(z.convoy_size_pct[0] == std::array<std::uint8_t, 3>{95, 5, 0});
  CHECK(z.escort_pct[0] == std::array<std::uint8_t, 4>{70, 30, 0, 0});
  CHECK(z.crew_pct[0] == std::array<std::uint8_t, 3>{70, 25, 5});
  CHECK(z.air_cover_pct[0] == 30);
  CHECK(z.air_raid_pct[0] == 0);
  CHECK(zones[14].name == "Aleutian Islands");
  CHECK(zones[14].bases == "MAP15BAS.DAT");
}

TEST_CASE("Every MAP<n>BASE.DAT parses; Hiroshima and Kure", "[bases]") {
  SH_REQUIRE_GAME_DATA(install);
  for (int map = 1; map <= 15; ++map) {
    const std::string file = "DATA/" + base_file_name(map);
    INFO(file);
    const auto data = install.read(file);
    CHECK(data.size() % BaseRecord::kRecordSize == 0);
    CHECK_FALSE(parse_bases(data, file).empty());
  }
  CHECK(base_file_name(9) == "MAP9BASE.DAT");
  CHECK(base_file_name(10) == "MAP10BAS.DAT");

  const auto bases = parse_bases(install.read("DATA/MAP1BASE.DAT"), "MAP1BASE.DAT");
  REQUIRE(bases.size() == 31);
  const BaseRecord& hiroshima = bases[0];
  CHECK(hiroshima.name == "Hiroshima");
  CHECK(hiroshima.station.x == 17956864);
  CHECK(hiroshima.station.y == 0);
  CHECK(hiroshima.station.z == 18939904);
  CHECK(hiroshima.harbor.x == 17973458);
  CHECK(hiroshima.patrol.z == 17252352);
  CHECK(hiroshima.owners[0].side == BaseSide::kJapanese);
  CHECK(hiroshima.owners[0].month == 12);
  CHECK(hiroshima.owners[0].day == 7);
  CHECK(hiroshima.owners[0].year == 1941);
  CHECK(hiroshima.owners[1].side == BaseSide::kNone);
  CHECK(hiroshima.flags == 51);
  CHECK(hiroshima.has(BaseFlag::kActive));
  CHECK(hiroshima.has(BaseFlag::kSpawnsPatrols));
  CHECK(hiroshima.has(BaseFlag::kAircraft));
  CHECK_FALSE(hiroshima.has(BaseFlag::kNoRecon));
  CHECK(hiroshima.anchored_size == 3);
  CHECK(hiroshima.anchored_size_cap == 3);
  CHECK(hiroshima.patrol_param_a == 1);
  CHECK(hiroshima.patrol_spawn_pct == 100);
  CHECK(hiroshima.patrol_param_b == -1);
  CHECK(hiroshima.pick_weight == 100);
  CHECK(bases[1].name == "Kure");
  CHECK(bases[1].flags == 19);
}

}  // namespace sh
