// `SBOPTION.SAV`: the shipped values, the round trip, and the rewrite of a
// missing or wrong-version file with all options on.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/install.h"
#include "editor/options.h"
#include "game/saves_options.h"
#include "game/shpath.h"
#include "game_data.h"
#include "temp_dir.h"

namespace sh::editor {
namespace {

using test::TempDir;

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::vector<std::uint8_t> all_on_file() {
  std::vector<std::uint8_t> out{EditorOptions::kVersion};
  out.insert(out.end(), 36, 0xff);
  return out;
}

}  // namespace

TEST_CASE("The shipped SBOPTION.SAV reads back byte for byte", "[editor_options]") {
  SH_REQUIRE_GAME_DATA(install);
  const TempDir dir;
  const SaveDir saves(dir.path(), &install, ShPaths::load(install).dir(GameDir::kSave));
  const EditorOptions o = EditorOptions::load(saves);
  CHECK(o.limit_ships == kOptionOff);
  CHECK(o.limit_equipment == kOptionOff);
  CHECK(o.range_circles == kOptionOn);
  CHECK(o.convoy_icons == kOptionOn);
  CHECK(o.end_on_lost_contact == kOptionOff);
  CHECK(o.randomize == kOptionOff);
  CHECK(o.to_bytes() == install.read("SAVE/SBOPTION.SAV"));
  CHECK_FALSE(std::filesystem::exists(dir.path() / "SBOPTION.SAV"));  // the install is never written
}

TEST_CASE("SBOPTION.SAV saves and loads every option", "[editor_options]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  EditorOptions o;
  o.limit_subs = kOptionOff;
  o.convoy_icons = kOptionOff;
  o.randomize = kOptionOff;
  REQUIRE(o.save(saves));
  CHECK(read_file(dir.path() / "SBOPTION.SAV").size() == 37);
  CHECK(EditorOptions::load(saves) == o);
}

TEST_CASE("A missing or wrong-version SBOPTION.SAV is rewritten with all options on", "[editor_options]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  const std::filesystem::path file = dir.path() / "SBOPTION.SAV";

  CHECK(EditorOptions::load(saves) == EditorOptions{});
  CHECK(read_file(file) == all_on_file());

  std::vector<std::uint8_t> version1 = EditorOptions{.limit_ships = kOptionOff}.to_bytes();
  version1[0] = 1;
  write_bytes(file, version1);
  CHECK(EditorOptions::load(saves) == EditorOptions{});
  CHECK(read_file(file) == all_on_file());
}

TEST_CASE("A short SBOPTION.SAV keeps the values read and is not rewritten", "[editor_options]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  const std::filesystem::path file = dir.path() / "SBOPTION.SAV";

  const std::vector<std::uint8_t> short_file = {2, 0, 0, 0, 0, 0, 0, 0, 0, 0xff};
  write_bytes(file, short_file);
  const EditorOptions o = EditorOptions::load(saves);
  CHECK(o.limit_ships == kOptionOff);
  CHECK(o.limit_subs == kOptionOff);
  CHECK(o.limit_aircraft == kOptionOn);
  CHECK(o.randomize == kOptionOn);
  CHECK(read_file(file) == short_file);

  // No version byte: the defaults, and the empty file stays.
  write_bytes(file, {});
  CHECK(EditorOptions::load(saves) == EditorOptions{});
  CHECK(read_file(file).empty());
}

TEST_CASE("An unwritable wrong-version SBOPTION.SAV gives the defaults instead of hanging", "[editor_options]") {
  const TempDir dir;
  // The install's SAVE\SBOPTION.SAV has another version, and the save
  // directory cannot be created (a file is in its way).
  write_bytes(dir.path() / "install" / "SAVE" / "SBOPTION.SAV", {1, 0, 0, 0, 0});
  write_bytes(dir.path() / "install" / "SH.EXE", {0});
  write_bytes(dir.path() / "blocked", {0});
  const Install install(dir.path() / "install");
  const SaveDir saves(dir.path() / "blocked" / "save", &install, "SAVE\\");
  CHECK(EditorOptions::load(saves) == EditorOptions{});
}

}  // namespace sh::editor
