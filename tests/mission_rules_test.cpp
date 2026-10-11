// The extended variant's mission rules (game/mission_rules.h): their RULE
// section and EXTENDED.CFG lines, and their way through a mission and its
// save.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "assets/dat_tables.h"
#include "core/error.h"
#include "game/mission.h"
#include "game/mission_rules.h"
#include "game/mission_save.h"
#include "game/saves_options.h"
#include "game/sim_state.h"
#include "game_data.h"
#include "sim/state_io.h"

namespace sh {
namespace {

class TempDir {
 public:
  TempDir() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() / ("sh_mission_rules_test_" + std::to_string(stamp));
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::span<const std::uint8_t> as_bytes(std::string_view s) {
  return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}

// Returns a RULE section of the given key/value entries.
std::vector<std::uint8_t> section(const std::vector<std::pair<std::string_view, std::int32_t>>& entries) {
  ByteWriter out;
  out.u32(static_cast<std::uint32_t>(entries.size()));
  for (const auto& [key, value] : entries) {
    out.u8(static_cast<std::uint8_t>(key.size()));
    out.bytes(as_bytes(key));
    out.i32(value);
  }
  return out.take();
}

MissionRules fixed() {
  MissionRules r;
  r.fix_obvious_errors = true;
  return r;
}

TEST_CASE("Mission rules round-trip through their RULE section and EXTENDED.CFG lines", "[rules]") {
  for (const MissionRules& rules : {MissionRules{}, fixed()}) {
    CHECK(decode_mission_rules(encode_mission_rules(rules)) == rules);
    CHECK(parse_mission_rules_config(mission_rules_config(rules)) == rules);
  }
  CHECK(encode_mission_rules(fixed()) == section({{"fix_obvious_errors", 1}, {"quiet_sonar_reacquire", 0}}));
  CHECK(mission_rules_config(fixed()) == "rule.fix_obvious_errors = 1\nrule.quiet_sonar_reacquire = 0\n");
}

TEST_CASE("Mission rules skip unknown keys and keep the defaults of missing ones", "[rules]") {
  CHECK(decode_mission_rules(section({{"a_later_rule", 7}, {"fix_obvious_errors", 1}})) == fixed());
  CHECK(decode_mission_rules(section({})) == MissionRules{});
  // A truncated section is damaged.
  std::vector<std::uint8_t> cut = section({{"fix_obvious_errors", 1}});
  cut.pop_back();
  CHECK_THROWS_AS(decode_mission_rules(cut), DataError);

  // Other settings, unknown rules, bad values and stray text are skipped.
  CHECK(parse_mission_rules_config("ui.wide_screen = 1\r\nrule.a_later_rule = 3\n"
                                   "  rule.fix_obvious_errors\t=  1  \r\nnot a setting\n") == fixed());
  CHECK(parse_mission_rules_config("rule.fix_obvious_errors = yes\n") == MissionRules{});
  CHECK(parse_mission_rules_config("rule.fix_obvious_errors = 1x") == MissionRules{});
  CHECK(parse_mission_rules_config("") == MissionRules{});
}

TEST_CASE("EXTENDED.CFG gives the rules of the save directory, the original's without one", "[rules]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  CHECK(load_mission_rules(saves) == MissionRules{});
  REQUIRE(saves.write(kExtendedConfigName, as_bytes(mission_rules_config(fixed()))));
  CHECK(load_mission_rules(saves) == fixed());
}

// Returns a historical mission run for a while with `rules`.
std::unique_ptr<Mission> played(const Install& install, const GameTables& tables, const MissionRules& rules) {
  MissionSetup setup;
  setup.options = RealismFile::kDefaultBits;
  setup.random = WatcomRandom(7);
  setup.rules = rules;
  std::unique_ptr<Mission> m = Mission::load(install, tables, "SCEN/HIST01.SCN", setup);
  for (int i = 0; i < 200 && m->end_code() == 0; ++i) {
    m->simulate_frame(10, false);
  }
  return m;
}

// Returns the mission restored from `file` with set-up rules `setup_rules`.
std::unique_ptr<Mission> restored(const Install& install, const GameTables& tables,
                                  const std::vector<std::uint8_t>& file, const MissionRules& setup_rules) {
  const std::optional<MissionSaveFile> parsed = MissionSaveFile::parse(file);
  REQUIRE(parsed);
  MissionSetup setup;
  setup.random = WatcomRandom(1);
  setup.rules = setup_rules;
  return restore_mission(install, tables, *parsed, setup);
}

TEST_CASE("A mission keeps its rules, and its save carries them only when they are not the original's",
          "[rules][save]") {
  SH_REQUIRE_GAME_DATA(install);
  const GameTables tables = GameTables::load(install);
  const std::unique_ptr<Mission> original = played(install, tables, {});
  const std::unique_ptr<Mission> fixing = played(install, tables, fixed());
  CHECK(original->sim().conditions().rules == MissionRules{});
  CHECK(fixing->sim().conditions().rules == fixed());

  const MissionSaveFile original_save = capture_mission_save(install, *original, 0, "Rules", {});
  const MissionSaveFile fixing_save = capture_mission_save(install, *fixing, 0, "Rules", {});
  CHECK(original_save.state.rules_extension.empty());
  CHECK(fixing_save.state.rules_extension == encode_mission_rules(fixed()));

  // The original's rules write the files as before; other rules only
  // append their section (no rule reads the field yet, so both missions ran
  // alike).
  const std::vector<std::uint8_t> original_file = original_save.serialize();
  const std::vector<std::uint8_t> fixing_file = fixing_save.serialize();
  MissionSaveFile stripped = fixing_save;
  stripped.state.rules_extension.clear();
  CHECK(stripped.serialize() == original_file);
  std::vector<std::uint8_t> appended = original_file;
  ByteWriter rule;
  rule.u32(0x454c5552);  // "RULE"
  rule.u32(static_cast<std::uint32_t>(fixing_save.state.rules_extension.size()));
  rule.bytes(fixing_save.state.rules_extension);
  appended.insert(appended.end(), rule.data().begin(), rule.data().end());
  CHECK(fixing_file == appended);

  // A restored mission takes the rules of its save, not of its set-up: the
  // original's when the save has no RULE section.
  CHECK(restored(install, tables, fixing_file, {})->sim().conditions().rules == fixed());
  CHECK(restored(install, tables, original_file, fixed())->sim().conditions().rules == MissionRules{});
  const std::unique_ptr<Mission> back = restored(install, tables, fixing_file, {});
  CHECK(capture_mission_save(install, *back, 0, "Rules", {}).state.rules_extension ==
        encode_mission_rules(fixed()));
}

}  // namespace
}  // namespace sh
