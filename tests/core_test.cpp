#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/byte_reader.h"
#include "core/error.h"
#include "core/fixed.h"
#include "core/install.h"
#include "core/random.h"
#include "game_data.h"

namespace sh {

TEST_CASE("ByteReader reads little-endian values and bounded strings") {
  const std::array<std::uint8_t, 12> data{0x34, 0x12, 0x78, 0x56, 0x34, 0x12, 'A', 'B', 0, 'Z', 0xff, 0xff};
  ByteReader r(data, "test");
  CHECK(r.u16() == 0x1234);
  CHECK(r.u32() == 0x12345678);
  CHECK(r.fixed_string(4) == "AB");
  CHECK(r.i16() == -1);
  CHECK(r.at_end());
  CHECK_THROWS_AS(r.u8(), DataError);
}

TEST_CASE("Fixed16 multiplies and divides like the original") {
  const Fixed16 a = Fixed16::from_int(3);
  const Fixed16 b = Fixed16::from_raw(0x8000);  // 0.5
  CHECK((a * b).raw() == 0x18000);
  CHECK((a / b) == Fixed16::from_int(6));
  CHECK(Fixed16::from_raw(-0x8000).floor_int() == -1);
}

TEST_CASE("Angle10 normalizes into one turn") {
  CHECK(Angle10(-10).normalized().tenths() == 3590);
  CHECK(Angle10(7300).normalized().tenths() == 100);
}

TEST_CASE("WatcomRandom follows the Watcom C library sequence") {
  WatcomRandom r;
  // First values of the Watcom rand() from its initial seed 1.
  CHECK(r.rand() == 16838);
  CHECK(r.rand() == 5758);
  CHECK(r.rand() == 10113);
  CHECK(r.random_int(0) == 0);
}

TEST_CASE("Install finds files case-insensitively") {
  SH_REQUIRE_GAME_DATA(install);
  CHECK(install.exists("data/subs.dat"));
  CHECK(install.exists("DATA\\SUBS.DAT"));
  CHECK(install.exists("data"));
  CHECK_FALSE(install.exists("DATA/NOPE.DAT"));
  CHECK_FALSE(install.try_read("DATA/NOPE.DAT").has_value());
  CHECK_THROWS_AS(install.read("DATA/NOPE.DAT"), DataError);
  CHECK(install.try_read("data\\subs.dat") == install.read("DATA/SUBS.DAT"));
  CHECK_FALSE(install.list("SCEN", ".SCN").empty());
}

namespace {

// A packaged tree in memory, by exact path; counts the reads.
struct FakePackage {
  std::map<std::string, std::vector<std::uint8_t>> files;
  std::shared_ptr<int> reads = std::make_shared<int>(0);

  Install::PackageReader reader() const {
    return [files = files, reads = reads](const std::string& path) -> std::optional<std::vector<std::uint8_t>> {
      ++*reads;
      const auto it = files.find(path);
      if (it == files.end()) {
        return std::nullopt;
      }
      return it->second;
    };
  }
};

constexpr std::string_view kIndex =
    "SH-BUNDLE 2\n"
    "2 SH.EXE\n"
    "5 DATA/SUBS.DAT\n"
    "3 DATA/Atlantic.WLD\n"
    "1 DATA/SUB/DEEP.WLD\n"
    "4 SCEN/a.scn\n"
    "0 GONE.TXT\n";

FakePackage fake_package() {
  return {.files = {{"SH.EXE", {'M', 'Z'}},
                    {"DATA/SUBS.DAT", {1, 2, 3, 4, 5}},
                    {"DATA/Atlantic.WLD", {6, 7, 8}},
                    {"DATA/SUB/DEEP.WLD", {9}},
                    {"SCEN/a.scn", {1, 1, 1}}}};
}

}  // namespace

TEST_CASE("A packaged install looks its index up case-insensitively", "[install]") {
  const FakePackage package = fake_package();
  const Install install = Install::packaged(kIndex, package.reader(), "test package");

  CHECK(install.exists("sh.exe"));
  CHECK(install.exists("data\\atlantic.wld"));
  CHECK(install.exists("./DATA//Sub/deep.wld"));
  CHECK(install.exists("data"));
  CHECK(install.exists("DATA/SUB"));
  CHECK(install.exists(""));
  CHECK_FALSE(install.exists("DATA/NOPE.DAT"));
  CHECK_FALSE(install.exists("DAT"));
  CHECK(*package.reads == 0);

  // Reads go to the file as spelled in the index.
  CHECK(install.read("data/ATLANTIC.wld") == std::vector<std::uint8_t>{6, 7, 8});
  CHECK(install.try_read("Data\\Subs.Dat") == std::vector<std::uint8_t>{1, 2, 3, 4, 5});
  CHECK(*package.reads == 2);

  // A missing file is not read; a directory is not a file.
  CHECK_FALSE(install.try_read("DATA/NOPE.DAT").has_value());
  CHECK_FALSE(install.try_read("DATA/SUB").has_value());
  CHECK_THROWS_AS(install.read("DATA/NOPE.DAT"), DataError);
  CHECK(*package.reads == 2);

  // Listed but unreadable, or of another size than the index's: an error.
  CHECK_THROWS_AS(install.try_read("GONE.TXT"), DataError);
  CHECK_THROWS_AS(install.read("SCEN/A.SCN"), DataError);
}

TEST_CASE("A packaged install lists a directory by extension", "[install]") {
  const FakePackage package = fake_package();
  const Install install = Install::packaged(kIndex, package.reader(), "test package");

  CHECK(install.list("data", ".wld") == std::vector<std::string>{"DATA/ATLANTIC.WLD"});
  CHECK(install.list("DATA\\", ".DAT") == std::vector<std::string>{"DATA/SUBS.DAT"});
  CHECK(install.list("DATA/SUB", ".WLD") == std::vector<std::string>{"DATA/SUB/DEEP.WLD"});
  CHECK(install.list("", ".EXE") == std::vector<std::string>{"SH.EXE"});
  CHECK(install.list("", "") == std::vector<std::string>{"GONE.TXT", "SH.EXE"});
  CHECK(install.list("SCEN", ".SCN") == std::vector<std::string>{"SCEN/A.SCN"});
  CHECK(install.list("NOPE", ".SCN").empty());
  CHECK(install.list("DAT", ".WLD").empty());
}

TEST_CASE("A packaged install rejects a malformed index", "[install]") {
  const FakePackage package = fake_package();
  const auto make = [&](std::string_view index) { return Install::packaged(index, package.reader(), "test"); };
  CHECK_NOTHROW(make("SH-BUNDLE 2\n2 SH.EXE\n"));
  CHECK_NOTHROW(make("SH-BUNDLE 2\r\n5 DATA/SUBS.DAT\r\n"));
  CHECK_THROWS_AS(make(""), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 1\n2 SH.EXE\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2SH.EXE\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\nx SH.EXE\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2 SH.EXE\n2 ../SH.EXE\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2 SH.EXE\n2 /SH.EXE\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2 SH.EXE\n2 DATA\\X.DAT\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2 SH.EXE\n2 DATA//X.DAT\n"), DataError);
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2 SH.EXE\n2 sh.exe\n"), DataError);
  // Not an install: neither SH.EXE nor DATA/.
  CHECK_THROWS_AS(make("SH-BUNDLE 2\n2 SE.EXE\n"), DataError);
}

}  // namespace sh
