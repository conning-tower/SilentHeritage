// The manifest of the original's file structure (synthetic/manifest.h):
// extracted by "[.synthetic_manifest]", checked against the install.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include "game_data.h"
#include "assets/exe_image.h"
#include "synthetic/exe_manifest.h"
#include "synthetic/exe_rules.h"
#include "synthetic/le_writer.h"
#include "synthetic/manifest.h"
#include "synthetic/table_manifest.h"

namespace sh::test {
namespace {

TEST_CASE("The file manifest is extracted from the install", "[.synthetic_manifest]") {
  SH_REQUIRE_GAME_DATA(install);
  std::filesystem::create_directories(manifest_path().parent_path());
  std::ofstream out(manifest_path(), std::ios::trunc);
  out << format_manifest(extract_manifest(install));
  REQUIRE(out.good());
  std::ofstream tables(manifest_path().parent_path() / "tables.txt", std::ios::trunc);
  tables << format_table_manifest(extract_table_manifest(install));
  REQUIRE(tables.good());
}

TEST_CASE("The committed file manifest reads back and describes the install", "[synthetic]") {
  const Manifest& manifest = committed_manifest();
  CHECK(parse_manifest(format_manifest(manifest)) == manifest);
  CHECK(manifest.contains("ART/CONN.PCX"));
  SH_REQUIRE_GAME_DATA(install);
  CHECK(extract_table_manifest(install) == committed_table_manifest());
  const Manifest extracted = extract_manifest(install);
  CHECK(extracted.size() == manifest.size());
  for (const auto& [path, m] : extracted) {
    INFO(path);
    const auto it = manifest.find(path);
    REQUIRE(it != manifest.end());
    CHECK(it->second == m);
  }
}

}  // namespace
}  // namespace sh::test

namespace sh::test {
namespace {

#ifdef SH_TRACE_EXE_READS
// Run in a build with SH_TRACE_EXE_READS, SH_TRACE_EXE naming the trace of
// a whole test run (both test programs) of that build.
TEST_CASE("The executables' manifests are extracted from the install", "[.synthetic_exe_manifest]") {
  SH_REQUIRE_GAME_DATA(install);
  const char* trace = std::getenv("SH_TRACE_EXE");
  REQUIRE(trace != nullptr);
  for (const std::string_view name : {"sh_exe", "se_exe"}) {
    const ExeImage image(install.read(name == "sh_exe" ? "SH.EXE" : "SE.EXE"));
    const ExeManifest m = extract_exe_manifest(image, read_exe_trace(trace, image.end()), exe_rules(name));
    std::ofstream out(exe_manifest_path(name), std::ios::trunc);
    out << format_exe_manifest(m);
    REQUIRE(out.good());
  }
}
#endif

TEST_CASE("The synthetic executables load like the original's", "[synthetic]") {
  for (const std::string_view name : {"sh_exe", "se_exe"}) {
    INFO(name);
    const ExeManifest& m = committed_exe_manifest(name);
    CHECK(parse_exe_manifest(format_exe_manifest(m)) == m);
    const ExeImage image(write_le_executable(m));
    CHECK(image.fixup_consistency() == 1.0);
    for (const ExeManifest::String& s : m.strings) {
      CHECK(image.c_string(s.address) == s.text);
    }
    for (const ExeManifest::Pointer& p : m.pointers) {
      CHECK(image.u32(p.address) == p.target);
    }
    for (const ExeManifest::Value& v : m.values) {
      CHECK((v.size == 1 ? image.u8(v.address) : v.size == 2 ? image.u16(v.address) : image.u32(v.address)) == v.value);
    }
  }
}

TEST_CASE("Synthetic text keeps the structure of a string, file names whole", "[synthetic]") {
  const std::string text = synthetic_text("Ship %s at %02d:%02d.\nDepth: 120 ft", 7);
  CHECK(text.size() == 35);
  CHECK(text.substr(4, 4) == " %s ");
  CHECK(text.find("%02d:%02d.\n") == 11);
  CHECK(text.find("120") != std::string::npos);
  CHECK(text != "Ship %s at %02d:%02d.\nDepth: 120 ft");
  CHECK(synthetic_text("SPEECH\\HELM0380.RAW", 7) == "SPEECH\\HELM0380.RAW");
}

}  // namespace
}  // namespace sh::test
