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
#include "game/extended_config.h"
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

MissionRules fixed() { return MissionRules::extended(); }

MissionRules keen() {
  MissionRules r = MissionRules::extended();
  r.keen_escort_sonar = true;
  return r;
}

TEST_CASE("Mission rules round-trip through their RULE section and EXTENDED.CFG lines", "[rules]") {
  for (const MissionRules& rules : {MissionRules{}, fixed(), keen()}) {
    CHECK(decode_mission_rules(encode_mission_rules(rules)) == rules);
    CHECK(parse_mission_rules_config(mission_rules_config(rules)) == rules);
    CHECK(parse_mission_rules_config(mission_rules_config(rules), keen()) == rules);
  }
  CHECK(encode_mission_rules(keen()) ==
        section({{"fix_obvious_errors", 1}, {"quiet_sonar_reacquire", 1}, {"keen_escort_sonar", 1}}));
  CHECK(mission_rules_config(fixed()) ==
        "rule.fix_obvious_errors = 1\nrule.quiet_sonar_reacquire = 1\nrule.keen_escort_sonar = 0\n");
  // The extended game's rules: the corrections, deafened escorts.
  CHECK(fixed().escorts_deafened());
  CHECK_FALSE(keen().escorts_deafened());
  CHECK_FALSE(MissionRules{}.escorts_deafened());
}

TEST_CASE("Mission rules skip unknown keys and keep the defaults of missing ones", "[rules]") {
  CHECK(decode_mission_rules(section({{"a_later_rule", 7}, {"fix_obvious_errors", 1}})) ==
        MissionRules{.fix_obvious_errors = true});
  CHECK(decode_mission_rules(section({})) == MissionRules{});
  // A truncated section is damaged.
  std::vector<std::uint8_t> cut = section({{"fix_obvious_errors", 1}});
  cut.pop_back();
  CHECK_THROWS_AS(decode_mission_rules(cut), DataError);

  // Other settings, unknown rules, bad values and stray text are skipped.
  CHECK(parse_mission_rules_config("ui.wide_screen = 1\r\nrule.a_later_rule = 3\n"
                                   "  rule.fix_obvious_errors\t=  1  \r\nnot a setting\n") ==
        MissionRules{.fix_obvious_errors = true});
  CHECK(parse_mission_rules_config("rule.fix_obvious_errors = yes\n") == MissionRules{});
  CHECK(parse_mission_rules_config("rule.fix_obvious_errors = 1x") == MissionRules{});
  CHECK(parse_mission_rules_config("") == MissionRules{});
  // Missing fields keep the given defaults.
  CHECK(parse_mission_rules_config("rule.keen_escort_sonar = 1\n", MissionRules::extended()) == keen());
}

TEST_CASE("EXTENDED.CFG gives the rules of the save directory, the extended defaults without one", "[rules]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  CHECK(load_mission_rules(saves) == MissionRules::extended());
  REQUIRE(saves.write(kExtendedConfigName, as_bytes(mission_rules_config(MissionRules{}))));
  CHECK(load_mission_rules(saves) == MissionRules{});
  // Saving the rules keeps the file's other settings.
  REQUIRE(saves.write(kExtendedConfigName, as_bytes("ui.wide_screen = 1\nrule.fix_obvious_errors = 0\n")));
  REQUIRE(save_mission_rules(saves, keen()));
  CHECK(load_mission_rules(saves) == keen());
  const std::optional<std::vector<std::uint8_t>> text = saves.read(kExtendedConfigName);
  REQUIRE(text);
  CHECK(std::string(text->begin(), text->end()) ==
        "ui.wide_screen = 1\nrule.fix_obvious_errors = 1\nrule.quiet_sonar_reacquire = 1\nrule.keen_escort_sonar = 1\n");
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
  // Rules that change nothing in the simulation (the realism choice alone
  // counts only in the rating), so that both missions run alike.
  const MissionRules marked{.keen_escort_sonar = true};
  const std::unique_ptr<Mission> fixing = played(install, tables, marked);
  CHECK(original->sim().conditions().rules == MissionRules{});
  CHECK(fixing->sim().conditions().rules == marked);

  const MissionSaveFile original_save = capture_mission_save(install, *original, 0, "Rules", {});
  const MissionSaveFile fixing_save = capture_mission_save(install, *fixing, 0, "Rules", {});
  CHECK(original_save.state.rules_extension.empty());
  CHECK(fixing_save.state.rules_extension == encode_mission_rules(marked));

  // The original's rules write the files as before; other rules only
  // append their section.
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

  // A restored mission takes the rules of its save, not of its set-up; a
  // save without a RULE section (the original's, a faithful build's) keeps
  // those of its set-up.
  CHECK(restored(install, tables, fixing_file, {})->sim().conditions().rules == marked);
  CHECK(restored(install, tables, original_file, keen())->sim().conditions().rules == keen());
  CHECK(restored(install, tables, original_file, {})->sim().conditions().rules == MissionRules{});
  const std::unique_ptr<Mission> back = restored(install, tables, fixing_file, {});
  CHECK(capture_mission_save(install, *back, 0, "Rules", {}).state.rules_extension ==
        encode_mission_rules(marked));
}

TEST_CASE("EXTENDED.CFG keeps its sections apart and survives unknown lines", "[rules][extended]") {
  const TempDir dir;
  const SaveDir saves(dir.path());
  CHECK(load_features(saves, Features::extended()) == Features::extended());
  CHECK_FALSE(load_last_boat(saves));
  REQUIRE(saves.write(kExtendedConfigName, as_bytes("# a comment\nui.crew_log = 0\nui.someday = 1\nui.chart_tools = x\n")));
  Features f = load_features(saves, Features::extended());
  CHECK_FALSE(f.crew_log);
  CHECK(f.chart_tools);  // a bad value keeps the default
  f.wide_screen = false;
  REQUIRE(save_features(saves, f));
  REQUIRE(save_last_boat(saves, "S CLASS"));
  REQUIRE(save_mission_rules(saves, keen()));
  CHECK(load_features(saves, Features::extended()) == f);
  CHECK(load_last_boat(saves) == "S CLASS");
  CHECK(load_mission_rules(saves) == keen());
  REQUIRE(save_last_boat(saves, "GATO"));
  CHECK(load_last_boat(saves) == "GATO");
  // The comment and the unknown key stay.
  const std::string text = read_config(saves);
  CHECK(text.starts_with("# a comment\n"));
  CHECK(text.find("ui.someday = 1") == std::string::npos);  // the ui. section is rewritten whole
  CHECK(parse_config(text).at("ui.crew_log") == "0");
  CHECK(parse_config(text).at("state.last_boat") == "GATO");
}

}  // namespace
}  // namespace sh
