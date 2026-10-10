// The File commands over a temporary CUSTOM directory: a saved scenario
// listed and loaded back byte for byte, the overwrite and delete
// confirmations (an install scenario masked when deleted), "Save changes to
// scenario?" before New and Load, the password accepted or refused, Run
// writing AEONSCEN.TMP as Save would, the write errors, the range check and
// the list bug that overwrites the current briefing.

#include "editor/file_commands.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "assets/scn_file.h"
#include "core/install.h"
#include "editor_ui_fixture.h"
#include "game/shpath.h"
#include "game_data.h"
#include "password_key.h"
#include "temp_dir.h"

namespace sh::editor {
namespace {

using test::EditorFixture;
using test::TempDir;

struct Point {
  int x;
  int y;
};
constexpr Point at(int dx, int dy, int x, int y, int w, int h) { return {dx + x + w / 2, dy + y + h / 2}; }

// The Load / Delete dialog at (134, 50): its rows and LOAD / DELETE.
constexpr Point list_row(int row) { return {134 + 14 + 60, 50 + 54 + 1 + row * 16 + 8}; }
constexpr Point kListButton = at(134, 50, 234, 325, 100, 24);
// SAVE SCENARIO at (164, 110): its first name and SAVE.
constexpr Point kSaveRow0 = {164 + 34 + 60, 110 + 54 + 1 + 8};
constexpr Point kSaveButton = at(164, 110, 178, 245, 100, 24);
constexpr Point kSaveChangesDiscard = at(203, 142, 67, 92, 100, 24);
constexpr Point kSaveChangesCancel = at(203, 142, 67, 169, 100, 24);
constexpr std::uint16_t kYes = 0x1559;
constexpr std::uint16_t kNo = 0x314e;

// The fixture with File commands over a CUSTOM directory in a scratch
// directory (the install's CUSTOM\ read behind it).
struct FilesRig {
  explicit FilesRig(const Install& game)
      : fx(game),
        paths(ShPaths::load(game)),
        custom(dir.path() / "CUSTOM", &game, paths.dir(GameDir::kCustom)),
        files(FileEnv{.ui = fx.ui, .scenario = fx.scenario, .install = game, .paths = paths, .custom = custom}) {
    // No "NO SHIPS IN RANGE?" unless a test asks for it.
    fx.scenario.header.end_on_lost_contact = 0;
  }

  void type(std::string_view text) {
    for (const char c : text) {
      fx.key(static_cast<std::uint16_t>(static_cast<unsigned char>(c)));
    }
  }
  void click(Point p) { fx.click(p.x, p.y); }
  std::filesystem::path user_file(std::string_view name) const { return dir.path() / "CUSTOM" / name; }

