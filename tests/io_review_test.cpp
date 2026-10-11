// I/O regressions: atomic and case-insensitive saves, the
// parsers' bounds on hostile sizes, and the audio's file reads kept out of
// the audio lock.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "assets/environment.h"
#include "assets/exe_image.h"
#include "assets/fnt.h"
#include "assets/pcx.h"
#include "assets/shp.h"
#include "assets/smacker.h"
#include "assets/speech.h"
#include "assets/world.h"
#include "audio/mel_layer.h"
#include "audio/sample_table.h"
#include "core/error.h"
#include "core/install.h"
#include "game/mission_save.h"
#include "game/saves_options.h"
#include "game_data.h"
#include "view/render_list.h"

namespace sh {
namespace {

using Bytes = std::vector<std::uint8_t>;

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_io_review_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void put_file(const std::filesystem::path& path, const Bytes& data) {
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                              static_cast<std::streamsize>(data.size()));
}

void put_u32(Bytes& data, std::size_t at, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    data[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
  }
}

void put_u16(Bytes& data, std::size_t at, std::uint16_t v) {
  data[at] = static_cast<std::uint8_t>(v);
  data[at + 1] = static_cast<std::uint8_t>(v >> 8);
}

// A one-frame sprite set with box (x0, y0)-(x1, y1) and `rows`.
Bytes one_frame_shp(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, const Bytes& rows) {
  Bytes d = {'1', '.', '1', '0'};
  d.resize(16 + 0x18);
  put_u32(d, 4, 1);
  put_u32(d, 8, 16);
  put_u32(d, 16 + 8, static_cast<std::uint32_t>(x0));
  put_u32(d, 16 + 12, static_cast<std::uint32_t>(y0));
  put_u32(d, 16 + 16, static_cast<std::uint32_t>(x1));
  put_u32(d, 16 + 20, static_cast<std::uint32_t>(y1));
  d.insert(d.end(), rows.begin(), rows.end());
  return d;
}

}  // namespace

TEST_CASE("SaveDir writes atomically: a failed write keeps the old file", "[ioreview]") {
  TempDir dir;
  const SaveDir saves(dir.path());
  const Bytes v1{1, 2, 3};
  REQUIRE(saves.write("A.SAV", v1));
  CHECK(saves.read("A.SAV") == v1);
  CHECK_FALSE(std::filesystem::exists(dir.path() / "A.SAV.tmp"));
  // The temporary file cannot be created: the write fails, A.SAV survives.
  std::filesystem::create_directory(dir.path() / "A.SAV.tmp");
  CHECK_FALSE(saves.write("A.SAV", Bytes{9, 9}));
  CHECK(saves.read("A.SAV") == v1);
  // A device is written in place, its failed flush reported.
  if (std::filesystem::exists("/dev/full")) {
    CHECK_FALSE(SaveDir("/dev").write("full", Bytes(3000, 7)));
  }
}

TEST_CASE("Deleting a mission save keeps a save whose renumbering copy failed", "[ioreview]") {
  TempDir dir;
  const SaveDir saves(dir.path());
  const Bytes two{2, 2}, three{3, 3};
  REQUIRE(saves.write("MISS1.SAV", Bytes{1}));
  REQUIRE(saves.write("MISS2.SAV", two));
  REQUIRE(saves.write("MISS3.SAV", three));
  // MISS2 cannot be copied to MISS1.
  std::filesystem::create_directory(dir.path() / "MISS1.SAV.tmp");
  delete_mission_save(saves, 1);
  CHECK_FALSE(saves.read("MISS1.SAV"));
  CHECK(saves.read("MISS2.SAV") == two);
  CHECK(saves.read("MISS3.SAV") == three);
  // Without the obstacle the saves move down.
  std::filesystem::remove(dir.path() / "MISS1.SAV.tmp");
  REQUIRE(saves.write("MISS1.SAV", Bytes{1}));
  delete_mission_save(saves, 1);
  CHECK(saves.read("MISS1.SAV") == two);
  CHECK(saves.read("MISS2.SAV") == three);
  CHECK_FALSE(saves.read("MISS3.SAV"));
}

TEST_CASE("SaveDir finds lower-case files under the listed upper-case names", "[ioreview]") {
  TempDir dir;
  put_file(dir.path() / "miss4.sav", Bytes{4});
  const SaveDir saves(dir.path());
  REQUIRE(saves.list(".SAV") == std::vector<std::string>{"MISS4.SAV"});
  CHECK(saves.read("MISS4.SAV") == Bytes{4});
  // Writing replaces the file of the other case instead of adding one.
  REQUIRE(saves.write("MISS4.SAV", Bytes{5}));
  CHECK(std::filesystem::exists(dir.path() / "miss4.sav"));
  CHECK(saves.read("miss4.sav") == Bytes{5});
  saves.remove("MISS4.SAV");
  CHECK(saves.list(".SAV").empty());
}

TEST_CASE("SHP frame boxes are bounded", "[ioreview]") {
  // One row of 2 literal pixels, then the end of the row.
  const Bytes row{0x05, 7, 8, 0x00};
  CHECK(Shp::parse(one_frame_shp(-1, 0, 0, 0, row), "ok").frame(0).width() == 2);
  // A box past 16 bits with rows: rejected before any allocation.
  CHECK_THROWS_AS(Shp::parse(one_frame_shp(-0x8000, 0, 0x10, 0, row), "x"), DataError);
  CHECK_THROWS_AS(Shp::parse(one_frame_shp(0, 0, 1, 0x7fff, row), "x"), DataError);
  CHECK_THROWS_AS(Shp::parse(one_frame_shp(std::numeric_limits<std::int32_t>::min(), 0,
                                           std::numeric_limits<std::int32_t>::max(), 0, row),
                             "x"),
                  DataError);
  // No rows: an empty frame whatever its box.
  const Shp empty = Shp::parse(one_frame_shp(0x7fffff7e, 0x7ffffeab, 0x7fffff80, 0x7ffffead, {}), "x");
  CHECK(empty.frame(0).empty());
  CHECK(empty.frame(0).width() == 0);
}

