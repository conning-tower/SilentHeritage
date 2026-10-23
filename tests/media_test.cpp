// The original CDs' containers: ARJ archives (core/arj.h), ISO 9660 images
// (core/iso9660.h), and the game's media found in a folder or an image and
// installed (core/game_media.h), over synthetic archives and images
// (synthetic/media_writer.h). With SH_MEDIA naming a v1.31 CD (a copy of its
// files or its image) and SH_DATA its install, the media's files are
// checked against the install's.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "core/arj.h"
#include "core/byte_source.h"
#include "core/crc32.h"
#include "core/error.h"
#include "core/game_media.h"
#include "core/install.h"
#include "core/iso9660.h"
#include "editor/temp_dir.h"
#include "game_data.h"
#include "synthetic/media_writer.h"

namespace sh {
namespace {

using editor::test::TempDir;
using test::ArjFileSpec;
using test::IsoFileSpec;

std::vector<std::uint8_t> bytes(std::string_view s) { return {s.begin(), s.end()}; }

void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& data) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(data.data()),
                                              static_cast<std::streamsize>(data.size()));
}

std::vector<std::uint8_t> random_bytes(std::size_t n, std::uint32_t seed) {
  std::mt19937 random(seed);
  std::vector<std::uint8_t> out(n);
  for (std::uint8_t& b : out) {
    b = static_cast<std::uint8_t>(random());
  }
  return out;
}

// Text with repeats near and far, runs longer than a match, and a few
// random bytes: literals and matches of every length and distance class.
std::vector<std::uint8_t> mixed_bytes(std::size_t n) {
  std::vector<std::uint8_t> out;
  std::mt19937 random(7);
  const std::string words[] = {"periscope ", "torpedo ", "depth charge ", "convoy ", "Marus ", "\r\n"};
  while (out.size() < n) {
    switch (random() % 4) {
      case 0:
        out.insert(out.end(), 300 + random() % 300, static_cast<std::uint8_t>(random()));
        break;
      case 1:
        for (int k = 0; k < 20; ++k) {
          out.push_back(static_cast<std::uint8_t>(random()));
        }
        break;
      default:
        for (int k = 0; k < 30; ++k) {
          const std::string& w = words[random() % std::size(words)];
          out.insert(out.end(), w.begin(), w.end());
        }
    }
  }
  out.resize(n);
  return out;
}

ArjFileSpec arj_file(std::string path, std::vector<std::uint8_t> data, std::uint8_t method = 1,
                     std::uint8_t flags = 0, std::uint32_t continued_at = 0) {
  return ArjFileSpec{.path = std::move(path),
                     .data = std::move(data),
                     .method = method,
                     .flags = flags,
                     .continued_at = continued_at,
                     .directory = false};
}

ArjFileSpec arj_dir(std::string path) {
  return ArjFileSpec{.path = std::move(path), .data = {}, .method = 0, .flags = 0, .continued_at = 0, .directory = true};
}

ArjArchive archive_of(const std::vector<std::uint8_t>& data) { return ArjArchive(memory_source(data, "test.arj")); }

TEST_CASE("ARJ files decompress with every method", "[media]") {
  const std::vector<std::vector<std::uint8_t>> inputs = {
      {}, bytes("x"), bytes("abcabcabcabcabcabc"), mixed_bytes(40000), random_bytes(3000, 1),
      std::vector<std::uint8_t>(5000, 'z')};
  for (const std::uint8_t method : std::initializer_list<std::uint8_t>{0, 1, 2, 3, 4}) {
    for (const std::vector<std::uint8_t>& data : inputs) {
      CAPTURE(method, data.size());
      const std::vector<std::uint8_t> packed = test::arj_compress(data, method);
      CHECK(arj_decompress(packed, method, data.size(), "test") == data);
    }
  }
  SECTION("method 1 over several blocks of 65535 codes") {
    const std::vector<std::uint8_t> data = random_bytes(140000, 2);
    CHECK(arj_decompress(test::arj_compress(data, 1), 1, data.size(), "test") == data);
  }
}

