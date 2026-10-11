// The synthetic install (synthetic/synthetic_install.h): every file reads
// with the game's loaders, its scenarios build missions that run.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "assets/bases.h"
#include "assets/cat.h"
#include "assets/dat_tables.h"
#include "assets/environment.h"
#include "assets/ephemeris.h"
#include "assets/fnt.h"
#include "assets/mel.h"
#include "assets/objects.h"
#include "assets/patrol_zones.h"
#include "assets/pcx.h"
#include "assets/routes.h"
#include "assets/scenario.h"
#include "assets/scn_file.h"
#include "assets/shp.h"
#include "assets/smacker.h"
#include "assets/world.h"
#include "game/headless.h"
#include "game/mission.h"
#include "game_data.h"
#include "synthetic/manifest.h"
#include "synthetic/synthetic_install.h"

namespace sh::test {
namespace {

std::string extension(const std::string& path) { return path.substr(path.rfind('.') + 1); }

TEST_CASE("Every file of the synthetic install reads with the game's loaders", "[synthetic]") {
  const Install& install = synthetic_install();
  const GameTables tables = GameTables::load(install);
  CHECK_FALSE(tables.ships.empty());
  CHECK(tables.find_torpedo("MARK14") != nullptr);
  const std::vector<PatrolZone> zones = parse_patrol_zones(install.read("DATA/PZ.DAT"), "PZ.DAT");
  REQUIRE(zones.size() == 15);
  for (const PatrolZone& z : zones) {
    INFO(z.name);
    const World w = parse_world(install.read("DATA/" + z.world), z.world);
    CHECK_FALSE(w.locations.empty());
    CHECK_FALSE(parse_routes(install.read("DATA/" + z.routes), z.routes).empty());
    CHECK_FALSE(parse_bases(install.read("DATA/" + z.bases), z.bases).empty());
  }
  CHECK_NOTHROW(Ephemeris::load(install));
  for (const auto& [path, m] : committed_manifest()) {
    INFO(path);
    if (path.starts_with("SAVE/") || path.ends_with(".RPT")) {
      continue;  // the player's files (the game writes them), build reports
    }
    REQUIRE(install.exists(path));
    const std::string ext = extension(path);
    if (m.kind == "pcx") {
      CHECK(load_pcx(install, path).image.width() == m.numbers.at(0));
    } else if (m.kind == "shp") {
      CHECK(Shp::load(install, path).frame_count() == m.sprites.at(0).frames.size());
    } else if (m.kind == "fnt") {
      CHECK(Font::load(install, path).height() == m.numbers.at(0));
    } else if (m.kind == "cat") {
      CHECK_FALSE(CatArchive::load(install, path).entries().empty());
    } else if (m.kind == "obs") {
      CHECK(parse_objects(install.read(path), path).size() == m.sprites.size());
    } else if (m.kind == "env") {
      CHECK(parse_environment(install.read(path), path).version == m.numbers.at(0));
    } else if (m.kind == "mel") {
      CHECK(parse_mel(install.read(path), path).pcm.size() == static_cast<std::size_t>(m.numbers.at(5)));
    } else if (m.kind == "bnk") {
      CHECK(parse_bnk(install.read(path), path).size() == m.numbers.size());
    } else if (m.kind == "smk") {
      SmackerVideo video = SmackerVideo::load(install, path);
      CHECK(video.width() == m.numbers.at(0));
      while (video.decode_next()) {
      }
    } else if (ext == "SCN") {
      const Scenario s = parse_scenario(install.read(path), path);
      CHECK(s.player_sub.has_value());
      CHECK(write_scn_file(read_scn_file(install.read(path), path)) == install.read(path));
    }
  }
}

// The missions run with the process-wide SH.EXE: the synthetic one without
// SH_DATA, else the original's (game_exe_setup.cpp).
TEST_CASE("The synthetic scenarios build missions that run", "[synthetic]") {
  const Install& install = synthetic_install();
  const GameTables tables = GameTables::load(install);
  for (const std::string& path : install.list("SCEN", ".SCN")) {
    INFO(path);
    std::unique_ptr<Mission> mission = HeadlessRunner::load(install, tables, path);
    HeadlessRunner runner(*mission);
    runner.run(120);
    CHECK(mission->end_code() == 0);
  }
}

}  // namespace
}  // namespace sh::test