  TempDir dir;
  EditorFixture fx;
  ShPaths paths;
  SaveDir custom;
  FileCommands files;
};

std::vector<std::string> titles(const std::vector<ScenarioListEntry>& entries) {
  std::vector<std::string> out;
  for (const ScenarioListEntry& e : entries) {
    out.push_back(e.title);
  }
  return out;
}

void set_briefing(ScnHeader& header, std::string_view text) {
  header.briefing.fill(0);
  std::ranges::copy(text, header.briefing.begin());
}

}  // namespace

TEST_CASE("A saved scenario is listed and loads back byte for byte", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  set_briefing(rig.fx.scenario.header, "My own patrol");
  rig.fx.scenario.modified = true;

  rig.type("mine");
  rig.fx.key(ui::kKeyEnter);
  REQUIRE(rig.files.save() == ui::kResultAccept);
  CHECK(rig.files.name == "MINE");
  CHECK_FALSE(rig.fx.scenario.modified);
  REQUIRE(std::filesystem::exists(rig.user_file("MINE.SCN")));
  const std::vector<std::uint8_t> saved = read_file(rig.user_file("MINE.SCN"));
  CHECK(saved == write_scn_file(rig.fx.scenario.save()));

  // The player's scenario comes before the install's SITDUCK.
  CHECK(save_list_build(rig.custom) == std::vector<std::string>{"MINE", "SITDUCK"});
  const FileEnv env{.ui = rig.fx.ui, .scenario = rig.fx.scenario, .install = install, .paths = rig.paths,
                    .custom = rig.custom};
  CHECK(titles(scenario_list_build(env, false)) == std::vector<std::string>{"MINE", "SITDUCK"});

  rig.files.new_scenario();
  CHECK(rig.files.name.empty());
  rig.click(list_row(0));
  rig.click(kListButton);
  REQUIRE(rig.files.load(ScenarioListMode::kLoadScenario));
  CHECK(rig.files.name == "MINE");
  CHECK(scn_text(rig.fx.scenario.header.briefing) == "My own patrol");
  CHECK(write_scn_file(rig.fx.scenario.save()) == saved);
}

TEST_CASE("Saving over an existing name asks first", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  rig.files.name = "OLD";

  SECTION("NO keeps the file and the name") {
    rig.click(kSaveRow0);
    rig.click(kSaveButton);
    rig.fx.screenshot("se_overwrite");
    rig.fx.key(kNo);
    CHECK(rig.files.save() == ui::kResultCancel);
    CHECK(rig.files.name == "OLD");
    CHECK_FALSE(std::filesystem::exists(rig.user_file("SITDUCK.SCN")));
  }
  SECTION("YES writes it in the player's directory") {
    rig.click(kSaveRow0);
    rig.click(kSaveButton);
    rig.fx.key(kYes);
    CHECK(rig.files.save() == ui::kResultAccept);
    CHECK(rig.files.name == "SITDUCK");
    CHECK(std::filesystem::exists(rig.user_file("SITDUCK.SCN")));
  }
}

TEST_CASE("Delete Scenario removes a file after a confirmation", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  rig.files.name = "MINE";
  REQUIRE(rig.custom.write("MINE.SCN", write_scn_file(rig.fx.scenario.save())));

  // NO first, then YES.
  rig.click(list_row(0));
  rig.click(kListButton);
  rig.fx.screenshot("se_delete_confirm");
  rig.fx.key(kNo);
  CHECK_FALSE(rig.files.load(ScenarioListMode::kDeleteScenario));
  CHECK(std::filesystem::exists(rig.user_file("MINE.SCN")));
  rig.click(list_row(0));
  rig.click(kListButton);
  rig.fx.key(kYes);
  rig.files.load(ScenarioListMode::kDeleteScenario);
  CHECK_FALSE(std::filesystem::exists(rig.user_file("MINE.SCN")));

  // The install's scenario is masked, not deleted.
  rig.click(list_row(0));
  rig.click(kListButton);
  rig.fx.key(kYes);
  rig.files.load(ScenarioListMode::kDeleteScenario);
  CHECK(save_list_build(rig.custom).empty());
  CHECK(install.exists("CUSTOM/SITDUCK.SCN"));
  // The name of the scenario being edited is not touched by Delete.
  CHECK(rig.files.name == "MINE");
}

TEST_CASE("New and Load ask to save a modified scenario", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  rig.fx.scenario.header.hour = 3;
  rig.fx.scenario.modified = true;

  rig.click(kSaveChangesCancel);
  CHECK_FALSE(rig.files.new_scenario());
  CHECK(rig.fx.scenario.header.hour == 3);
  rig.click(kSaveChangesCancel);
  CHECK_FALSE(rig.files.load(ScenarioListMode::kLoadHistorical));
  rig.click(kSaveChangesDiscard);
  CHECK(rig.files.new_scenario());
  CHECK(rig.fx.scenario.header.hour == 12);
  CHECK_FALSE(rig.fx.scenario.modified);
}

TEST_CASE("A scenario's password is asked on load", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  rig.fx.scenario.header.password = encode_scn_password("Secret", sh::test::se_password_key(install));
  rig.fx.scenario.header.hour = 5;
  REQUIRE(rig.custom.write("LOCKED.SCN", write_scn_file(rig.fx.scenario.save())));
  rig.files.new_scenario();

  SECTION("Accepted, case-insensitively") {
    rig.click(list_row(0));
    rig.click(kListButton);
    rig.type("SECRET");
    rig.fx.key(ui::kKeyEnter);
    CHECK(rig.files.load(ScenarioListMode::kLoadScenario));
    CHECK(rig.files.name == "LOCKED");
    CHECK(rig.fx.scenario.header.hour == 5);
  }
  SECTION("Refused after three tries: a new scenario without a name") {
    rig.click(list_row(0));
    rig.click(kListButton);
    for (int i = 0; i < 3; ++i) {
      rig.type("guess");
      rig.fx.key(ui::kKeyEnter);
      rig.fx.key(ui::kKeyEnter);  // ACCESS DENIED!
    }
    CHECK(rig.files.load(ScenarioListMode::kLoadScenario));
    CHECK(rig.files.name.empty());
    CHECK(rig.fx.scenario.header.hour == 12);
    CHECK(rig.fx.scenario.objects().size() == 1);
  }
}

TEST_CASE("Run writes AEONSCEN.TMP as Save writes a scenario", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  rig.fx.scenario.header.minute = 42;
  rig.fx.scenario.modified = true;

  // "Save changes to scenario?": DISCARD, then the file is written anyway.
  rig.click(kSaveChangesDiscard);
  REQUIRE(rig.files.run());
  const std::vector<std::uint8_t> run = read_file(rig.user_file(kRunFileName));
  CHECK(run == write_scn_file(rig.fx.scenario.save()));
  // The game reads it like any custom scenario.
  REQUIRE(rig.custom.read(kRunFileName));
  CHECK(read_scn_file(*rig.custom.read(kRunFileName), "AEONSCEN.TMP").minute == 42);
  // A file of the install is never written.
  CHECK_FALSE(install.exists("CUSTOM/AEONSCEN.TMP"));
}

TEST_CASE("The range and size checks ask before saving and running", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  rig.fx.scenario.header.end_on_lost_contact = -1;
  // The player alone: no ship in range.
  rig.fx.screenshot("se_no_ships_in_range");
  rig.fx.key(kNo);
  CHECK_FALSE(rig.files.run());
  CHECK_FALSE(std::filesystem::exists(rig.user_file(kRunFileName)));
  rig.fx.key(kYes);
  CHECK(rig.files.run());
  rig.fx.key(kNo);
  CHECK(rig.files.save() == ui::kResultCancel);
}

TEST_CASE("Write errors are reported", "[editor][file]") {
  SH_REQUIRE_GAME_DATA(install);
  if (std::filesystem::exists("/dev/full")) {
    const std::vector<std::uint8_t> bytes(10000, 1);
    CHECK(write_scenario_file("/dev/full", bytes) == WriteResult::kDiskFull);
  }
  FilesRig rig(install);
  std::filesystem::create_directories(rig.dir.path() / "CUSTOM");
  std::filesystem::permissions(rig.dir.path() / "CUSTOM", std::filesystem::perms::owner_read |
                                                              std::filesystem::perms::owner_exec);
  if (write_scenario_file(rig.user_file("PROBE.SCN"), std::vector<std::uint8_t>{1}) == WriteResult::kOk) {
    SKIP("the directory is writable anyway (running as root?)");
  }
  CHECK(write_scenario_file(rig.user_file("X.SCN"), std::vector<std::uint8_t>{1}) == WriteResult::kCannotOpen);
  // Run: "CANNOT CREATE FILE" and no run.
  rig.fx.screenshot("se_cannot_create");
  rig.fx.key(ui::kKeyEnter);
  CHECK_FALSE(rig.files.run());
  // Save: "CANNOT OPEN FILE", the modified flag kept.
  rig.fx.scenario.modified = true;
  rig.type("mine");
  rig.fx.key(ui::kKeyEnter);
  rig.fx.key(ui::kKeyEnter);
  CHECK(rig.files.save() == ui::kResultCancel);
  CHECK(rig.fx.scenario.modified);
  std::filesystem::permissions(rig.dir.path() / "CUSTOM", std::filesystem::perms::owner_all);
}

TEST_CASE("A scenario is written whole or not at all, under a UTF-8 path", "[editor][file][ioreview]") {
  TempDir dir;
  const std::filesystem::path folder = dir.path() / std::filesystem::path(u8"Profil \u00e9t\u00e9");
  const std::filesystem::path file = folder / "MINE.SCN";
  const std::vector<std::uint8_t> v1{1, 2, 3};
  REQUIRE(write_scenario_file(file, v1) == WriteResult::kOk);
  CHECK(read_file(file) == v1);
  CHECK_FALSE(std::filesystem::exists(folder / "MINE.SCN.tmp"));
  // The temporary file cannot be created: the old scenario survives.
  std::filesystem::create_directory(folder / "MINE.SCN.tmp");
  CHECK(write_scenario_file(file, std::vector<std::uint8_t>{9}) == WriteResult::kCannotOpen);
  CHECK(read_file(file) == v1);
}

TEST_CASE("Listing the scenarios keeps the current briefing", "[editor][file]") {
  // Regression test for the original bug "scenario lists overwrite the
  // briefing" (SE left the last listed file's briefing and version).
  SH_REQUIRE_GAME_DATA(install);
  FilesRig rig(install);
  set_briefing(rig.fx.scenario.header, "Mine");
  const ScnHeader before = rig.fx.scenario.header;
  rig.fx.key(ui::kKeyEsc);
  CHECK_FALSE(rig.files.load(ScenarioListMode::kLoadScenario));
  rig.fx.key(ui::kKeyEsc);
  CHECK_FALSE(rig.files.load(ScenarioListMode::kLoadHistorical));
  rig.fx.key(ui::kKeyEsc);
  CHECK_FALSE(rig.files.load(ScenarioListMode::kDeleteScenario));
  CHECK(scn_text(rig.fx.scenario.header.briefing) == "Mine");
  CHECK(rig.fx.scenario.header.version == before.version);
  CHECK_FALSE(rig.fx.scenario.modified);
}

}  // namespace sh::editor