TEST_CASE("An ARJ archive lists its files and extracts them", "[media]") {
  const std::vector<std::uint8_t> text = mixed_bytes(9000);
  const std::vector<ArjFileSpec> files = {
      arj_file("SH.EXE", text, 1),
      arj_dir("DATA"),
      arj_file("DATA/SUBS.DAT", bytes("subs"), 0),
      arj_file("ART/X.PCX", text, 4),
  };
  // A self-extractor's code before the archive.
  const ArjArchive archive = archive_of(test::write_arj("SH.A00", files, random_bytes(5000, 3)));
  const std::vector<ArjEntry>& entries = archive.entries();
  REQUIRE(entries.size() == 4);
  CHECK(entries[0].path == "SH.EXE");
  CHECK(entries[1].directory);
  CHECK(entries[2].path == "DATA/SUBS.DAT");
  CHECK(entries[3].method == 4);
  CHECK(archive.extract(entries[0]) == text);
  CHECK(archive.extract(entries[2]) == bytes("subs"));
  CHECK(archive.extract(entries[3]) == text);
}

TEST_CASE("Corrupt, garbled and foreign ARJ data is refused", "[media]") {
  const std::vector<std::uint8_t> text = mixed_bytes(4000);
  SECTION("not an archive") { CHECK_THROWS_AS(archive_of(random_bytes(3000, 4)), DataError); }
  SECTION("a damaged header") {
    std::vector<std::uint8_t> data = test::write_arj("SH.A00", {arj_file("A", text)});
    data[0x40] ^= 0xff;  // in the file header
    CHECK_THROWS_AS(archive_of(data), DataError);
  }
  SECTION("damaged data") {
    std::vector<std::uint8_t> data = test::write_arj("SH.A00", {{.path = "A", .data = text}});
    data[data.size() - 100] ^= 0x5a;
    const ArjArchive archive = archive_of(data);
    CHECK_THROWS_AS(archive.extract(archive.entries()[0]), DataError);
  }
  SECTION("a truncated archive") {
    std::vector<std::uint8_t> data = test::write_arj("SH.A00", {{.path = "A", .data = text}});
    data.resize(data.size() - 200);
    CHECK_THROWS_AS(archive_of(data), DataError);
  }
  SECTION("a password") {
    const ArjArchive archive = archive_of(test::write_arj("SH.A00", {arj_file("A", text, 1, 0x01)}));
    CHECK_THROWS_AS(archive.extract(archive.entries()[0]), DataError);
  }
  SECTION("an unknown method") {
    CHECK_THROWS_AS(arj_decompress(text, 7, text.size(), "test"), DataError);
  }
}

TEST_CASE("An ISO 9660 image lists its files without versions", "[media]") {
  const std::vector<std::uint8_t> big = random_bytes(5000, 5);
  const auto image = memory_source(test::write_iso({{.path = "README.TXT", .data = bytes("read me")},
                                                    {.path = "SH/SH.A00", .data = big},
                                                    {.path = "SH/BUD/1.SMK", .data = bytes("smk")}}),
                                   "test.iso");
  REQUIRE(looks_like_iso(*image));
  const IsoImage iso(image);
  std::vector<std::string> paths;
  for (const IsoFile& f : iso.files()) {
    paths.push_back(f.path);
  }
  std::ranges::sort(paths);
  CHECK(paths == std::vector<std::string>{"README.TXT", "SH/BUD/1.SMK", "SH/SH.A00"});
  const auto a00 = std::ranges::find(iso.files(), "SH/SH.A00", &IsoFile::path);
  CHECK(iso.open(*a00)->read_all() == big);
  CHECK_FALSE(looks_like_iso(*memory_source(random_bytes(40000, 6), "x")));
  CHECK_THROWS_AS(IsoImage(memory_source(std::vector<std::uint8_t>(40000, 0), "x")), DataError);
}

