// The asset setup's logic (app/install_source.h): the remembered source,
// the checks of candidate folders, the start-up decision, and the document
// tree install (core/install.h) over a fake document provider.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/install_source.h"
#include "core/error.h"
#include "core/install.h"
#include "editor/temp_dir.h"

namespace sh {
namespace {

using editor::test::TempDir;

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}

// Makes a minimal install in `dir`: SH.EXE and DATA/SUBS.DAT, in `case`'s
// spelling.
void make_install(const std::filesystem::path& dir, bool lower_case = false) {
  write_text(dir / (lower_case ? "sh.exe" : "SH.EXE"), "MZ");
  write_text(dir / (lower_case ? "data/subs.dat" : "DATA/SUBS.DAT"), "subs");
}

// An in-memory document provider: directories and files by id, counting
// the listings.
class FakeTree final : public Install::DocumentTree {
 public:
  // Adds entry `name` under directory `parent` ("root" for the root);
  // `content` is ignored for directories. Returns the new id.
  std::string add(const std::string& parent, const std::string& name, bool directory, std::string content = {}) {
    const std::string id = std::format("doc{}", next_++);
    dirs_[parent].push_back(
        Entry{.name = name, .id = id, .directory = directory, .size = directory ? 0 : content.size()});
    if (!directory) {
      contents_[id] = std::move(content);
    }
    return id;
  }

  std::string root() const override { return "root"; }

  std::vector<Entry> children(const std::string& id) const override {
    ++listings;
    if (id == unlistable) {
      throw DataError("cannot list " + id);
    }
    const auto it = dirs_.find(id);
    return it == dirs_.end() ? std::vector<Entry>() : it->second;
  }

  std::optional<std::vector<std::uint8_t>> read(const std::string& id) const override {
    const auto it = contents_.find(id);
    if (it == contents_.end() || id == unreadable) {
      return std::nullopt;
    }
    std::string content = it->second;
    if (id == truncated) {
      content.pop_back();
    }
    return std::vector<std::uint8_t>(content.begin(), content.end());
  }

  std::string describe() const override { return "the fake tree"; }

  mutable std::atomic<int> listings = 0;
  std::string unlistable;
  std::string unreadable;
  std::string truncated;

