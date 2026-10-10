// The lossless `.SCN` model: the shipped files written back, older versions
// re-saved as version 7, the password, malformed files.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "assets/scenario.h"
#include "assets/scn_file.h"
#include "core/error.h"
#include "core/install.h"
#include "game_data.h"
#include "password_key.h"

namespace sh {
namespace {

std::vector<std::string> shipped_scenarios(const Install& install) {
  std::vector<std::string> files = install.list("SCEN", ".SCN");
  const std::vector<std::string> custom = install.list("CUSTOM", ".SCN");
  files.insert(files.end(), custom.begin(), custom.end());
  return files;
}

// Builds a file image field by field, with the fields of `version` only.
class FileBuilder {
 public:
  explicit FileBuilder(std::uint8_t version) : version_(version) {
    for (const char c : std::string_view("ASCN")) {
      u8(static_cast<std::uint8_t>(c));
    }
    u8(0);
    u8(version);
  }

  void u8(std::uint8_t v) { data_.push_back(v); }
  void i32(std::int32_t v) {
    const auto u = static_cast<std::uint32_t>(v);
    for (int shift = 0; shift < 32; shift += 8) {
      data_.push_back(static_cast<std::uint8_t>(u >> shift));
    }
  }
  void text(std::string_view s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
      u8(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
    }
  }
  void i32s(std::size_t n, std::int32_t first) {
    for (std::size_t i = 0; i < n; ++i) {
      i32(first + static_cast<std::int32_t>(i));
    }
  }
  // A field present from `min_version` on.
  void since(std::uint8_t min_version, std::int32_t v) {
    if (version_ >= min_version) {
      i32(v);
    }
  }

  const std::vector<std::uint8_t>& data() const { return data_; }