// The CD's SH folder of a game: two volumes (SH.A01 replacing DATA/SUBS.DAT
// and continuing ART/BIG.PCX from SH.A00), a folder read from the CD
// (BUD) and one also archived (SPEECH), and the installer's own files.
std::vector<IsoFileSpec> cd_files(const std::vector<std::uint8_t>& big) {
  const std::vector<std::uint8_t> first(big.begin(), big.begin() + 3000);
  const std::vector<std::uint8_t> rest(big.begin() + 3000, big.end());
  return {
      {.path = "INSTALL.BAT", .data = bytes("cd sh")},
      {.path = "SH/INSTALL.EXE", .data = bytes("MZ installer")},
      {.path = "SH/SH.A00",
       .data = test::write_arj("SH.A00", {arj_file("SH.EXE", bytes("MZ game")),
                                          arj_file("DATA/SUBS.DAT", bytes("old subs"), 4),
                                          arj_file("SPEECH/HELLO.RAW", bytes("archived")),
                                          arj_file("ART/BIG.PCX", first, 1, 0x04)})},
      {.path = "SH/SH.A01",
       .data = test::write_arj("SH.A01", {arj_file("ART/BIG.PCX", rest, 1, 0x08, 3000),
                                          arj_file("DATA/SUBS.DAT", bytes("new subs"))})},
      {.path = "SH/BUD/1.SMK", .data = bytes("interview")},
      {.path = "SH/SPEECH/HELLO.RAW", .data = bytes("loose")},
  };
}

void write_tree(const std::filesystem::path& dir, const std::vector<IsoFileSpec>& files) {
  for (const IsoFileSpec& f : files) {
    write_bytes(dir / f.path, f.data);
  }
}

void check_cd_media(const GameMedia& media, const std::vector<std::uint8_t>& big) {
  std::vector<std::string> paths;
  for (const GameMedia::File& f : media.files) {
    paths.push_back(f.path);
  }
  std::ranges::sort(paths);
  CHECK(paths ==
        std::vector<std::string>{"ART/BIG.PCX", "BUD/1.SMK", "DATA/SUBS.DAT", "SH.EXE", "SPEECH/HELLO.RAW"});
  CHECK(media.find("art/big.pcx")->read() == big);
  CHECK(media.find("art/big.pcx")->size == big.size());
  CHECK(media.find("DATA/SUBS.DAT")->read() == bytes("new subs"));
  CHECK(media.find("SPEECH/HELLO.RAW")->read() == bytes("archived"));
  CHECK(media.find("BUD/1.SMK")->read() == bytes("interview"));
}

TEST_CASE("The game's media are found in the CD's folders and images", "[media]") {
  const TempDir dir("sh_media_test_");
  const std::vector<std::uint8_t> big = mixed_bytes(7000);
  write_tree(dir.path() / "cd", cd_files(big));

  SECTION("the CD's root") {
    const std::optional<GameMedia> media = find_game_media(host_media_tree(dir.path() / "cd"));
    REQUIRE(media);
    check_cd_media(*media, big);
  }
  SECTION("its SH folder") {
    const std::optional<GameMedia> media = find_game_media(host_media_tree(dir.path() / "cd" / "SH"));
    REQUIRE(media);
    check_cd_media(*media, big);
  }
  SECTION("an image, opened or in the folder chosen") {
    write_bytes(dir.path() / "images" / "Game.iso", test::write_iso(cd_files(big)));
    const std::optional<GameMedia> in_folder = find_game_media(host_media_tree(dir.path() / "images"));
    REQUIRE(in_folder);
    check_cd_media(*in_folder, big);
    const std::optional<GameMedia> opened =
        find_game_media(iso_media_tree(host_file_source(dir.path() / "images" / "Game.iso")));
    REQUIRE(opened);
    check_cd_media(*opened, big);
  }
  SECTION("none") {
    write_bytes(dir.path() / "other" / "SH" / "README.TXT", bytes("x"));
    CHECK_FALSE(find_game_media(host_media_tree(dir.path() / "other")));
  }
}