 private:
  int next_ = 0;
  std::map<std::string, std::vector<Entry>> dirs_;
  std::map<std::string, std::string> contents_;
};

// A tree holding sh.exe, Data/SUBS.DAT (and Data/Subs.dat, hidden by it in
// byte order), Data/Art/x.pcx, data/EXTRA.DAT (a second "DATA" directory,
// hidden by the first) and names that cannot be paths.
std::shared_ptr<FakeTree> make_tree() {
  auto tree = std::make_shared<FakeTree>();
  tree->add("root", "sh.exe", false, "MZ..");
  const std::string data = tree->add("root", "Data", true);
  tree->add(data, "Subs.dat", false, "duplicate");
  tree->add(data, "SUBS.DAT", false, "subs");
  const std::string art = tree->add(data, "Art", true);
  tree->add(art, "x.pcx", false, "pcx");
  const std::string other = tree->add("root", "data", true);
  tree->add(other, "EXTRA.DAT", false, "extra");
  tree->add("root", "a/b", false, "slash");
  tree->add("root", "..", true);
  tree->add("root", "", false, "empty");
  return tree;
}

TEST_CASE("The remembered install source is saved and read back", "[setup]") {
  const TempDir dir("sh_setup_test_");
  const std::filesystem::path file = dir.path() / "prefs" / "INSTALL.CFG";
  CHECK_FALSE(load_install_config(file).has_value());

  for (const InstallSource& source : {
           InstallSource{.kind = InstallSource::Kind::kFolder, .location = "/games/Silent Hunter"},
           InstallSource{.kind = InstallSource::Kind::kDocumentTree,
                         .location = "content://com.android.externalstorage.documents/tree/primary%3ASH"},
           InstallSource{.kind = InstallSource::Kind::kCopy, .location = "/data/user/0/app/files/install"},
       }) {
    save_install_config(file, source);
    CHECK(load_install_config(file) == source);
  }

  write_text(file, "SH-INSTALL 1\r\nfolder C:\\SH\r\n");
  CHECK(load_install_config(file) ==
        InstallSource{.kind = InstallSource::Kind::kFolder, .location = "C:\\SH"});

  CHECK_THROWS_AS(save_install_config(file, InstallSource{.location = "two\nlines"}), std::runtime_error);
  for (const char* bad : {"", "SH-INSTALL 2\nfolder /x\n", "SH-INSTALL 1\nfolder\n", "SH-INSTALL 1\ndisk /x\n",
                          "SH-INSTALL 1\n"}) {
    write_text(file, bad);
    CHECK_THROWS_AS(load_install_config(file), DataError);
  }
}

TEST_CASE("A candidate folder needs SH.EXE and DATA in any case", "[setup]") {
  const TempDir dir("sh_setup_test_");
  make_install(dir.path() / "upper");
  make_install(dir.path() / "lower", true);
  CHECK(check_install_folder(dir.path() / "upper").install.has_value());
  const InstallCheck lower = check_install_folder(dir.path() / "lower");
  REQUIRE(lower.install.has_value());
  CHECK(lower.install->read("DATA/SUBS.DAT").size() == 4);

  write_text(dir.path() / "no_exe" / "DATA" / "SUBS.DAT", "subs");
  CHECK(check_install_folder(dir.path() / "no_exe").problem == "SH.EXE is missing");
  write_text(dir.path() / "no_data" / "SH.EXE", "MZ");
  CHECK(check_install_folder(dir.path() / "no_data").problem == "the DATA folder is missing");

  // Neither: Install's own check.
  std::filesystem::create_directories(dir.path() / "empty");
  const InstallCheck empty = check_install_folder(dir.path() / "empty");
  CHECK_FALSE(empty.install.has_value());
  CHECK(empty.problem.find("is not a Silent Hunter install") != std::string::npos);

  CHECK(check_install_folder(dir.path() / "missing").problem.ends_with("is not a folder"));
  CHECK(check_install_folder(dir.path() / "upper" / "SH.EXE").problem.ends_with("is not a folder"));
}

TEST_CASE("The start-up decision between --data, the remembered source and the setup", "[setup]") {
  const TempDir dir("sh_setup_test_");
  const std::filesystem::path good = dir.path() / "good";
  const std::filesystem::path other = dir.path() / "other";
  make_install(good);
  make_install(other);
  const std::filesystem::path bad = dir.path() / "bad";
  write_text(bad / "README.TXT", "not an install");
  const InstallSource remembered{.kind = InstallSource::Kind::kFolder, .location = other.string()};
  const InstallSource gone{.kind = InstallSource::Kind::kFolder, .location = (dir.path() / "gone").string()};

  SECTION("first start: the setup, nothing to report") {
    const StartupInstall s = choose_startup_install(std::nullopt, false, std::nullopt, open_folder_source);
    CHECK_FALSE(s.install.has_value());
    CHECK(s.problems.empty());
  }
  SECTION("--data wins and is not remembered") {
    const StartupInstall s = choose_startup_install(good, false, remembered, open_folder_source);
    REQUIRE(s.install.has_value());
    CHECK_FALSE(s.source.has_value());
  }
  SECTION("the remembered source") {
    const StartupInstall s = choose_startup_install(std::nullopt, false, remembered, open_folder_source);
    REQUIRE(s.install.has_value());
    CHECK(s.source == remembered);
  }
  SECTION("a bad --data falls back to the remembered source, reported") {
    const StartupInstall s = choose_startup_install(bad, false, remembered, open_folder_source);
    CHECK(s.install.has_value());
    REQUIRE(s.problems.size() == 1);
    CHECK(s.problems[0].starts_with("--data "));
  }
  SECTION("a remembered folder gone: the setup again") {
    const StartupInstall s = choose_startup_install(bad, false, gone, open_folder_source);
    CHECK_FALSE(s.install.has_value());
    REQUIRE(s.problems.size() == 2);
    CHECK(s.problems[1].starts_with("The game files are no longer usable at " + gone.location));
  }
  SECTION("--setup") {
    const StartupInstall s = choose_startup_install(good, true, remembered, open_folder_source);
    CHECK_FALSE(s.install.has_value());
    CHECK(s.problems.empty());
  }
  SECTION("a document tree outside Android: the setup again") {
    const InstallSource tree{.kind = InstallSource::Kind::kDocumentTree, .location = "content://x/tree/y"};
    const StartupInstall s = choose_startup_install(std::nullopt, false, tree, open_folder_source);
    CHECK_FALSE(s.install.has_value());
    CHECK(s.problems.size() == 1);
  }
  SECTION("a copy is a host folder") {
    const InstallSource copy{.kind = InstallSource::Kind::kCopy, .location = good.string()};
    CHECK(choose_startup_install(std::nullopt, false, copy, open_folder_source).install.has_value());
  }
}

TEST_CASE("A document tree install is indexed once and read case-insensitively", "[setup]") {
  const std::shared_ptr<FakeTree> tree = make_tree();
  const Install install = Install::document_tree(tree);
  const int listings = tree->listings;
  CHECK(listings == 3);  // root, Data, Data/Art

  CHECK(install.exists("SH.EXE"));
  CHECK(install.exists("data\\art"));
  CHECK(install.read("DATA/SUBS.DAT") == std::vector<std::uint8_t>{'s', 'u', 'b', 's'});
  CHECK(install.read("data/art/X.PCX").size() == 3);
  // The second "data" directory is hidden by "Data".
  CHECK_FALSE(install.exists("DATA/EXTRA.DAT"));
  CHECK(install.list("DATA", ".dat") == std::vector<std::string>{"DATA/SUBS.DAT"});
  CHECK(install.list("", ".EXE") == std::vector<std::string>{"SH.EXE"});
  CHECK_FALSE(install.try_read("DATA/MISSING.DAT").has_value());
  CHECK(tree->listings == listings);

  const std::vector<DocumentFile> files = list_document_files(*tree);
  std::vector<std::string> paths;
  for (const DocumentFile& f : files) {
    paths.push_back(f.path);
  }
  CHECK(paths == std::vector<std::string>{"Data/Art/x.pcx", "Data/SUBS.DAT", "sh.exe"});
}

TEST_CASE("A document tree install reports unreadable documents and bad trees", "[setup]") {
  SECTION("a document that cannot be opened or is shorter than listed") {
    auto tree = std::make_shared<FakeTree>();
    tree->unreadable = tree->add("root", "SH.EXE", false, "MZ");
    tree->add("root", "DATA", true);
    tree->truncated = tree->add("root", "GAME.CFG", false, "config");
    const Install install = Install::document_tree(tree);
    CHECK_THROWS_AS(install.read("SH.EXE"), DataError);
    CHECK_THROWS_AS(install.read("GAME.CFG"), DataError);
  }
  SECTION("not an install") {
    auto tree = std::make_shared<FakeTree>();
    tree->add("root", "README.TXT", false, "hello");
    CHECK_THROWS_AS(Install::document_tree(tree), DataError);
    CHECK(check_install([&] { return Install::document_tree(tree); }).problem.find("not a Silent Hunter install") !=
          std::string::npos);
  }
  SECTION("a folder that can no longer be listed") {
    auto tree = make_tree();
    tree->unlistable = "root";
    CHECK(check_install([&] { return Install::document_tree(tree); }).problem == "cannot list root");
  }
}

TEST_CASE("The files of a document tree are copied into a host folder", "[setup]") {
  const TempDir dir("sh_setup_test_");
  const std::shared_ptr<FakeTree> tree = make_tree();
  const std::vector<DocumentFile> files = list_document_files(*tree);
  std::vector<std::uint64_t> progress;
  copy_document_files(*tree, files, dir.path() / "install", [&](std::uint64_t done) { progress.push_back(done); });
  CHECK(progress == std::vector<std::uint64_t>{3, 7, 11});
  const InstallCheck copy = check_install_folder(dir.path() / "install");
  REQUIRE(copy.install.has_value());
  CHECK(copy.install->read("DATA/ART/X.PCX") == std::vector<std::uint8_t>{'p', 'c', 'x'});

  tree->truncated = files[1].id;
  CHECK_THROWS_AS(copy_document_files(*tree, files, dir.path() / "again", [](std::uint64_t) {}), DataError);
}

}  // namespace
}  // namespace sh
