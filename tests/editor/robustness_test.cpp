// Damaged `.SCN` files. Every truncation of a file must be refused
// with `DataError`; a file with random bytes changed must be refused with
// `DataError` or read, and then load into the editor (or be refused with
// `DataError`), save, draw on the chart and read in the game without
// undefined behaviour (the sanitizers of the debug build watch). Then the
// regressions the damaged files found.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <variant>
#include <vector>

#include "assets/scenario.h"
#include "assets/scn_file.h"
#include "chart_rig.h"
#include "core/error.h"
#include "core/install.h"
#include "game_data.h"
#include "temp_dir.h"

namespace sh::editor {
namespace {

using test::ChartRig;

// Version-7 files (written back byte for byte, so their end tag is their
// last word) and an older one.
const std::vector<std::string> kFiles = {"SCEN/HIST60.SCN", "SCEN/HIST57.SCN", "SCEN/HIST45.SCN", "SCEN/HIST01.SCN",
                                         "CUSTOM/SITDUCK.SCN"};

// Returns `data` with `n` random bytes or words changed: random bytes,
// or whole words set to 0, -1, a large count or a large negative value.
std::vector<std::uint8_t> damaged(std::vector<std::uint8_t> data, std::mt19937& random) {
  std::uniform_int_distribution<std::size_t> offset(6, data.size() - 1);
  std::uniform_int_distribution<int> kind(0, 4);
  std::uniform_int_distribution<int> byte(0, 255);
  const int n = std::uniform_int_distribution<int>(1, 6)(random);
  for (int i = 0; i < n; ++i) {
    const std::size_t at = offset(random) & ~std::size_t{3};
    const int k = kind(random);
    if (k == 0 || at + 4 > data.size()) {
      data[offset(random)] = static_cast<std::uint8_t>(byte(random));
      continue;
    }
    constexpr std::int32_t kValues[] = {0, -1, 0x7fffffff, static_cast<std::int32_t>(0x80000000)};
    const std::int32_t v = k == 4 ? 26 + byte(random) : kValues[k - 1];
    for (int b = 0; b < 4; ++b) {
      data[at + static_cast<std::size_t>(b)] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * b));
    }
  }
  return data;
}

}  // namespace

TEST_CASE("Every truncation of a scenario file is refused", "[editor][robustness]") {
  SH_REQUIRE_INSTALL(install);
  for (const std::string& name : kFiles) {
    const std::vector<std::uint8_t> data = install.read(name);
    const ScnFile file = read_scn_file(data, name);
    if (file.version != 7) {
      continue;
    }
    for (std::size_t n = 0; n < data.size(); ++n) {
      INFO(std::format("{} cut at {}", name, n));
      const std::span<const std::uint8_t> cut(data.data(), n);
      CHECK_THROWS_AS(read_scn_file(cut, name), DataError);
      CHECK_THROWS_AS(parse_scenario(cut, name), DataError);
    }
  }
}

TEST_CASE("Damaged scenario files are refused or load, save and draw", "[editor][robustness]") {
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install);
  std::mt19937 random(7);
  int read = 0;
  int loaded = 0;
  for (int i = 0; i < 500; ++i) {
    const std::string& name = kFiles[static_cast<std::size_t>(i) % kFiles.size()];
    const std::vector<std::uint8_t> data = damaged(install.read(name), random);
    INFO(std::format("{} damage #{}", name, i));
    try {
      (void)parse_scenario(data, name);
    } catch (const DataError&) {
    }
    std::optional<ScnFile> file;
    try {
      file = read_scn_file(data, name);
    } catch (const DataError&) {
      continue;
    }
    ++read;
    try {
      if (!rig.scenario.load(*file)) {
        continue;
      }
    } catch (const DataError&) {
      rig.chart->world_changed();
      continue;
    }
    ++loaded;
    (void)write_scn_file(rig.scenario.save());
    rig.chart->world_changed();
    rig.chart->center_on_player();
    for (const auto& o : rig.scenario.objects()) {
      rig.chart->select(*o);
      rig.frames(1);
    }
  }
  // Most damage hits values the reader takes as they are.
  CHECK(read > 100);
  CHECK(loaded > 100);
}