TEST_CASE("The media are installed into a host folder", "[media]") {
  const TempDir dir("sh_media_test_");
  const std::vector<std::uint8_t> big = mixed_bytes(7000);
  write_tree(dir.path() / "cd", cd_files(big));
  const std::optional<GameMedia> media = find_game_media(host_media_tree(dir.path() / "cd"));
  REQUIRE(media);
  write_bytes(dir.path() / "install" / "OLD.TXT", bytes("previous install"));
  std::vector<std::uint64_t> progress;
  install_game_media(*media, dir.path() / "install", [&](std::uint64_t done) { progress.push_back(done); });
  CHECK(progress.size() == media->files.size());
  CHECK(progress.back() == media->total_size());
  CHECK_FALSE(std::filesystem::exists(dir.path() / "install" / "OLD.TXT"));
  CHECK_FALSE(std::filesystem::exists(dir.path() / "install.tmp"));
  const Install install(dir.path() / "install");
  CHECK(install.read("ART/BIG.PCX") == big);
  CHECK(install.read("DATA/SUBS.DAT") == bytes("new subs"));
  CHECK(install.read("BUD/1.SMK") == bytes("interview"));
  CHECK_FALSE(install.exists("INSTALL.EXE"));
}

TEST_CASE("Media with unsafe paths, broken volumes or no game are refused", "[media]") {
  const TempDir dir("sh_media_test_");
  const auto media_of = [&](const std::string& name, const std::vector<std::vector<ArjFileSpec>>& volumes) {
    const std::filesystem::path cd = dir.path() / name;
    for (std::size_t v = 0; v < volumes.size(); ++v) {
      const std::string volume = std::format("SH.A{:02}", v);
      write_bytes(cd / volume, test::write_arj(volume, volumes[v]));
    }
    return find_game_media(host_media_tree(cd));
  };
  const ArjFileSpec exe = arj_file("SH.EXE", bytes("MZ"));
  CHECK_THROWS_AS(media_of("climbs", {{exe, arj_file("../../ESCAPE.TXT", bytes("x"))}}), DataError);
  CHECK_THROWS_AS(media_of("absolute", {{exe, arj_file("C:/ESCAPE.TXT", bytes("x"))}}), DataError);
  CHECK_THROWS_AS(media_of("unfinished", {{exe, arj_file("BIG", bytes("x"), 1, 0x04)}}), DataError);
  CHECK_THROWS_AS(media_of("orphan", {{exe}, {arj_file("BIG", bytes("x"), 1, 0x08, 5)}}),
                  DataError);
  CHECK_THROWS_AS(media_of("no game", {{arj_file("README.TXT", bytes("x"))}}), DataError);
}

// With SH_MEDIA set, the files of the media that the install SH_DATA also
// holds are the same (an install keeps a few files of its own, and its
// player's).
TEST_CASE("The media of a v1.31 CD hold its install's files", "[media]") {
  const char* media_path = std::getenv("SH_MEDIA");
  const std::optional<Install> install = test::game_install();
  if (media_path == nullptr || *media_path == '\0' || !install) {
    SKIP("SH_MEDIA and SH_DATA not both set");
  }
  const std::filesystem::path path(media_path);
  const std::optional<GameMedia> media = find_game_media(
      std::filesystem::is_directory(path) ? host_media_tree(path) : iso_media_tree(host_file_source(path)));
  REQUIRE(media);
  int compared = 0;
  for (const GameMedia::File& f : media->files) {
    const std::optional<std::vector<std::uint8_t>> installed = install->try_read(f.path);
    if (!installed || f.path.ends_with(".INI") || f.path.ends_with(".TXT")) {
      continue;
    }
    CAPTURE(f.path);
    CHECK(f.read() == *installed);
    ++compared;
  }
  CHECK(compared > 900);
}

}  // namespace
}  // namespace sh