TEST_CASE("SBCHART.SHP's huge-box frame stays an empty frame", "[ioreview][content]") {
  SH_REQUIRE_GAME_DATA(install);
  const Shp shp = Shp::load(install, "ART/SBCHART.SHP");
  const SpriteFrame& f = shp.frame(42);
  CHECK(f.x0 == 2147483518);
  CHECK(f.empty());
  CHECK(f.height() == 0);
}

TEST_CASE("PCX sizes the data cannot hold are rejected", "[ioreview]") {
  Bytes d(128 + 4 + 769, 0);
  d[0] = 0x0a;
  d[2] = 1;
  d[3] = 8;
  d[65] = 1;
  put_u16(d, 0x42, 4);
  put_u16(d, 6, 0);
  put_u16(d, 10, 0);
  d[128] = 0xc4;  // one row: 4 pixels of colour 1
  d[129] = 1;
  CHECK(decode_pcx(d, "ok").image.height() == 1);
  // ymin above ymax (formerly a wrap to 65536 rows).
  put_u16(d, 6, 5);
  put_u16(d, 10, 4);
  CHECK_THROWS_AS(decode_pcx(d, "x"), DataError);
  // 65535 x 65535 pixels from 900 bytes.
  put_u16(d, 6, 0);
  put_u16(d, 10, 0xfffe);
  put_u16(d, 0x42, 0xffff);
  CHECK_THROWS_AS(decode_pcx(d, "x"), DataError);
}

TEST_CASE("The .WLD grid size is checked before decoding its block", "[ioreview]") {
  SH_REQUIRE_INSTALL(install);
  Bytes d = install.read("DATA/WORLD-1.WLD");
  const World w = parse_world(d, "WORLD-1");
  const std::size_t grid = 6 + (w.version >= 6 ? 31 : 0) + 4 + 2 + 24 * w.locations.size();
  REQUIRE(static_cast<std::int32_t>(d[grid] | d[grid + 1] << 8) == w.grid.width);
  put_u32(d, grid, 0x7fffffff);
  put_u32(d, grid + 4, 0x7fffffff);
  CHECK_THROWS_AS(parse_world(d, "x"), DataError);
}

TEST_CASE(".ENV sea fields used as indices and shift are validated", "[ioreview]") {
  SH_REQUIRE_INSTALL(install);
  const Bytes original = install.read("DATA/ENV-01.ENV");
  REQUIRE_NOTHROW(parse_environment(original, "ENV-01"));
  Bytes d = original;
  d[6 + 0x304] = 32;
  CHECK_THROWS_AS(parse_environment(d, "x"), DataError);
  d = original;
  d[6 + 0x368 + 100] = 6;
  CHECK_THROWS_AS(parse_environment(d, "x"), DataError);
}

TEST_CASE("Smacker audio chunks above the header's maximum are rejected", "[ioreview][content]") {
  SH_REQUIRE_GAME_DATA(install);
  Bytes d = install.read("CINES/BOT.SMK");
  {
    SmackerVideo video(d, "BOT");
    REQUIRE(video.audio_tracks().at(0).max_chunk_size == 24260);
    REQUIRE(video.decode_next());
  }
  put_u32(d, 0x18, 16);  // track 0's largest decoded chunk
  SmackerVideo video(d, "x");
  CHECK_THROWS_AS(video.decode_next(), DataError);
}

TEST_CASE("Projected sizes never divide by a wrapped zero", "[ioreview]") {
  // 0x10000 << 16 wraps a 32-bit divisor to 0.
  CHECK(projected_size(100u << 16, 0x10000, 16) == 0);
  CHECK(projected_size(100u << 16, 0x10, 16) == 100u << 16 >> 20);
  CHECK(projected_size(7, 0, 0) == 7);
}

TEST_CASE("Signed overflows of the saved-mission text and the map longitude", "[ioreview]") {
  SH_REQUIRE_INSTALL(install);
  const ExeImage exe(install.read("SH.EXE"));
  const Font font = Font::load(install, "ART/MENU_S.FNT");
  MissionSaveHeader h;
  h.speed = std::numeric_limits<std::int32_t>::min();
  CHECK(saved_mission_summary(h, exe, font).find("-32768") != std::string::npos);

  World w = parse_world(install.read("DATA/WORLD-1.WLD"), "WORLD-1");
  REQUIRE(w.bounds);
  w.bounds->lon_west = Fixed16::from_raw(std::numeric_limits<std::int32_t>::min() + 1);
  w.bounds->lon_east = Fixed16::from_raw(std::numeric_limits<std::int32_t>::max());
  CHECK_NOTHROW(w.longitude_at(0));
}

TEST_CASE("Effects load before the lock and speech starts from read PCM", "[ioreview]") {
  SH_REQUIRE_INSTALL(install);
  SampleTable table(&install);
  MelLayer layer(table, [] { return 0; });
  layer.preload_sfx(10);
  CHECK(table.contains(10));
  CHECK_FALSE(layer.channel(0).playing);
  CHECK(layer.play_sfx(10) == 1);

  REQUIRE(start_speech_raw(layer, read_speech_raw(install, "2DEEP.RAW")));
  CHECK(layer.speech_is_playing());
  // A missing file stops the current clip, as play_speech_raw.
  CHECK_FALSE(start_speech_raw(layer, read_speech_raw(install, "NOSUCH.RAW")));
  CHECK_FALSE(layer.speech_is_playing());
}

}  // namespace sh