TEST_CASE("A path far off the map is drawn without hanging", "[editor][robustness]") {
  // An aircraft of a damaged file at y = 2^31 - 1: clipping its path's legs
  // with the wrapping arithmetic never converged.
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install);
  Aircraft& a = rig.scenario.add_aircraft(0, AircraftMission::kFollowPath);
  const WorldPoint p = rig.scenario.player().position(rig.scenario.object_env());
  a.rec.waypoints.count = 2;
  a.rec.waypoints.x = {p.x, p.x + 1000, -1};
  a.rec.waypoints.y = {0x7fffffff, p.y, 0};
  a.rec.x = p.x;
  a.rec.y = 0x7fffffff;
  rig.chart->select(a);
  rig.frames(2);
  CHECK(rig.scenario.selected() == &a);
}

TEST_CASE("Values the editor indexes with are checked on load", "[editor][robustness]") {
  SH_REQUIRE_INSTALL(install);
  const ScnFile good = read_scn_file(install.read("SCEN/HIST57.SCN"), "HIST57.SCN");
  CHECK_NOTHROW(Scenario::check_file(good));
  const auto with = [&](auto change) {
    ScnFile f = good;
    change(f);
    return f;
  };
  const auto first = [](ScnFile& f) -> ScnShip& {
    for (ScnRecord& r : f.records) {
      if (auto* s = std::get_if<ScnShip>(&r)) {
        return *s;
      }
    }
    FAIL("no ship");
    throw;
  };
  CHECK_THROWS_AS(Scenario::check_file(with([](ScnFile& f) { f.month = 12; })), DataError);
  CHECK_THROWS_AS(Scenario::check_file(with([](ScnFile& f) { f.weather = static_cast<Weather>(4); })), DataError);
  CHECK_THROWS_AS(Scenario::check_file(with([](ScnFile& f) { f.year = 17; })), DataError);
  CHECK_THROWS_AS(Scenario::check_file(with([&](ScnFile& f) { first(f).waypoints.count = 25; })), DataError);
  CHECK_THROWS_AS(Scenario::check_file(with([&](ScnFile& f) { first(f).crew = static_cast<CrewQuality>(3); })),
                  DataError);
  CHECK_THROWS_AS(Scenario::check_file(with([&](ScnFile& f) { first(f).heading = 360; })), DataError);
  CHECK_THROWS_AS(Scenario::check_file(with([&](ScnFile& f) { first(f).convoy_group = 9; })), DataError);
  CHECK_NOTHROW(Scenario::check_file(with([&](ScnFile& f) { first(f).waypoints.count = 24; })));

  // A refused file leaves the scenario as it was.
  EditorOptions options;
  Scenario s(install, test::editor_data(install), options, [] { return 1000; });
  s.header.hour = 3;
  CHECK_THROWS_AS(s.load(with([](ScnFile& f) { f.month = -1; })), DataError);
  CHECK(s.header.hour == 3);
}

TEST_CASE("A leader of convoy 0 and huge depths are harmless", "[editor][robustness]") {
  // A damaged file: a Convoy Leader without a convoy (the original takes
  // the player for its leader), depths whose world units overflow.
  SH_REQUIRE_INSTALL(install);
  ChartRig rig(install, "SCEN/HIST57.SCN");
  Vessel* ship = nullptr;
  for (const auto& o : rig.scenario.objects()) {
    if (auto* v = dynamic_cast<Vessel*>(o.get()); v != nullptr && v->kind == Vessel::Kind::kShip) {
      ship = v;
      break;
    }
  }
  REQUIRE(ship != nullptr);
  ship->ship.orders = ShipOrders::kConvoyLeader;
  ship->ship.convoy_group = 0;
  CHECK(rig.scenario.convoy_leader(0) == nullptr);
  CHECK(rig.scenario.ship_max_speed(*ship) == 0);
  rig.chart->select(*ship);
  rig.frames(2);
  rig.scenario.player().rec.depth = 0x7fffffff;
  for (const auto& o : rig.scenario.objects()) {
    if (auto* v = dynamic_cast<Vessel*>(o.get()); v != nullptr && v->kind == Vessel::Kind::kSubmarine) {
      v->sub.depth = -0x7fffffff;
    }
  }
  rig.scenario.update_all_visibility();
  rig.frames(2);
}

TEST_CASE("Reading a directory as a file is refused", "[editor][robustness]") {
  // A damaged world name ("" or a directory's) made `read_file` allocate
  // the directory's bogus size.
  const test::TempDir dir;
  CHECK_THROWS_AS(read_file(dir.path()), DataError);
}

}  // namespace sh::editor