 private:
  std::uint8_t version_;
  std::vector<std::uint8_t> data_;
};

// A file of `version` with every record kind, out of the game's grouping
// order, and a special mission flag the reader clears below version 4.
std::vector<std::uint8_t> synthetic_file(std::uint8_t version) {
  FileBuilder b(version);
  b.text("Briefing", kScnBriefingSize);
  b.i32(-1);  // special_mission
  b.text("WORLD-3.WLD", 13);
  b.i32s(9, 1);  // weather .. minute
  b.since(4, 0);
  b.since(4, 0);
  b.since(4, 1234);
  if (version >= 4) {
    b.text("", kScnPasswordSize);
  }
  b.since(7, 400000);
  b.i32s(10, 2);  // tubes
  b.i32s(15, 0);  // reloads
  b.i32(5);       // equipment
  b.u8(2);        // ship
  b.i32s(7, 10);
  b.text("FUSO", 25);
  b.text("Fuso", 31);
  b.i32s(51, 100);
  b.i32s(3, 0);
  b.u8(3);  // aircraft
  b.i32(2);
  b.i32(1);
  b.text("BETTY", 25);
  b.i32s(5, 7);
  b.i32s(51, 200);
  b.i32(1);
  b.since(6, 0);  // relaunch
  b.u8(1);        // player
  b.i32s(6, 20);
  b.text("BALAO", 10);
  b.text("(SS-305) Skate", 31);
  b.since(3, 2);  // crew
  b.u8(4);        // enemy sub
  b.i32s(8, 30);
  b.text("KAIDAI TYPE", 25);
  b.i32s(51, 300);
  b.i32s(3, 40);
  b.since(5, 1);  // side
  b.u8(5);        // bomber group
  b.i32s(4, 50);
  b.u8(6);  // photo recon
  b.i32s(3, 60);
  b.u8(0);
  return b.data();
}

}  // namespace

TEST_CASE("Shipped version-7 .SCN files are written back byte-identical", "[scn_file]") {
  SH_REQUIRE_GAME_DATA(install);
  int version7 = 0;
  for (const std::string& file : shipped_scenarios(install)) {
    INFO(file);
    const std::vector<std::uint8_t> data = install.read(file);
    const ScnFile scn = read_scn_file(data, file);
    if (scn.version == kScnWriteVersion) {
      ++version7;
      CHECK(write_scn_file(scn) == data);
    }
  }
  CHECK(version7 == 31);
}

TEST_CASE("Older .SCN versions are re-saved as version 7 with the reader's defaults", "[scn_file]") {
  SH_REQUIRE_GAME_DATA(install);
  int older = 0;
  for (const std::string& file : shipped_scenarios(install)) {
    INFO(file);
    const std::vector<std::uint8_t> data = install.read(file);
    const ScnFile scn = read_scn_file(data, file);
    const std::vector<std::uint8_t> saved = write_scn_file(scn);
    REQUIRE(saved[5] == kScnWriteVersion);
    // Nothing is lost: the saved file reads back to the same model.
    ScnFile reread = read_scn_file(saved, file);
    CHECK(reread.version == kScnWriteVersion);
    reread.version = scn.version;
    CHECK(reread == scn);
    // The game sees the same scenario but for the version.
    Scenario game = parse_scenario(saved, file);
    CHECK(game.version == kScnWriteVersion);
    game.version = scn.version;
    CHECK(game == parse_scenario(data, file));
    if (scn.version < kScnWriteVersion) {
      ++older;
      // Original bug (docs/editor-design.md "Pre-v7 required_memory"): the editor
      // accumulates its estimate from this -1, so it ends one byte low.
      CHECK(scn.required_memory == -1);
    }
  }
  CHECK(older == 30);

  const ScnFile h1 = read_scn_file(install.read("SCEN/HIST01.SCN"), "HIST01.SCN");
  CHECK(h1.version == 3);
  CHECK(h1.end_on_lost_contact == -1);
  CHECK(h1.randomize == -1);
  CHECK(h1.random_seed == 0);
  CHECK(h1.password == std::array<std::uint8_t, kScnPasswordSize>{});
  CHECK(scn_text(h1.world) == "WORLD-8.WLD");
  REQUIRE(std::holds_alternative<ScnPlayerSub>(h1.records[0]));
  CHECK(scn_text(std::get<ScnPlayerSub>(h1.records[0]).name) == "(SS-305) Skate");
}

TEST_CASE("Every version from 2 to 7 reads with the reader's defaults and keeps the record order", "[scn_file]") {
  for (std::uint8_t version = 2; version <= 7; ++version) {
    INFO(static_cast<int>(version));
    const std::vector<std::uint8_t> data = synthetic_file(version);
    const ScnFile scn = read_scn_file(data, "synthetic");
    CHECK(scn.version == version);
    CHECK(scn.special_mission == (version >= 4 ? -1 : 0));
    CHECK(scn.random_seed == (version >= 4 ? 1234 : 0));
    CHECK(scn.end_on_lost_contact == (version >= 4 ? 0 : -1));
    CHECK(scn.required_memory == (version >= 7 ? 400000 : -1));
    CHECK(scn.minute == 9);
    CHECK(scn.equipment == 5);
    REQUIRE(scn.records.size() == 6);
    REQUIRE(std::holds_alternative<ScnShip>(scn.records[0]));
    REQUIRE(std::holds_alternative<ScnAircraft>(scn.records[1]));
    REQUIRE(std::holds_alternative<ScnPlayerSub>(scn.records[2]));
    REQUIRE(std::holds_alternative<ScnEnemySub>(scn.records[3]));
    REQUIRE(std::holds_alternative<BomberGroupRecord>(scn.records[4]));
    REQUIRE(std::holds_alternative<PhotoReconRecord>(scn.records[5]));
    const auto& ship = std::get<ScnShip>(scn.records[0]);
    CHECK(scn_text(ship.ship_class) == "FUSO");
    CHECK(ship.waypoints.x[24] == 124);
    CHECK(ship.waypoints.count == 150);
    CHECK(std::get<ScnAircraft>(scn.records[1]).relaunch == (version >= 6 ? 0 : 1));
    CHECK(std::get<ScnPlayerSub>(scn.records[2]).crew == (version >= 3 ? CrewQuality::kElite : CrewQuality::kGreen));
    CHECK(std::get<ScnEnemySub>(scn.records[3]).side == (version >= 5 ? 1 : 0));
    CHECK(std::get<BomberGroupRecord>(scn.records[4]).hold_min == 53);
    CHECK(std::get<PhotoReconRecord>(scn.records[5]).duration_min == 62);

    const std::vector<std::uint8_t> saved = write_scn_file(scn);
    CHECK(saved.size() == synthetic_file(7).size());  // every version-7 field
    ScnFile reread = read_scn_file(saved, "saved");
    reread.version = version;
    CHECK(reread == scn);
    if (version == 7) {
      CHECK(saved == data);
    }
  }
}

TEST_CASE("Passwords are stored with the key added to all 15 bytes", "[scn_file]") {
  SH_REQUIRE_GAME_DATA(install);
  // The encoded bytes (the password key added, NULs included).
  const std::array<std::uint8_t, kScnPasswordSize> secret = {'S', 'E' + 2, 'C' + 1, 'R' + 14, 'E' + 12, 'T' + 3, 4, 6,
                                                            5,   10,      13,      11,       9,        8,       7};
  CHECK(encode_scn_password("SECRET", sh::test::se_password_key(install)) == secret);
  CHECK(decode_scn_password(secret, sh::test::se_password_key(install)) == "SECRET");
  CHECK(encode_scn_password("", sh::test::se_password_key(install)) == std::array<std::uint8_t, kScnPasswordSize>{});
  CHECK(decode_scn_password(std::array<std::uint8_t, kScnPasswordSize>{}, sh::test::se_password_key(install)).empty());
  CHECK(decode_scn_password(encode_scn_password("14 characters!", sh::test::se_password_key(install)), sh::test::se_password_key(install)) ==
        "14 characters!");
  CHECK_THROWS_AS(encode_scn_password("fifteen chars!!", sh::test::se_password_key(install)), std::invalid_argument);

  // The game keeps the stored bytes up to the first zero byte.
  ScnFile scn = read_scn_file(synthetic_file(7), "synthetic");
  scn.password = secret;
  const Scenario game = parse_scenario(write_scn_file(scn), "synthetic");
  CHECK(game.header.password.size() == kScnPasswordSize);
}

TEST_CASE("Malformed .SCN files throw DataError", "[scn_file]") {
  const std::vector<std::uint8_t> data = synthetic_file(7);
  auto truncated = data;
  truncated.pop_back();  // no end tag
  CHECK_THROWS_AS(read_scn_file(truncated, "truncated"), DataError);
  truncated.resize(truncated.size() - 20);  // inside a record
  CHECK_THROWS_AS(read_scn_file(truncated, "truncated"), DataError);
  auto bad_tag = data;
  bad_tag.back() = 7;
  CHECK_THROWS_AS(read_scn_file(bad_tag, "tag"), DataError);
  auto bad_magic = data;
  bad_magic[0] = 'B';
  CHECK_THROWS_AS(read_scn_file(bad_magic, "magic"), DataError);
  auto version1 = data;
  version1[5] = 1;
  CHECK_THROWS_AS(read_scn_file(version1, "v1"), DataError);
  // Bytes after the end tag are ignored, as by the original.
  auto trailing = data;
  trailing.push_back(0xaa);
  CHECK(write_scn_file(read_scn_file(trailing, "trailing")) == data);
}

}  // namespace sh
